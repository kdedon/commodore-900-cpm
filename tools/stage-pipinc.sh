#!/bin/sh
# stage-pipinc.sh -- host-side compile headers for src/cmd/pip.c.
#
# pip.c is Digital Research's PIP.C (Z8000/CP/M-8000 dev-pack source),
# kept in src/ because we modify and build it; see src/cmd/pip.c's own
# banner for the one-line fix.  It #includes "portab.h", "bdos.h",
# "setjmp.h" and "basepage.h" -- DRI's own header set, not our src/cmd/
# cpm.h.  The vendor drop (vendor/z8001mb/cpm8k/packages/base/) carries
# these 8.3-truncated (basepa.h) and CP/M-EOF-terminated (a trailing ^Z
# byte the host cpp chokes on), and never declares the "_base" basepage
# pointer PIP.C reads -- DRI expected some other header in the original
# kit to do that.  vendor/SOURCES holds the drop unmodified and asks for
# a tools/ step instead, so this script copies those four headers verbatim
# up to the ^Z, renames basepa.h to basepage.h, and appends the missing
# "extern struct b_page *_base;" (the pointer our own src/cmd/cstart.c
# sets at startup, same base-page layout as src/cmd/cpm.h's struct bpage).
#
# Usage: stage-pipinc.sh basedir outdir

set -eu

BASE=${1:?usage: stage-pipinc.sh basedir outdir}
OUT=${2:?usage: stage-pipinc.sh basedir outdir}

[ -d "$BASE" ] || { echo "stage-pipinc: no such dir: $BASE" >&2; exit 1; }
mkdir -p -- "$OUT"

# striplf SRC DST -- copy up to (not including) the first ^Z byte
striplf() {
	if [ ! -f "$BASE/$1" ]; then
		echo "stage-pipinc: missing source $BASE/$1" >&2
		exit 1
	fi
	tmp="$OUT/.$2.tmp"
	awk 'BEGIN{RS="\x1a"} {printf "%s", $0; exit}' "$BASE/$1" > "$tmp"
	if ! cmp -s -- "$tmp" "$OUT/$2" 2>/dev/null; then
		mv -- "$tmp" "$OUT/$2"
	else
		rm -f -- "$tmp"
	fi
}

striplf portab.h portab.h
striplf bdos.h   bdos.h
striplf setjmp.h setjmp.h
striplf basepa.h basepage.h

# Append the extern _base declaration basepage.h never carries.
tmp="$OUT/.basepage.h.tmp"
awk '
	/^#endif/ && !done { print "extern struct b_page *_base;"; print ""; done=1 }
	{ print }
' "$OUT/basepage.h" > "$tmp"
if ! cmp -s -- "$tmp" "$OUT/basepage.h"; then
	mv -- "$tmp" "$OUT/basepage.h"
else
	rm -f -- "$tmp"
fi

echo "stage-pipinc: $OUT is current"
