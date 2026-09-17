#!/bin/sh
# Copyright (c) 2026 Kevin Dedon.
# SPDX-License-Identifier: MIT

#
# selfhostchk.sh -- the assertions on the self-host chain.
#
#   sh tests/selfhostchk.sh CCLOG LDLOG RUNLOG ARTDIR
#
# Run from the repository root, like tests/bannerchk.sh.  CCLOG/LDLOG/RUNLOG are
# the three cold-boot transcripts `make selfhost' produces (ZCC, LD8K,
# HELLO2) and ARTDIR is the directory the cpma partition was extracted
# into afterwards.
#
# The chain is: HELLO.C on drive A: -> ZCC (which chains ZCC1, ZCC2, ZCC3
# and ASZ8K) -> HELLO.O -> LD8K with STARTUP.O and LIBCPM.A -> HELLO2.Z8K
# -> the program runs.  Every stage is a stock 1984 Digital Research
# binary executing on the port's own BDOS, so a failure anywhere is a
# failure of this system, not of the toolchain.
#
# There is no reference build to diff the artifacts against: nothing in
# this tree ships a DRI-built HELLO.O or HELLO2.Z8K, and one produced by
# the host cross-compiler would be a different compiler's output, so a
# diff would be a difference by construction.  What can be checked, and
# is, is that each artifact is the KIND of file its producer is supposed
# to emit (the CP/M-8000 magic word) and that the one thing the source
# says must survive the whole chain -- the string literal in HELLO.C --
# is present in the object, in the linked program, and finally on the
# console.  That is a chain from the source to the running program with
# no reference file in it.
#
# The two toolchain transcripts are also scanned for diagnostics.  The
# patterns are the DRI tools' own message texts (strings in ZCC1.Z8K and
# LD8K.Z8K: "%d:fatal error: %s", "undefined variable '%s'", "illegal
# ...", "%d undefined symbols:", "Cannot open %s"), because a compile
# that fails still leaves the previous HELLO.O on the disk and a link
# that fails still leaves the previous HELLO2.Z8K -- so without this the
# run in step three could pass on artifacts from an earlier session.
#
# Prints one `FAIL [name] ...' line per broken assertion and exits
# nonzero if there were any.

cclog=$1; ldlog=$2; runlog=$3; art=$4
if [ $# -ne 4 ]; then
	echo "usage: $0 CCLOG LDLOG RUNLOG ARTDIR" >&2
	exit 2
fi

fail=0
bad() {
	echo "FAIL [$1] $2"
	fail=`expr $fail + 1`
}

work=`mktemp`
trap 'rm -f "$work"' 0

# The DRI tools' own diagnostic texts (see the note above).
DIAG='error|undefined|illegal|[Cc]annot (open|create|append)|No match'

# clean NAME LOG -- the transcript exists, and holds no diagnostic
clean() {
	if [ ! -s "$2" ]; then
		bad "$1" "$2 is missing or empty: the boot produced nothing"
		return
	fi
	tr -d '\r' < "$2" > "$work"
	if grep -qE "$DIAG" "$work"; then
		bad "$1" "the tool reported: `grep -m1 -E \"$DIAG\" \"$work\"`"
	fi
}

# has NAME LOG TEXT
has() {
	tr -d '\r' < "$2" > "$work"
	grep -qF "$3" "$work" ||
		bad "$1" "\`$3' is not in $2"
}

# ---- 1: the compiler.  The driver signs on, then chains its three
# passes by name -- each one a separate program load through the CCP, so
# a pass that failed to load leaves its command line without a successor.
clean compile "$cclog"
has compile "$cclog" "Zilog CP/M-Z8000 C Compiler"
for pass in ZCC1 ZCC2 ZCC3; do
	has compile "$cclog" "A>$pass "
done

# ---- 2: the linker
clean link "$ldlog"
has link "$ldlog" "CP/M-Z8000 Linker"

# ---- 3: what the source says, all the way through.  The literal is read
# out of HELLO.C rather than spelled here, so editing the test program is
# not a failure.
LIT=`sed -n 's/.*printf("\([^"]*\)\\\\n");.*/\1/p' build/diska/HELLO.C | head -1`
if [ -z "$LIT" ]; then
	bad source "no printf() literal found in build/diska/HELLO.C"
else
	has run "$runlog" "$LIT"
	for f in HELLO.O HELLO2.Z8K; do
		if [ ! -s "$art/$f" ]; then
			bad artifact "$art/$f is missing or empty: the chain wrote nothing"
		elif ! grep -aqF "$LIT" "$art/$f"; then
			bad artifact "$art/$f does not contain the literal from HELLO.C"
		fi
	done
fi

# ---- and each artifact is the kind of file its producer emits: EE02 is
# an unlinked relocatable object, EE03 a linked segmented program
# (../docs, and the same magic verify-arx reads out of XDUMP).
magic() {
	[ -s "$2" ] || return 0			# already reported above
	_m=`od -An -t x1 -N 2 "$2" | tr -d ' \n'`
	[ "$_m" = "$3" ] ||
		bad "$1" "$2 starts $_m, not $3"
}
magic object "$art/HELLO.O" ee02
magic program "$art/HELLO2.Z8K" ee03

if [ $fail -ne 0 ]; then
	echo "selfhostchk: FAIL -- $fail assertion(s)"
	exit 1
fi
echo "selfhostchk: PASS -- compiled, linked and ran on target; the literal"
echo "             in HELLO.C reached the object, the program and the console"
exit 0
