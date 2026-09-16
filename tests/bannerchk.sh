#!/bin/sh
#
# bannerchk.sh -- the assertions on the cold-start system identification.
#
# Reads a cold-boot transcript and checks the three lines bdosinit prints
# (src/bdos/bdosmisc.c) against what the build says they should be.  Split out of
# the Makefile because tests/bannermutate.sh runs exactly the same assertions
# against deliberately broken builds and needs to report which one caught
# each defect -- a check nobody has ever seen fail is not a check.
#
#   sh tests/bannerchk.sh LOG VERSION DATE YEAR
#
# VERSION/DATE/YEAR are the Makefile's CPMVER/CPMDATE/COPYYEAR.  Passing
# them in rather than reading them back out of the image is the point: the
# comparison is against the value the build was told to use, so a banner
# that silently kept an older string is a failure rather than a tautology.
#
# Prints one `FAIL [name] ...' line per broken assertion, in the shape
# tests/rtctest.c uses, and exits nonzero if there were any.

log=$1; ver=$2; date=$3; year=$4
if [ $# -ne 4 ]; then
	echo "usage: $0 LOG VERSION DATE YEAR" >&2
	exit 2
fi
if [ ! -s "$log" ]; then
	echo "FAIL [transcript] $log is missing or empty: the boot produced nothing"
	exit 1
fi

work=`mktemp`
trap 'rm -f "$work"' 0
tr -d '\r' < "$log" > "$work"

VERLINE="CPM-Z8000 Version $ver $date"
DRILINE="Copyright 1982 Digital Research Inc., Zilog Inc."
CONLINE="Copyright $year OpenCoherent contributors"

fail=0
bad() {
	echo "FAIL [$1] $2"
	fail=`expr $fail + 1`
}

# 1. the version line, version AND date, exactly as the build spelled them
grep -qxF "$VERLINE" "$work" ||
	bad version "no line reading \`$VERLINE' -- got: `grep -i 'CPM-Z8000 Version' \"$work\" | head -1`"

# 2. the 1982 attribution.  This is the authentic DRI/Zilog copyright and
#    is not ours to reword or drop; it has to survive every change to the
#    lines around it.
grep -qxF "$DRILINE" "$work" ||
	bad dri "the 1982 Digital Research/Zilog line is gone"

# 3. the contributors' line, with the year the build derived
grep -qxF "$CONLINE" "$work" ||
	bad contributors "no line reading \`$CONLINE' -- got: `grep 'OpenCoherent' \"$work\" | head -1`"

# 4. the order: version, then the 1982 attribution, then the contributors.
#    Checked by line number so a build that printed all three in the wrong
#    order -- or printed one of them somewhere else entirely -- fails.
n1=`grep -nxF "$VERLINE" "$work" | head -1 | cut -d: -f1`
n2=`grep -nxF "$DRILINE" "$work" | head -1 | cut -d: -f1`
n3=`grep -nxF "$CONLINE" "$work" | head -1 | cut -d: -f1`
if [ -n "$n1" ] && [ -n "$n2" ] && [ -n "$n3" ]; then
	[ "$n2" -eq `expr $n1 + 1` ] && [ "$n3" -eq `expr $n2 + 1` ] ||
		bad order "the three lines are at $n1, $n2, $n3, not consecutive and in that order"
fi

# 5. nothing anywhere in the boot still identifies the system as the 1983
#    build.  This is the assertion that would catch a second, forgotten
#    copy of the banner -- in the BIOS, in the CCP, in a stale cpm.sys the
#    patch step failed to install.
if grep -q 'Version 1\.2 03/14/83' "$work"; then
	bad stale "the 1983 identification is still being printed"
fi

if [ $fail -ne 0 ]; then
	echo "bannerchk: FAIL -- $fail assertion(s)"
	exit 1
fi
echo "bannerchk: PASS -- $VERLINE / $DRILINE / $CONLINE"
exit 0
