#!/bin/sh
# Copyright (c) 2026 Kevin Dedon.
# SPDX-License-Identifier: MIT
#
# verifyfail.sh -- say what a verify run failed at, at the bottom of the log.
#
#   sh tests/verifyfail.sh [LINES]
#
# The FAIL lines of the run summary, then the tail of each failing target's
# transcript.  LINES defaults to 30.
#
# It reports and nothing more: the verdict was the suite's own exit status,
# and this always exits 0 so it can neither hide that nor add to it.

set -u

RUNDIR=build/verify-run
N=${1:-30}

if [ ! -f $RUNDIR/results ]; then
	echo "=== no verify results: the run failed before any target ran"
	exit 0
fi

fails=`awk '$1 == "FAIL" { print $2 }' $RUNDIR/results`
if [ -z "$fails" ]; then
	echo "=== every target passed: the failure is above, outside the suite"
	exit 0
fi

echo "=== failed: `echo $fails`"

# A target that shares a make with another is logged under the first name on
# its job line, so the log to read is not always the target's own.
logof() {
	if [ -f "$RUNDIR/$1.log" ]; then
		echo "$RUNDIR/$1.log"
	else
		f=`awk -v t="$1" '{ for (i = 1; i <= NF; i++) if ($i == t) { print $1; exit } }' \
			$RUNDIR/jobs 2>/dev/null`
		[ -n "$f" ] && echo "$RUNDIR/$f.log"
	fi
}

for t in $fails; do
	l=`logof $t`
	echo
	echo "--- $t: last $N lines of ${l:-no log}"
	[ -n "$l" ] && [ -f "$l" ] && tail -n "$N" "$l"
done
exit 0
