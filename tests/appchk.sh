#!/bin/sh
# Copyright (c) 2026 Kevin Dedon.
# SPDX-License-Identifier: MIT

#
# appchk.sh -- the src/app binary on the disk was built there, from the
# source on the disk.
#
#	sh tests/appchk.sh BUILDLOG ARTDIR NAME.Z8K...
#
# Run from the repository root.  BUILDLOG is the transcript of a session
# that erased each NAME.Z8K on drive A: and then rebuilt it there with
# ZCC and LD8K; ARTDIR is the directory that session's cpma partition was
# extracted into afterwards.
#
# The erase is what gives this its teeth: a compile that fails still
# leaves whatever .Z8K was there before -- the host-built copy `all'
# staged -- and the caller would go on to run that.  Erased first, a
# failed build leaves NO file, which is why "missing" is reported here as
# a build failure rather than as a missing artifact.  Whether what was
# built works is the caller's check.
#
# The transcript is also scanned for the DRI tools' own diagnostics --
# the same patterns tests/selfhostchk.sh uses, and for the same reason.
#
# Prints one `FAIL [name] ...' line per broken assertion and exits
# nonzero if there were any.

log=$1; shift
art=$1; shift
if [ -z "$log" ] || [ -z "$art" ] || [ $# -eq 0 ]; then
	echo "usage: $0 BUILDLOG ARTDIR NAME.Z8K..." >&2
	exit 2
fi

fail=0
bad() {
	echo "FAIL [$1] $2"
	fail=`expr $fail + 1`
}

work=`mktemp`
trap 'rm -f "$work"' 0

# The DRI tools' own diagnostic texts.  tests/selfhostchk.sh has the
# list of format strings these come out of; the patterns here are
# narrower than its, because ZCC1 ends every compile it had anything to
# say about with "pass1 had 0 errors, N warnings" -- and these programs
# do warn.  A bare `error' would match that line and fail every run.  So
# the count is matched with a nonzero digit in front of it, and the rest
# are texts a tool only ever prints when it is giving up.
DIAG="fatal error|[1-9][0-9]* errors|undefined variable|undefined symbols\
|illegal|[Cc]an(not|'t) (open|create|append)|No match|unexpected EOF"

if [ ! -s "$log" ]; then
	bad build "$log is missing or empty: the boot produced nothing"
else
	tr -d '\r' < "$log" > "$work"
	if grep -qE "$DIAG" "$work"; then
		bad build "the tool reported: `grep -m1 -E \"$DIAG\" \"$work\"`"
	fi
fi

for n in "$@"; do
	if [ ! -s "$art/$n" ]; then
		bad build "$art/$n is missing or empty: the session erased it"
		echo "     and did not rebuild it, so the compile or the link failed."
		continue
	fi
	# EE03 is a linked segmented program (the same magic verify-arx and
	# selfhostchk read); anything else means LD8K wrote something other
	# than a program.
	m=`od -An -t x1 -N 2 "$art/$n" | tr -d ' \n'`
	if [ "$m" != "ee03" ]; then
		bad kind "$art/$n starts $m, not ee03"
		continue
	fi
	echo "  ok  $n -- built on the machine"
done

if [ $fail -ne 0 ]; then
	echo "appchk: FAIL -- $fail assertion(s)"
	exit 1
fi
echo "appchk: PASS -- every binary listed was built from its own source"
echo "        on the target"
exit 0
