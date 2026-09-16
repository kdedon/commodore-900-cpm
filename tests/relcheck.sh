#!/bin/sh
# Usage: relcheck.sh DEVDIR RELDIR NAME...

set -eu

DEV=$1; shift
REL=$1; shift

[ -d "$DEV" ] || { echo "relcheck: no such dir: $DEV" >&2; exit 1; }
[ -d "$REL" ] || { echo "relcheck: no such dir: $REL" >&2; exit 1; }

shipped=''
dead=''
for n in "$@"; do
	[ -e "$REL/$n" ] && shipped="$shipped $n"
	[ -e "$DEV/$n" ] || dead="$dead $n"
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
[ "$rc" -eq 0 ] || exit 1

