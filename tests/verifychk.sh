#!/bin/sh
# Copyright (c) 2026 Kevin Dedon.
# SPDX-License-Identifier: MIT

#
# verifychk.sh -- the assertions on the two oldest console sessions.
#
#   sh tests/verifychk.sh verify   build/verify.log
#   sh tests/verifychk.sh reverify build/reverify.log
#
# Run from the repository root, like tests/bannerchk.sh, because it reads the
# sources the sessions exercise (src/cmd/mhello.c, src/cmd/fcopy.c, src/cmd/beep.c)
# and the file drive A: is packed with (src/dist/disk-a/HELLO.TXT).
#
# The two sessions are described by VERIFYIN and REVERIFYIN in the
# Makefile.  What they prove, and so what is checked here:
#
#   verify    a cold boot reaches the CCP; an MWC transient loads, sees
#             its command tail and runs twice; the speaker driver takes
#             the count it was given; a program-level file copy moves the
#             bytes and reports its record count; the error path reports a
#             file that is not there; the copy TYPEs back identical to the
#             file the packer put on the disk; the CCP's own DIR sees the
#             new file; and three DRI utilities -- STAT, PIP, DDT -- still
#             run against this BDOS, with STAT's record arithmetic
#             agreeing with the copy and PIP's copy TYPEing back identical
#             too.  Only DDT is still the 1984 binary: STAT and PIP are
#             built here from DRI's own source and staged over the vendor
#             .Z8K (Makefile, $(USTAT)/$(UPIP)).
#   reverify  a SECOND cold boot of the same image: the two files the
#             first session wrote (one by a transient, one by PIP) are
#             still there and one of them still reads back byte for byte.
#
# Where the text belongs to this tree it is derived from the source that
# prints it rather than pinned here, the way tests/signoncount.py reads the
# banner out of bdosinit(): a reword is then not a failure.  Where it
# belongs to a 1984 Digital Research binary (DDT) it is pinned, because
# nothing in this tree can reword it and its exact wording is part of what
# "the stock utility still runs" means.  STAT's and PIP's text is DRI's
# too, but this tree now compiles it, so what is asserted about those two
# is arithmetic and content -- things a reword cannot break and a
# regression cannot survive.
#
# Prints one `FAIL [name] ...' line per broken assertion, in the shape
# tests/bannerchk.sh and tests/rtctest.c use, and exits nonzero if any.

mode=$1; log=$2
if [ $# -ne 2 ]; then
	echo "usage: $0 verify|reverify LOG" >&2
	exit 2
fi
if [ ! -s "$log" ]; then
	echo "FAIL [transcript] $log is missing or empty: the boot produced nothing"
	exit 1
fi

work=`mktemp`
trap 'rm -f "$work"' 0
tr -d '\r' < "$log" > "$work"

fail=0
bad() {
	echo "FAIL [$1] $2"
	fail=`expr $fail + 1`
}

# want NAME COUNT TEXT -- TEXT must appear exactly COUNT times
want() {
	_n=`grep -cF "$3" "$work"`
	[ "$_n" = "$2" ] ||
		bad "$1" "\`$3' appears $_n times, expected $2"
}

# wantx NAME COUNT TEXT -- the same, but TEXT must be the WHOLE line.
# The console echoes every command, so a program whose output repeats its
# own command line (BEEP) needs the echo excluded: `A>BEEP 2' is not a
# whole line reading `BEEP 2'.
wantx() {
	_n=`grep -cxF "$3" "$work"`
	[ "$_n" = "$2" ] ||
		bad "$1" "\`$3' is a whole line $_n times, expected $2"
}

# ---- the text this tree owns, read out of the programs that print it ----
HELLO=`sed -n 's/.*printstr("\([^"]*\)\$");.*/\1/p' src/cmd/mhello.c | head -1`
[ -n "$HELLO" ] ||
	bad source "no printstr() greeting found in src/cmd/mhello.c"
FCNOPEN=`sed -n 's/.*cputs("\(fcopy: cannot open \)");.*/\1/p' src/cmd/fcopy.c |
	head -1`
[ -n "$FCNOPEN" ] ||
	bad source "no \`cannot open' message found in src/cmd/fcopy.c"
FCCOPIED=`sed -n 's/.*cputs("\(fcopy: copied \)");.*/\1/p' src/cmd/fcopy.c |
	head -1`
FCRECS=`sed -n 's/.*cputs("\( records\)\\\\r\\\\n");.*/\1/p' src/cmd/fcopy.c |
	head -1`
[ -n "$FCCOPIED" ] && [ -n "$FCRECS" ] ||
	bad source "no \`copied N records' message found in src/cmd/fcopy.c"
BEEPPFX=`sed -n 's/.*cputs("\(BEEP \)");.*/\1/p' src/cmd/beep.c | head -1`
[ -n "$BEEPPFX" ] ||
	bad source "no \`BEEP ' message found in src/cmd/beep.c"

# HELLO.TXT is what the packer put on drive A: and what both copies in
# the session are made of, so its own bytes are the reference.  Every
# non-empty line of it must come back the right number of times.
typed() {
	while IFS= read -r l; do
		[ -n "$l" ] || continue
		_n=`grep -cF "$l" "$work"`
		[ "$_n" = "$2" ] ||
			bad "$1" "TYPEd line \`$l' appears $_n times, expected $2"
	done < src/dist/disk-a/HELLO.TXT
}

case "$mode" in
verify)
	# ---- the MWC transient: loaded twice, and it saw its command tail.
	# BETA-1 carries the hyphen the CCP upper-cases around but must not
	# eat, and it is the SECOND argument, so the tail was split as well
	# as delivered.
	want mhello 2 "$HELLO"
	want mhelloarg 1 "  arg 1: ALPHA"
	want mhelloarg 1 "  arg 2: BETA-1"
	# ---- BEEP 2: the program echoes the count it parsed, and exactly
	# two BELs reach the console.  The count is the assertion -- a BEL
	# path that fired once, or once per character, fails here.
	wantx beep 1 "${BEEPPFX}2"
	nbel=`tr -cd '\007' < "$log" | wc -c`
	[ "$nbel" = 2 ] ||
		bad bel "the console saw $nbel BEL characters, expected 2 from \`BEEP 2'"
	# ---- FCOPY: HELLO.TXT is 2 records, and the copy is byte-identical
	# because it TYPEs back the same lines (below).
	want fcopy 1 "${FCCOPIED}2${FCRECS}"
	# ---- and the error path, which is the only thing in the session
	# that proves a failed BDOS open is reported rather than ignored
	want fcopyerr 1 "${FCNOPEN}NOPE.TXT"
	# ---- the CCP's own directory sees the file the transient created
	want dir 1 "COPY2    TXT"
	# ---- twice: once TYPEd from FCOPY's copy, once from PIP's
	typed type 2
	# ---- STAT: its record arithmetic has to agree with the copy -- 2
	# records, one FCB, one 1k block -- and it has to read the DPB well
	# enough to report the drive.
	#
	# STAT is no longer the stock binary (see the header): src/cmd/stat.c
	# is DRI's cpm8k13 STAT.C built here.  Two assertions moved with it.
	# The Bytes column now reads 1k, not the vendor's 2k, because this
	# source prints the file's own rounded-up record content,
	# (rcnt + 7) / 8 (src/cmd/stat.c:1779) -- 2 records is 1k -- where the
	# vendor binary printed the allocated block instead; the Total: line
	# below still carries the allocation, and both numbers are now on
	# screen at once.  And `CP/M-8000 STAT', the vendor sign-on, is gone:
	# this source prints no sign-on at all (its only version string is
	# values()'s usage text, src/cmd/stat.c:1188).  What replaced that
	# assertion is the totals check, which is what the rebuild was for --
	# DRI accumulates into kblks without initialising it
	# (src/cmd/stat.c display()), and over a single file the "-1k blocks"
	# figure is that file's own k column by construction.
	grep -qE '^ +2 +1k +1 Dir RW +A:COPY2 +\.TXT$' "$work" ||
		bad stat "no STAT row reading 2 records / 1k / 1 FCB for A:COPY2.TXT"
	grep -qE '^Total: +[0-9]+k +1 \( +1 file, +1-1k blocks\)$' "$work" ||
		bad stattotal "STAT's Total: line does not read one file in one 1k block; \
display()'s kblks is summing into an uninitialised automatic again"
	grep -q '^A: RW, Free Space: ' "$work" ||
		bad statfree "STAT printed no drive free-space line"
	# ---- PIP (stock DRI) created OUT2.TXT; its contents are covered by
	# the `typed 2' above, which is what makes this more than a no-op
	# ---- DDT (stock DRI): it loads the transient and reports the two
	# segments it will debug in.
	#
	# `debugee start' is map_adr(0, caller data) -- the PHYSICAL address
	# of offset 0 of the space the debugee is loaded into -- so on the
	# Zilog development board, whose TPA is physical 0, it read
	# 00000000, which is what this line asked for and what no C900 can
	# print.  Here that address is the TPA base, c900cfg.h TPABASE.
	# Getting it at all is the assertion: until map_adr's system space
	# codes were fixed (src/bios/bios900.c) DDT copied its own text in
	# place of the memory region table, ran into a TRAP and took the
	# warm boot's CCP load down with it.
	want ddt 1 "Zilog portable debugger"
	want ddtload 1 "debugee start=32000000"
	# and the name off the command tail reached it, which is the whole
	# of the base page the loader handed the program.
	want ddttail 1 "loading 'mhello.z8k' as executable"
	# ---- AND IT GETS PAST THAT NOW.  Slot 4 of the memory region
	# table used to name the TPA, so DDT relocated its own 64 KB on top
	# of the debugee it had just loaded and died in the wreckage; it
	# now names a segment out of the pool (src/bios/bios900.c memtab,
	# src/bdos/proc.c pmrtseg).  The first assertion is the one that
	# matters and it is a NEGATIVE, because which pool segment DDT is
	# given depends on how much RAM the machine has and on what else
	# holds a slot: whatever it is, it must not be the debugee's.
	want ddtseg 0 "debugger segment= 32000000"
	# With a segment of its own DDT reads the file header and walks the
	# whole base page the BDOS loader built for the debugee -- the last
	# line of that walk is the command tail, empty because DDT passed
	# the debugee none.  Getting to it means the load finished.
	want ddtmagic 1 "magic number= EE01"
	want ddtbase 1 "command tail= ''"
	# It still cannot DEBUG.  Having loaded the debugee, DDT patches an
	# SC #0 over the first word of its entry point and transfers to it,
	# which is a breakpoint and is exactly right -- but it never tells
	# this system where its handler is (neither BIOS function 22 nor
	# BDOS function 61 is ever called), because on the Zilog board it
	# was written for the debugger owns the Program Status Area and
	# plants the vector itself.  Here the SC #0 reaches the kernel's
	# fault path with nothing recorded, and the program is killed.
	# That is a second, separate defect and it is not this one.
	;;
reverify)
	# ---- this is a COLD boot, so the system signs on before the session.
	# The version and date are deliberately NOT checked against the
	# build's values here the way `make verify' checks them with
	# tests/bannerchk.sh: reverify boots the image verify left behind,
	# which may have been built on an earlier day, and a check that fails
	# for that reason would be measuring the calendar.  What has to hold
	# is that a CP/M-8000 signed on at all -- otherwise the DIR below
	# would be missing for the wrong reason.
	grep -q '^CPM-Z8000 Version ' "$work" ||
		bad signon "no version line: the system did not sign on"
	grep -qxF 'Copyright 1982 Digital Research Inc., Zilog Inc.' "$work" ||
		bad signon "the 1982 Digital Research/Zilog line is gone"
	grep -q 'OpenCoherent contributors' "$work" ||
		bad signon "the contributors' line is gone"
	# ---- the point of a second cold boot: the files the first session
	# wrote are still on the medium.  COPY2.TXT was written by a
	# transient through the BDOS, OUT2.TXT by PIP.
	want dir 1 "COPY2    TXT"
	want dir 1 "OUT2     TXT"
	# ---- and one of them still reads back byte for byte
	typed type 1
	# ---- the system is still usable: a transient loads and sees its tail
	want mhello 1 "$HELLO"
	want mhelloarg 1 "  arg 1: AGAIN"
	;;
*)
	echo "usage: $0 verify|reverify LOG" >&2
	exit 2
	;;
esac

if [ $fail -ne 0 ]; then
	echo "verifychk: FAIL -- $fail assertion(s) in the $mode session"
	exit 1
fi
echo "verifychk: PASS -- the $mode session"
exit 0
