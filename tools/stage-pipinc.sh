#!/bin/sh
# Copyright (c) 2026 Kevin Dedon.
# SPDX-License-Identifier: MIT

# stage-pipinc.sh -- host-side compile headers for src/cmd/pip.c.
#
# pip.c is Digital Research's PIP.C (Z8000/CP/M-8000 dev-pack source),
# kept in src/ because we modify and build it; see src/cmd/pip.c's own
# banner for the one-line fix.  It #includes "portab.h", "bdos.h",
# "setjmp.h" and "basepage.h" -- DRI's own header set, not our src/lib/
# cpm.h.  The vendor drop (vendor/z8001mb/cpm8k/packages/base/) carries
# these 8.3-truncated (basepa.h) and CP/M-EOF-terminated (a trailing ^Z
# byte the host cpp chokes on), and never declares the "_base" basepage
# pointer PIP.C reads -- DRI expected some other header in the original
# kit to do that.  vendor/SOURCES holds the drop unmodified and asks for
# a tools/ step instead, so this script copies those four headers verbatim
# up to the ^Z, renames basepa.h to basepage.h, and appends the missing
# "extern struct b_page *_base;" (the pointer our own src/lib/cstart.c
# sets at startup, same base-page layout as src/lib/cpm.h's struct bpage).
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

# Widen jmp_buf from ten ints to twelve.  DRI's setjmp.h sizes it for
# Zilog's own runtime -- "the eight safe registers and a segmented return
# address" -- which recovered the stack pointer from the frame pointer.
# We do not link that runtime: pip.c is compiled by this project's Z8001
# code generator, whose model is R6-R12 callee-saved, R13 frame pointer,
# RR14 segmented SP (toolchain src/cc/h/z8001/mch.h:112), and whose own
# library saves the return PC pair plus r6..r15 -- twelve words
# (toolchain src/libc/gen/setjmp.s).  src/cmd/pipjmp.s implements that
# contract, so the buffer PIP declares has to be that wide or the last
# two words of it land in whatever follows main_stack.
tmp="$OUT/.setjmp.h.tmp"
awk '
	/typedef[ \t]+int[ \t]+jmp_buf\[10\];/ {
		print "/* [12], not DRI\x27s [10]: this port links its own Z8001 runtime, whose"
		print "   setjmp saves the return PC pair plus r6..r15.  See tools/stage-pipinc.sh"
		print "   and src/cmd/pipjmp.s. */"
		sub(/jmp_buf\[10\]/, "jmp_buf[12]")
		widened = 1
	}
	{ print }
	END { if (!widened) exit 1 }
' "$OUT/setjmp.h" > "$tmp" ||
	echo "stage-pipinc: WARNING -- setjmp.h no longer declares jmp_buf[10]; staged copy left as the vendor wrote it" >&2
if ! cmp -s -- "$tmp" "$OUT/setjmp.h"; then
	mv -- "$tmp" "$OUT/setjmp.h"
else
	rm -f -- "$tmp"
fi

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
