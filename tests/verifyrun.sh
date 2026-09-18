#!/bin/sh
# Copyright (c) 2026 Kevin Dedon.
# SPDX-License-Identifier: MIT
#
# verifyrun.sh -- run the verify suite, several targets at a time.
#
#   sh tests/verifyrun.sh JOBS MAKE SUMMARY
#
# Run from the repository root by `make verify-all'.  Every target still
# gets its own $(MAKE) and its own emulator, exactly as when the suite ran
# one after another; JOBS of them are simply in flight at once.  Three
# things make that safe and all three are set up here:
#
#   1. `all' and every verify target's file prerequisites (the media, the
#      fixture images, the host-side tools) are built ONCE, serially, before
#      any worker starts -- phase 1 below.  Two workers that each built
#      build/cpma-conc.img would be writing the same file.
#
#   2. The workers run with VERIFYCHILD=1, which stops `all' republishing
#      cpm.sys and the drive images under the other workers' feet (see the
#      Makefile's `unpublish').  Phase 1 has just built and checked them.
#
#   3. A target that is a prerequisite of another verify target runs in the
#      SAME worker as it (`verify-boot' with `verify-bootgate'), because two
#      makes would otherwise run it twice, at once, over one transcript.
#
# Targets that rebuild the tree from mutated sources own build/ and src/
# while they run: they go last, alone, and without VERIFYCHILD, since the
# whole point of them is that make rebuilds what they broke.
#
# Output: one log per worker in build/verify-run/, the same PASS/FAIL
# summary as before in SUMMARY, and a non-zero exit if any target failed.

set -u

RUNDIR=build/verify-run

# The suite's own targets, read from the makefile at run time: a target
# added there is picked up without being registered here.
targets() {
	sed -n 's/^\(verify-[a-z0-9-]*\):.*/\1/p' tests/verify.mk |
		grep -Ev '^verify-(all|zcc)$' | sort -u
}

# The verify-* prerequisites of one target, if any.
vdeps() {
	sed -n "s/^$1:[ 	]*\(.*\)/\1/p" tests/verify.mk |
		tr ' \011' '\012\012' | grep '^verify-' | sort -u
}

# One worker: run make over the targets of one job line, keep its output,
# and record a verdict for every target it covered.
job() {
	mode=$1; mk=$2; shift 2
	first=$1
	s=`date +%s`
	if [ "$mode" = child ]; then
		$mk --no-print-directory VERIFYCHILD=1 \
			LOG=$RUNDIR/$first.build.log "$@" \
			> $RUNDIR/$first.log 2>&1 && r=PASS || r=FAIL
	else
		$mk --no-print-directory "$@" \
			> $RUNDIR/$first.log 2>&1 && r=PASS || r=FAIL
	fi
	e=`date +%s`
	d=`expr $e - $s`
	for t in "$@"; do
		echo "$r $t" >> $RUNDIR/results
		echo "$d $t" >> $RUNDIR/times
	done
	echo "$r $* (${d}s, $RUNDIR/$first.log)"
}

# Re-entry from xargs: one job line, one worker.
if [ "${1:-}" = --job ]; then
	mk=$2
	shift 2
	job child "$mk" $*
	exit 0
fi

J=${1:?usage: verifyrun.sh JOBS MAKE SUMMARY}
MAKE=${2:?usage: verifyrun.sh JOBS MAKE SUMMARY}
SUM=${3:?usage: verifyrun.sh JOBS MAKE SUMMARY}

# The workers are ordinary serial makes.  Drop an outer `-j' so they do not
# queue on its jobserver; keep everything else in MAKEFLAGS, which is where
# variables given to the outer make (EMU=, KBOOT=, ...) travel.
if [ -n "${MAKEFLAGS:-}" ]; then
	MAKEFLAGS=`printf '%s' "$MAKEFLAGS" | sed \
		-e 's/--jobserver-auth=[^ ]*//g' \
		-e 's/--jobserver-fds=[^ ]*//g' \
		-e 's/ -j[0-9]*/ /g'`
	export MAKEFLAGS
fi

EXCL='verify-banner-mutants verify-initdir-mutants'

rm -rf $RUNDIR
mkdir -p $RUNDIR
: > $RUNDIR/results

echo "=== phase 1: the build and the fixtures every target shares"
$MAKE --no-print-directory all || exit 1
# -k, and the status is not checked: a fixture that cannot be built here
# (build/cpmhost without cpmtools, say) must fail the targets that need it
# and no others, which is what happened when they ran one after another.
$MAKE --no-print-directory -k verifyprep || true

# Jobs: a line per worker, a target in exactly one line.
jobs=$RUNDIR/jobs
: > $jobs
merged=''
for t in `targets`; do
	d=`vdeps $t`
	[ -n "$d" ] || continue
	echo `echo $d` $t >> $jobs
	merged="$merged `echo $d` $t"
done
for t in `targets`; do
	case " $merged " in *" $t "*) continue;; esac
	case " $EXCL " in *" $t "*) continue;; esac
	echo $t >> $jobs
done

# Longest job first, from the last run's own timings: one target is a third
# of the suite, and started late it is the whole tail.  A job with no
# recorded time (a target added since) goes first rather than last, which is
# the safe way to be wrong about it.
TIMES=build/verify-times
if [ -f $TIMES ]; then
	while read -r line; do
		set -- $line
		d=`awk -v t="$1" '$2 == t { print $1 }' $TIMES | tail -1`
		echo "${d:-9999} $line"
	done < $jobs | sort -k1,1nr | cut -d' ' -f2- > $jobs.ordered
	mv $jobs.ordered $jobs
fi

echo "=== phase 2: `wc -l < $jobs` jobs, $J at a time"
xargs -P "$J" -I@@ sh tests/verifyrun.sh --job "$MAKE" @@ < $jobs

echo "=== phase 3: the mutation gates, one at a time"
for t in $EXCL; do
	# Each of these rebuilds the tree from broken sources and restores it;
	# the next one starts from a build made of the real ones.
	$MAKE --no-print-directory all > $RUNDIR/$t.rebuild.log 2>&1 ||
		{ echo "FAIL $t (the rebuild before it failed)"; \
		  echo "FAIL $t" >> $RUNDIR/results; continue; }
	job serial "$MAKE" $t
done

[ -s $RUNDIR/times ] && cp $RUNDIR/times $TIMES
sort -k2 $RUNDIR/results > $SUM
pass=`grep -c '^PASS' $SUM` || pass=0
fail=`grep -c '^FAIL' $SUM` || fail=0
echo "=== verify-all summary ($SUM)"
cat $SUM
echo "verify-all: $pass passed, $fail failed, `expr $pass + $fail` run"
[ "$fail" -eq 0 ]
