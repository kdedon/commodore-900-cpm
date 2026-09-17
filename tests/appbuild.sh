#!/bin/sh
# Copyright (c) 2026 Kevin Dedon.
# SPDX-License-Identifier: MIT

#
# appbuild.sh -- build one src/app program ON THE MACHINE, out of the C
# that is on the same disk, with the machine's own ZCC and LD8K.
#
#	sh tests/appbuild.sh IMAGE LOG OUT.Z8K NAME...
#
# Run from the repository root.  IMAGE is a bootable medium built by
# tools/mkcpmdisk.py whose drive A: carries the source; LOG is where the
# concatenated transcripts go; OUT.Z8K is the program to produce; each
# NAME is a source file without its .C.  The image is modified in place --
# that is the point, the caller extracts the partition afterwards.
#
# ONE COLD BOOT PER COMMAND, which is what this script exists to arrange.
# The obvious thing is one boot with every command in a single --input
# script, and it works most of the time, which is worse than not working.
# ZCC is a driver: it chains ZCC1, ZCC2 and ZCC3 through the CCP, and each
# chained line is echoed after an `A>'.  The emulator's feeder paces bytes
# on "a prompt printed, the console quiet, or the guest spinning on the
# receiver" -- and a compiler pass prints an `A>' it is not offering, and
# goes quiet for seconds at a time in the middle of a big file.  So queued
# bytes get handed over and eaten by a program that is not reading them.
#
# Padding the script with blank lines absorbs that, and it survived three
# full runs of `make verify-sdb'.  On the fourth, with the machine loaded,
# ZCC2 and ZCC3 ran for 0.3 seconds on the first two files of fourteen and
# left 128-byte stubs behind; the link then failed on _db_pars, _db_comp
# and _db_fcod, and the transcript showed nothing wrong -- every command
# echoed correctly.  A verification target that fails that way is worse
# than no target at all, because the failure looks like the thing it is
# supposed to be testing.
#
# So: one command, one cold boot, no queued bytes to misdeliver.  This is
# what `selfhost' does and for the same reason ("CCP releases one line at a
# time, so one cold boot per session").  Fourteen extra boots cost about
# six emulated seconds against eight minutes of compiling.

set -u
: ${EMU:=$(sh tools/deps.sh emu)}
: ${EMUIDLE:=--stop-on=idle}
# Every boot here is one command that ends back at the CCP prompt, so the
# run is over the moment the prompt returns -- but the idle channel only
# says so after 40,000,000 instructions of silence, and this script boots
# once per source file.  ENDWORD is a command the CCP cannot find: it
# echoes it back with a `?' appended (src/ccp/ccp.c echo_cmd), and that
# echo is the mark.  The `?' is what makes it safe -- the console echo of
# the typed line is the word alone.  Nothing is added to the image; the
# word exists only in the two flags below, and --max still bounds the run.
: ${ENDWORD:=ZZEND}
# Per BOOT, not per session.  The biggest single file here, SDB's CMD.C at
# 860 lines, is about 51 seconds of emulated time through ZCC's three
# passes -- well past the 600,000,000 the rest of the suite uses, because
# nothing else in the suite compiles anything this size.
: ${EMUMAX:=2000000000}

img=$1; log=$2; out=$3; shift 3
if [ -z "${1:-}" ]; then
	echo "usage: $0 IMAGE LOG OUT.Z8K NAME..." >&2
	exit 2
fi

sh tools/deps.sh -n emu "$EMU" || exit 1

here=`pwd`
absimg=$here/$img
abslog=$here/$log
raw=`mktemp`
trap 'rm -f "$raw"' 0

# boot INPUT -- one cold boot running one command line, appended to LOG.
# The emulator's exit status is checked here rather than left to a
# pipeline, for the reason tests/verify.mk gives at $(EMUSTAT).
boot() {
	( cd "$EMU/bin" && ./c900 --disk="$absimg" \
		--input="$1$ENDWORD\r" --stop-mark="$ENDWORD?" \
		--max=$EMUMAX $EMUIDLE 2>/dev/null ) > "$raw"
	st=$?
	python3 tests/tstamp.py < "$raw" >> "$abslog"
	if [ $st -ne 0 ]; then
		echo "appbuild: the emulator exited $st on: $1" >&2
		echo "          what is in $log is not a complete session." >&2
		return 1
	fi
	return 0
}

: > "$abslog"

# Erase the target first.  A compile or link that fails leaves whatever
# .Z8K was there before, and the caller's cmp would then be comparing a
# staged copy with itself and passing.
boot "ERA $out\r" || exit 1

objs=
for n in "$@"; do
	boot "ZCC $n.C\r" || exit 1
	objs="$objs $n.O"
done

# Link order is upstream's own: STARTUP.O, then the objects as
# `sdbas/SDBLNK.SUB' asks for them (`d:s.o *.o'), then LIBCPM.A.  LD8K
# lays modules down in command-line order, so the order IS the binary and
# the caller's cmp will notice if it changes.  For SDB the line is 118
# bytes, inside the CCP's 128-byte buffer.
boot "LD8K -O $out STARTUP.O$objs LIBCPM.A\r" || exit 1

exit 0
