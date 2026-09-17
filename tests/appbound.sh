#!/bin/sh
# Copyright (c) 2026 Kevin Dedon.
# SPDX-License-Identifier: MIT

# appbound.sh -- drive the src/app/ tools, compiled FOR THE HOST under
# -fsanitize=address, against tests/appbound.py's malformed inputs.
#
# WHY ON THE HOST, when verify-a3 and verify-sdb build and run these same
# programs on the real machine: because THE RETURN CODE IS NOT THE OBJECT.
# Every defect here is a write past the end (or, for IEX.C, one byte before
# the start) of a buffer, and on the machine that write lands in whatever
# happens to be next in the TPA and the command still reports success.  The
# only way to assert "the memory past the array is intact" is to have the
# compiler watching the array, so each program is built with its arrays
# instrumented and an out-of-bounds write ABORTS the run: the abort is the
# assertion, exactly as in tests/xouttest.c and the -fsanitize=address half
# of verify-shim.
#
# Three of the nine checks are not about memory at all:
#   * noeof.hex is about TERMINATION -- FROMHEX.C's semicolon search never
#     tested EOF, and a hang has no exit status, so `timeout' supplies one.
#   * impnonl is about DATA -- IEX.C removed the last byte of every value
#     whether or not it was a newline, so a file with no final newline
#     imported 1000 as 100 and said "[ 1 imported ]".  A return code cannot
#     see that; the table SDB prints back can.
#   * the four `good' fixtures are the controls.  A bound that refuses
#     well-formed input is not a fix, so each tool is also run on input at
#     exactly its limit and its output is checked.
#
# Usage: appbound.sh <dir>   -- <dir> holds both the binaries and fixtures.

# No `set -e': every check runs, and the count at the end is the verdict.
D="$1"
test -n "$D" || { echo "appbound.sh: usage: appbound.sh <dir>"; exit 2; }
cd "$D"

fails=0

fail() {
	echo "appbound: FAIL -- $1"
	fails=`expr $fails + 1`
}

# san <log> <what> -- an AddressSanitizer report in the log IS the
# out-of-bounds access, and rc 124 from timeout IS the hang.
san() {
	if grep -q 'ERROR: AddressSanitizer' "$1" 2>/dev/null; then
		fail "$2: an out-of-bounds access ($(grep -m1 -o \
			'AddressSanitizer: [a-z-]*' "$1"), $(grep -m1 -o \
			'in [a-z_]* [a-z/.]*:[0-9]*' "$1" | head -1))"
	fi
}

# ---- (a) FROMHEX: P1 #17 -------------------------------------------------

timeout 20 ./fromhex noeof.hex out.bin > a1.log 2>&1 && rc=0 || rc=$?
test "$rc" != 124 \
	|| fail "FROMHEX did not terminate on input with no semicolon in it:\
 the EOF-less search at FROMHEX.C's get_hex() is still unbounded"
san a1.log "FROMHEX on a file with no semicolon"

timeout 20 ./fromhex big.hex out.bin > a2.log 2>&1 && rc=0 || rc=$?
san a2.log "FROMHEX on a 64-byte record in a 32-byte buffer"
test "$rc" = 3 \
	|| fail "FROMHEX must refuse a record longer than its 32-byte buffer\
 with its own rc 3 (got $rc)"

timeout 20 ./fromhex good.hex out.bin > a3.log 2>&1 && rc=0 || rc=$?
san a3.log "FROMHEX on a full 32-byte record"
test "$rc" = 0 \
	|| fail "FROMHEX must still accept a full 32-byte record (rc $rc)"
cmp -s out.bin good.bin \
	|| fail "FROMHEX must still decode a full 32-byte record correctly"

# ---- (c) SORTFL, (d) KILLDU: P1 #19 ------------------------------------

for t in sortfl killdu; do
	timeout 20 ./$t < long.txt > c-$t.log 2>&1 || true
	san c-$t.log "$t on a 200-character line (128-byte buffer)"
done

timeout 20 ./sortfl < many.txt > c-many.log 2>&1 || true
san c-many.log "SORTFL on 1,001 lines (1,000-entry pointer array)"

timeout 20 ./sortfl < sort.txt > c-sort.out 2>&1 || true
printf 'apple\napple\nbanana\ncherry\n' > c-sort.want
cmp -s c-sort.out c-sort.want \
	|| fail "SORTFL must still sort ordinary input"

timeout 20 ./killdu < c-sort.want > d-killdu.out 2>&1 || true
printf 'apple\nbanana\ncherry\n' > d-killdu.want
cmp -s d-killdu.out d-killdu.want \
	|| fail "KILLDU must still drop adjacent duplicates"

# ---- the SDB legs ------------------------------------------------------
#
# Each SDB run gets its own copy of the relation file it names, because a
# run that writes to one must not be read by the next.

sdbrun() {	# sdbrun <session> <relation-file> <relation-name>
	rm -f "$3.sdb"
	cp "$2" "$3.sdb"
	timeout 60 ./sdb < "$1" > "$1.log" 2>&1 && rc=0 || rc=$?
	test "$rc" != 124 \
		|| fail "SDB did not terminate on $1"
	san "$1.log" "SDB on $1"
}

# ---- (b) CMD.C get_aname(): P1 #18 ------------------------------------

printf 'print using "long.frm" * from b1 ;\nexit\n' > b1.in
sdbrun b1.in good.rel b1
printf 'print using "noterm.frm" * from b2 ;\nexit\n' > b2.in
sdbrun b2.in good.rel b2
printf 'print using "good.frm" * from b3 ;\nexit\n' > b3.in
sdbrun b3.in good.rel b3
grep -q 'SMITH in ENG' b3.in.log \
	|| fail "a form whose attribute names fit must still be substituted"

# ---- (e) INT.C db_xpush(): the P2 interpreter-stack item ---------------

sdbrun deep.in deep.rel deep
sdbrun shallow.in deep.rel deep
grep -q '2500' shallow.in.log \
	|| fail "an ordinary where clause must still select its tuples"

# ---- (f) IO.C's trust in the on-disk header: the P2 IO.C item ----------

for r in small wide zero short; do
	sdbrun $r.in $r.rel $r
done
sdbrun good.in good.rel good
grep -q 'SMITH' good.in.log \
	|| fail "a relation with an honest header must still be printable"

# ---- (g) IEX.C db_import(): the P2 IEX.C item -------------------------

for r in nonl nul wide good; do
	sdbrun imp$r.in imp.rel imp$r
done
grep -q '1000' impnonl.in.log \
	|| fail "IMPORT of a file with no final newline must keep the last\
 byte of the last value: 1000 was imported as 100"
grep -q '1000' impgood.in.log \
	|| fail "IMPORT of an ordinary file must still work"

# ---- verdict ----------------------------------------------------------

if test $fails != 0; then
	echo "appbound: $fails check(s) failed"
	exit 1
fi
echo "appbound: PASS -- FROMHEX terminates on a file with no semicolon and"
echo "          refuses an oversized record; SORTFL and KILLDU survive a"
echo "          200-character line and 1,001 lines; SDB survives a form"
echo "          with an overlong and an unterminated attribute name, a"
echo "          31-deep where clause, four malformed relation headers and"
echo "          three malformed import files -- with no out-of-bounds"
echo "          access anywhere, and every well-formed control still"
echo "          producing the right answer."
exit 0
