#!/bin/sh
# Copyright (c) 2026 Kevin Dedon.
# SPDX-License-Identifier: MIT

#
# bannermutate.sh -- the mutation gate for the system identification.
#
# A check that cannot fail cannot be merged.  `make verify-banner' greps a
# cold boot for three lines, and a grep for a string that is always there
# proves nothing -- so this rebuilds cpm.sys with one deliberate defect at a
# time, boots each broken build on the emulator, and requires
# tests/bannerchk.sh to reject every one.  It reports which assertion caught
# each defect and, at the end, the set of assertion names ever seen to fail:
# a name a reviewer expects in that list and does not find belongs to a
# check that is proving nothing.
#
# The defects are the plausible ones: a version left at its old value, a
# date left at its old value, a copyright year frozen, a line dropped, the
# lines printed in the wrong order.  Three are make-variable overrides --
# the version and the dates are build inputs, which is the whole point of
# the CPMVER block in the Makefile -- and three are edits to
# src/bdos/bdosmisc.c, applied to the real file and restored on any exit.
#
# Run from the repository root as `make verify-banner-mutants'.  Each mutant is a
# relink and a full cold boot -- slow on purpose: it proves the assertion
# reaches the running system rather than a host-side string.

set -u
: ${EMU:=$(sh tools/deps.sh emu)}
: ${KBOOT:=$(sh tools/deps.sh kboot)}
: ${CPMSYS:=build/cpm.sys}
: ${CPMAIMG:=build/cpma.img}
: ${CPMBIMG:=build/cpmb.img}
: ${EMUMAX:=600000000}
: ${CPMVER:?the true version must be passed in}
: ${CPMDATE:?the true date must be passed in}
: ${COPYYEAR:?the true year must be passed in}

SRC=src/bdos/bdosmisc.c
work=build/banmut
img=$work/mutant.bin
seen=$work/seen
fail=0

sh tools/deps.sh -n kboot "$KBOOT" || exit 1
sh tools/deps.sh -n emu "$EMU" || exit 1

rm -rf $work
mkdir -p $work
here=`pwd`
absimg=$here/$img
cp $SRC $work/bdosmisc.orig
restore() { cp $work/bdosmisc.orig $SRC; }
trap 'restore' 0 1 2 3 15
: > $seen

# Boot the build that is currently in build/ and run the REAL assertions
# against it.  The expected values handed to bannerchk are always the true
# ones: a mutant is caught precisely by disagreeing with them.
# 0 = every assertion held, 1 = some did not, 2 = the run never happened.
checkrun() {
	python3 tools/mkcpmdisk.py --kboot="$KBOOT" $img "$CPMSYS" "$CPMAIMG" \
		"$CPMBIMG" > $work/patch.log 2>&1 || return 2
	( cd "$EMU/bin" && ./c900 --disk="$absimg" --input="DIR *.TXT\r" \
		--max=$EMUMAX 2>/dev/null ) > $work/boot.log
	sh tests/bannerchk.sh $work/boot.log "$CPMVER" "$CPMDATE" "$COPYYEAR" \
		> $work/chk.log 2>&1
}

caught() {
	sed -n 's/^FAIL \[\([a-z]*\)\].*/\1/p' $work/chk.log | sort -u
}

# mutate NAME DESCRIPTION [MAKE-VAR=VALUE ...]
# Any source edit is already in $SRC when this is called.
mutate() {
	name=$1; desc=$2; shift 2

	if ! make "$@" all > $work/build.log 2>&1; then
		echo "MUTANT $name: would not build -- $desc"
		tail -3 $work/build.log
		fail=`expr $fail + 1`
		restore
		return
	fi
	checkrun
	case $? in
	2)	echo "MUTANT $name: could not build the test medium"
		fail=`expr $fail + 1`;;
	0)	echo "SURVIVED $name: $desc"
		fail=`expr $fail + 1`;;
	*)	caught >> $seen
		echo "caught  $name -- `caught | tr '\n' ' '` ($desc)";;
	esac
	restore
}

# edit SED-SCRIPT NAME -- apply a defect to src/bdos/bdosmisc.c and insist it
# changed something.  A mutation that does not apply is itself a failure:
# it means the code moved and the defect is no longer being injected.
edit() {
	restore
	sed -e "$1" $SRC > $work/tmp.c
	if cmp -s $work/tmp.c $SRC; then
		echo "MUTANT $2: the defect did not apply"
		fail=`expr $fail + 1`
		return 1
	fi
	cp $work/tmp.c $SRC
	return 0
}

# ---- the build inputs ----
# The whole reason the version and the dates come from the build is that a
# literal in the source goes stale.  These three are that staleness.
mutate version-and-date-stale \
	"the 1983 identification, unchanged -- what the banner said before this work" \
	CPMVER=1.2 CPMDATE=03/14/83
mutate date-stale "the version moved but the date was forgotten" \
	CPMDATE=03/14/83
mutate year-stale "the contributors' year frozen instead of derived" \
	COPYYEAR=1983

# ---- the lines themselves ----
edit '/prt_line(SYS_COPYRIGHT);/d' no-contributors &&
	mutate no-contributors "the contributors' line never printed"
edit '/Digital Research Inc., Zilog Inc/d' no-dri &&
	mutate no-dri "the 1982 DRI/Zilog attribution dropped"
edit '/prt_line(SYS_COPYRIGHT);/d
s|^\( *\)\(prt_line(.*Copyright 1982.*\)$|\1prt_line(SYS_COPYRIGHT);\n\1\2|' \
	order-swapped &&
	mutate order-swapped \
		"the contributors' line printed before the 1982 attribution"

# ---- and the boot that produced nothing at all ----
# The assertion that fires when the emulator never reached the BDOS.  It
# needs no rebuild and can have none: it is exercised by handing the
# checker an empty transcript.
: > $work/empty.log
if sh tests/bannerchk.sh $work/empty.log "$CPMVER" "$CPMDATE" "$COPYYEAR" \
		> $work/chk.log 2>&1; then
	echo "SURVIVED empty-transcript: a boot that printed nothing passed"
	fail=`expr $fail + 1`
else
	caught >> $seen
	echo "caught  empty-transcript -- `caught | tr '\n' ' '` (the boot produced nothing)"
fi

# leave the tree holding a correct system, not the last mutant
restore
if ! make all > $work/build.log 2>&1; then
	echo "verify-banner-mutants: the restored tree does not build"
	tail -5 $work/build.log
	exit 1
fi

echo
echo "assertions observed failing: `sort -u $seen | tr '\n' ' '`"
if [ $fail -ne 0 ]; then
	echo "verify-banner-mutants: FAIL -- $fail defect(s) not caught"
	exit 1
fi
echo "verify-banner-mutants: PASS -- every injected defect was caught"
exit 0
