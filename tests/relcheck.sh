#!/bin/sh
# Copyright (c) 2026 Kevin Dedon.
# SPDX-License-Identifier: MIT

# Verify that release staging is development staging minus exactly the excluded
# names -- nothing more, nothing less, and byte for byte.
#
# Four assertions, because the release image is built by a `cp' loop
# (mk/images.mk) and each of the four ways that loop can go wrong is silent:
#
#   1. every excluded name is ABSENT from release.  A dead exclusion name
#      would otherwise silently ship its renamed program.
#   2. every excluded name EXISTS in development.  A name that matches
#      nothing excludes nothing, so whatever it used to name now ships.
#   3. every name in development that is NOT excluded is PRESENT in release
#      and IDENTICAL to its development original.  This is the assertion the
#      old header claimed and the old code did not make: it walked the
#      exclusion list only, so a file the copy loop skipped, truncated or
#      corrupted reached the release image unnoticed.
#   4. release carries NOTHING that development does not.  A stale file left
#      in the staging directory from an earlier build ships otherwise.
#
# Usage: relcheck.sh DEVDIR RELDIR NAME...

set -eu

DEV=$1; shift
REL=$1; shift

[ -d "$DEV" ] || { echo "relcheck: no such dir: $DEV" >&2; exit 1; }
[ -d "$REL" ] || { echo "relcheck: no such dir: $REL" >&2; exit 1; }

nexcl=$#

shipped=''
dead=''
for n in "$@"; do
	[ -e "$REL/$n" ] && shipped="$shipped $n"
	[ -e "$DEV/$n" ] || dead="$dead $n"
done

# The exclusion set as a padded string, so `case' can match a whole name and
# not a prefix of one.  Built once: the walk below is over every input file.
excl=" $* "

missing=''
differ=''
held=0
kept=0
for f in "$DEV"/*; do
	[ -e "$f" ] || continue		# an unmatched glob is not a file
	b=`basename "$f"`
	case "$excl" in *" $b "*) held=`expr $held + 1`; continue;; esac
	kept=`expr $kept + 1`
	if [ ! -e "$REL/$b" ]; then
		missing="$missing $b"
	elif ! cmp -s "$f" "$REL/$b"; then
		differ="$differ $b"
	fi
done

extra=''
rkept=0
for f in "$REL"/*; do
	[ -e "$f" ] || continue
	b=`basename "$f"`
	rkept=`expr $rkept + 1`
	[ -e "$DEV/$b" ] || extra="$extra $b"
done

rc=0
if [ -n "$shipped" ]; then
	echo "relcheck: FAIL -- the release disk still carries exercisers:" >&2
	printf '  %s\n' $shipped >&2
	rc=1
fi
if [ -n "$dead" ]; then
	echo "relcheck: FAIL -- ATEST names nothing in $DEV:" >&2
	printf '  %s\n' $dead >&2
	echo "relcheck: a dead name excludes nothing, so whatever it used to" >&2
	echo "relcheck: name is now on the release disk.  Renamed, or dropped?" >&2
	rc=1
fi
if [ -n "$missing" ]; then
	echo "relcheck: FAIL -- these are not excluded and did not reach $REL:" >&2
	printf '  %s\n' $missing >&2
	echo "relcheck: the release image would ship without them.  Either the" >&2
	echo "relcheck: staging copy failed or the exclusion list is not the" >&2
	echo "relcheck: only thing deciding what goes in." >&2
	rc=1
fi
if [ -n "$differ" ]; then
	echo "relcheck: FAIL -- these reached $REL but do not match $DEV:" >&2
	printf '  %s\n' $differ >&2
	echo "relcheck: a release program that is not the program that was" >&2
	echo "relcheck: built and tested.  Truncated, stale, or overwritten." >&2
	rc=1
fi
if [ -n "$extra" ]; then
	echo "relcheck: FAIL -- these are in $REL with no original in $DEV:" >&2
	printf '  %s\n' $extra >&2
	echo "relcheck: staging was not clean, so the release carries a file" >&2
	echo "relcheck: from an earlier build." >&2
	rc=1
fi

# The arithmetic the old version printed but never checked.  It cannot fail
# once the five lists above are empty; it is here so that a future change
# which drops one of those walks still cannot make the counts lie.
if [ "$rc" -eq 0 ] && [ "$rkept" -ne "$kept" ]; then
	echo "relcheck: FAIL -- $REL holds $rkept files, but $DEV has $kept" >&2
	echo "relcheck: non-excluded inputs.  The per-file checks above passed," >&2
	echo "relcheck: so one of them is no longer walking every file." >&2
	rc=1
fi

[ "$rc" -eq 0 ] || exit 1

echo "relcheck: OK -- $kept inputs copied byte for byte, $held held back," \
     "$nexcl exclusion names all live, nothing extra in $REL"
