#!/bin/sh
# Copyright (c) 2026 Kevin Dedon.
# SPDX-License-Identifier: MIT

#
# appchk.sh -- the src/app binary on the disk was built, and built the
# same as the reference copy.
#
#	sh tests/appchk.sh BUILDLOG ARTDIR REFDIR NAME.Z8K...
#
# Run from the repository root.  BUILDLOG is the transcript of a session
# that erased each NAME.Z8K on drive A: and then rebuilt it there with
# ZCC and LD8K; ARTDIR is the directory that session's cpma partition was
# extracted into afterwards.  REFDIR holds the copies to cmp against --
# build/app, what `make apps' built -- or is `-' for no cmp, which is how
# `make apps' itself checks the build it has just made.
#
# The cmp is a reproducibility check, and the erase is what gives it its
# teeth: a compile that fails still leaves whatever .Z8K was there before,
# so without the erase this could compare a staged copy with itself.
# Erased first, a failed build leaves NO file and the cmp cannot be fooled
# -- which is why "missing" is reported here as a build failure rather
# than as a missing artifact.
#
# It is an assertion rather than a hope only because ZCC and LD8K are
# byte-reproducible.  They are, and it is worth saying how that was
# established rather than assumed: the fourteen SDB objects `verify-sdb'
# builds are byte-identical to the ones an earlier, entirely separate
# session produced from the same source.  Reproducibility is a property
# of these two 1984 programs, not something this repository arranges.
#
# The transcript is also scanned for the DRI tools' own diagnostics --
# the same patterns tests/selfhostchk.sh uses, and for the same reason.
#
# Prints one `FAIL [name] ...' line per broken assertion and exits
# nonzero if there were any.

log=$1; shift
art=$1; shift
ref=$1; shift
if [ -z "$log" ] || [ -z "$art" ] || [ -z "$ref" ] || [ $# -eq 0 ]; then
	echo "usage: $0 BUILDLOG ARTDIR REFDIR NAME.Z8K..." >&2
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
	src=$ref/$n
	if [ "$ref" != - ] && [ ! -f "$src" ]; then
		bad source "$src is missing: nothing to compare against"
		continue
	fi
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
	if [ "$ref" = - ]; then
		echo "  ok  $n -- built on the machine"
	elif cmp -s "$art/$n" "$src"; then
		echo "  ok  $n -- rebuilt on the machine, byte-identical to $src"
	else
		bad cmp "$n rebuilt on the machine differs from $src"
		cmp "$art/$n" "$src" 2>&1 | sed 's/^/       /'
		echo "       The same source built twice came out different: ZCC"
		echo "       and LD8K are not reproducible on this build."
	fi
done

if [ $fail -ne 0 ]; then
	echo "appchk: FAIL -- $fail assertion(s)"
	exit 1
fi
echo "appchk: PASS -- every binary listed was built from its own source"
echo "        on the target"
exit 0
