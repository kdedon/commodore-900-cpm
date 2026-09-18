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
# one after another; JOBS of them are simply in flight at once.  Four
# things make that safe and all four are set up here:
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
#   4. The two mutation gates break src/ on purpose and re-run `make all'
#      over the wreckage, so they cannot share src/ or build/ with anybody.
#      Phase 1 gives each of them a whole tree of its own under build/mut/
#      and they then run as ordinary jobs -- see mutsetup() below.
#
# Output: one log per worker in build/verify-run/, the same PASS/FAIL
# summary as before in SUMMARY, and a non-zero exit if any target failed.

set -u

RUNDIR=build/verify-run

# Where a mutation gate's private tree lives: build/mut/<target>.  The name
# is the target's own, which is what lets a worker recognise on re-entry
# that the job it was handed has a tree and must be run inside it.
MUTDIR=build/mut

# The gates that need one.  A target listed here gets a tree; everything
# else runs against the shared one, as before.
MUTS='verify-banner-mutants verify-initdir-mutants'

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

# mutsetup TARGET -- give one mutation gate a source tree of its own.
#
# A PLAIN COPY, not a checkout: the copy is of the WORKING tree, so an
# uncommitted edit comes along with it and the gate is testing the same
# sources as the rest of the suite.  A gate built from HEAD instead would
# go quietly vacuous on exactly the change somebody is testing, which is
# the one moment it is there for.  Nothing here touches the repository:
# a build has no business moving git's state about.
#
# build/ is not copied -- the copy makes its own, which is the point --
# and neither is deps/, a read-only input the copy borrows through a
# relative symlink.  Dot-entries are skipped wholesale: the build reads
# none of them, and .git and .claude are large and are state, not source.
#
# The tree is REMADE FROM SCRATCH each run rather than refreshed in place.
# That costs one full build per gate, paid inside the parallel phase, and
# it buys the only property worth having here: whatever a run that died
# half way through left behind -- a mutated source, a half-written object
# -- cannot survive into the next run and be mistaken for a verdict.
mutsetup() {
	d=$MUTDIR/$1
	rm -rf $d
	mkdir -p $d
	for e in *; do
		case $e in build|deps) continue;; esac
		cp -R "$e" $d/
	done
	ln -s ../../../deps $d/deps
}

# The dependency variables to hand a gate's make.  tools/deps.sh looks in
# $root/deps first, which the symlink above answers -- but when there is no
# deps/ at all and the toolchain, kboot and the emulator were resolved from
# sibling checkouts instead, that search cannot reach out of build/mut/<t>/
# (it walks at most three parents).  So resolve them HERE, once, in the tree
# where the search works, and pass the answers down.  Unresolved is left
# unset rather than passed empty, so the copy's own search still runs.
mutdeps() {
	for p in toolchain:C900_TOOLCHAIN kboot:KBOOT kbootsrc:KBOOTSRC \
		 emu:EMU cpmtools:CPMTOOLS; do
		v=`sh tools/deps.sh "${p%%:*}"` || v=
		[ -n "$v" ] && printf '%s=%s ' "${p#*:}" "$v"
	done
}

# One worker: run make over the targets of one job line, keep its output,
# and record a verdict for every target it covered.
job() {
	mode=$1; mk=$2; shift 2
	first=$1
	s=`date +%s`
	case $mode in
	child)
		$mk --no-print-directory VERIFYCHILD=1 \
			LOG=$RUNDIR/$first.build.log "$@" \
			> $RUNDIR/$first.log 2>&1 && r=PASS || r=FAIL
		;;
	tree)
		# A mutation gate, in the tree phase 1 made for it.  No
		# VERIFYCHILD: the whole point of the gate is that make
		# rebuilds what it broke, and there is nobody else in that
		# tree for the rebuild to disturb.
		$mk --no-print-directory -C $MUTDIR/$first \
			${MUTDEPS:-} "$@" \
			> $RUNDIR/$first.log 2>&1 && r=PASS || r=FAIL
		;;
	*)
		$mk --no-print-directory "$@" \
			> $RUNDIR/$first.log 2>&1 && r=PASS || r=FAIL
		;;
	esac
	e=`date +%s`
	d=`expr $e - $s`
	for t in "$@"; do
		echo "$r $t" >> $RUNDIR/results
		echo "$d $t" >> $RUNDIR/times
	done
	echo "$r $* (${d}s, $RUNDIR/$first.log)"
}

# Re-entry from xargs: one job line, one worker.  A job whose first target
# has a tree under build/mut/ is run inside it; everything else is an
# ordinary child.  MUTDEPS comes down through the environment.
if [ "${1:-}" = --job ]; then
	mk=$2
	shift 2
	if [ -d "$MUTDIR/$1" ]; then
		job tree "$mk" $*
	else
		job child "$mk" $*
	fi
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

rm -rf $RUNDIR
mkdir -p $RUNDIR
: > $RUNDIR/results

echo "=== phase 1: the build, the fixtures, and the mutation gates' own trees"
$MAKE --no-print-directory all || exit 1
# -k, and the status is not checked: a fixture that cannot be built here
# (build/cpmhost without cpmtools, say) must fail the targets that need it
# and no others, which is what happened when they ran one after another.
$MAKE --no-print-directory -k verifyprep || true
for t in $MUTS; do mutsetup $t; done
MUTDEPS=`mutdeps`
export MUTDEPS

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
	echo $t >> $jobs
done

# Longest job first, from the last run's own timings: one target is a third
# of the suite, and started late it is the whole tail.  A job with no
# recorded time (a target added since) goes first rather than last, which is
# the safe way to be wrong about it.  The mutation gates sort to the front
# on their own merit -- each is a full build plus several rebuilds -- and
# they need to, because a gate started late is the tail all over again.
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

[ -s $RUNDIR/times ] && cp $RUNDIR/times $TIMES
sort -k2 $RUNDIR/results > $SUM
pass=`grep -c '^PASS' $SUM` || pass=0
fail=`grep -c '^FAIL' $SUM` || fail=0
echo "=== verify-all summary ($SUM)"
cat $SUM
echo "verify-all: $pass passed, $fail failed, `expr $pass + $fail` run"
[ "$fail" -eq 0 ]
