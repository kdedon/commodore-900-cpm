#!/bin/sh
#
# concpad.sh -- PIN verify-concerr's A: DIRECTORY LAYOUT.
#
# verify-concerr only tests anything when CONCTGT.TXT's two directory
# entries land in DIFFERENT directory records.  That is what makes the
# nested dirscan filero() runs (src/bdos/bdosmisc.c) move the directory
# buffer under the delete() that asked, and it is the whole bug (H7).
# When both entries share a record the run passes without exercising it.
#
# Where they land is arithmetic, not luck.  mkcpmfs --initdir puts an SFCB
# in slot 3 of every directory record, so each record holds three ordinary
# entries; the packed files take the first N of those, the directory label
# takes one more, and CONCTGT.TXT's two entries take the next two free
# slots.  Those two straddle a record boundary exactly when
#
#	(N + 1) mod 3 == 2	i.e.	N mod 3 == 1
#
# and N moves every time a file is added to or removed from the dev disk --
# which is why this target passed on main and failed the moment Track A
# staged src/app (H2: 97 and 124 entries fail, 95, 96 and 98 pass).
#
# So the layout is pinned rather than inherited: this script packs the
# staging directory once to learn N, then adds up to two one-entry filler
# files to bring N mod 3 to 1.  tests/concfree.py checks the result on the
# finished image, and verify-concerr fails as a TEST if the straddle is not
# there -- a passing run that tested nothing is the failure mode to guard
# against here, not a broken build.
#
# usage: concpad.sh <staging dir> <blocks> <label> <label mode>

set -e
D=$1
BLKS=$2
LABEL=$3
LBLMODE=$4
[ -n "$LBLMODE" ] || { echo "usage: concpad.sh <dir> <blocks> <label> <mode>" >&2; exit 1; }

rm -f "$D"/CONCPAD?.TXT
mkdir -p build/obj
n=`python3 tools/mkcpmfs.py --initdir --label "$LABEL" --label-mode "$LBLMODE" \
	build/obj/concpad.img "$BLKS" "$D" \
	| sed -n 's/.*files, \([0-9][0-9]*\) dir entries.*/\1/p'`
[ -n "$n" ] || { echo "concpad: mkcpmfs printed no entry count" >&2; exit 1; }

k=$(( (4 - n % 3) % 3 ))
i=0
while [ $i -lt $k ]; do
	echo "concerr layout filler -- see tests/concpad.sh" > "$D/CONCPAD$i.TXT"
	i=$(( i + 1 ))
done
echo "concpad: $n entries + $k filler = $(( n + k )), straddling (mod 3 == 1)"
