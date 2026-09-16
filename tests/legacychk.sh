#!/bin/sh
#
# legacychk.sh -- row 11 ("stock CP/M-8000 1.3 binaries keep running").
#
#   sh tests/legacychk.sh LOG FSDIR
#
# LOG is the verify-legacy transcript, FSDIR the cpma partition extracted
# back out afterwards (tools/mkcpmfs.py --extract).  Nine programs are
# checked.  SEVEN of them are the 1984 Digital Research binaries from
# vendor/z8001mb/cpm8k/packages/base/, unmodified, which is what this row
# is about.  TWO -- PIP and STAT -- were stock when this script was
# written and are not any more: both are now built here from DRI's own
# source and staged over the vendor .Z8K (Makefile, $(UPIP)/$(USTAT)).
# They stay in this session because they share the disk and the fixture
# with the other seven and cost nothing extra to exercise, but their
# assertions are about what they DO, never about which binary answered:
#
#   working, asserted strictly  -- PIP (host-side byte identity of the
#     copy), STAT (free-space line, its own file row, and a Total: line
#     that agrees with the per-file column), DUMP (the HELLO.C source
#     read back out of its hex dump, not pinned here -- see LIT below)
#   ASZ8K, XCON, XDUMP, AR8K, NMZ8K, SIZEZ8K -- STRICT since 2026-09-02.
#     These six were recorded KNOWN-FAILING on a privileged-instruction
#     trap: the split-I/D fast path addressed the SC frame backwards
#     (base and index the wrong way round in splitfast.s), so
#     every saved-register access read and wrote through dispatch
#     scratch, and each program eventually executed its own text.  With
#     that fixed all six run, so the RECORD lines are now assertions, as
#     this comment used to ask of whoever fixed it.  A trap here is a
#     regression in the shim and fails the run.
#
# Prints one `FAIL [name] ...' line per broken assertion (exit nonzero on
# any) followed by a single dated RECORD line -- the retention record for
# this row.  There is no service or fetch here: keeping history is a
# person pasting that line into tests/legacy-history.log after a
# milestone run, which is the whole of the retention mechanism.

log=$1; fs=$2
if [ $# -ne 2 ]; then
	echo "usage: $0 LOG FSDIR" >&2
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
bad() { echo "FAIL [$1] $2"; fail=`expr $fail + 1`; }

# ---- PIP: the copy is byte-identical, checked host-side against the
# partition PIP actually wrote, not against the transcript.
if [ ! -s "$fs/LEGCOPY.TXT" ]; then
	bad pip "$fs/LEGCOPY.TXT is missing: PIP did not create it"
elif ! cmp -s "$fs/LEGCOPY.TXT" "$fs/HELLO.C"; then
	bad pip "LEGCOPY.TXT differs from HELLO.C: the copy is not byte-identical"
fi

# ---- STAT: reports the drive it was pointed at.
#
# STAT IS NO LONGER A STOCK BINARY.  src/cmd/stat.c is DRI's own STAT.C
# (cpm8k13) built here and staged over the vendor STAT.Z8K, the same way
# PIP is (Makefile, $(USTAT)/$(UPIP)).  This row's subject is therefore
# STAT's BEHAVIOUR, not the vendor's binary -- the same footing PIP is
# already on above, where the assertion is the copy it produced and not
# a banner.
#
# What used to stand here was `grep CP/M-8000 STAT', an assertion about
# WHICH BINARY was on the disk.  The answer is deliberately no longer
# "the vendor's", and the cpm8k13 source prints no sign-on at all -- its
# only version string is the usage text in values() (src/cmd/stat.c:1185)
# -- so that grep could only ever fail.  Deleting it outright would have
# left the row weaker than before, so it is replaced by a check on the
# very thing that made replacing the binary worth doing: the Total: line.
# DRI declares display()'s kblks and tall as automatics, never
# initialises them, and accumulates into both (src/cmd/stat.c:1687-1698),
# so the totals were whatever the stack held plus the real sum.  With one
# file matched, the "-1k blocks" figure IS that file's own k column by
# construction -- kblks is the sum of exactly those per-file values -- so
# the two must agree.  Before the fix they did not.
grep -q '^A: RW, Free Space: ' "$work" ||
	bad stat "STAT printed no drive free-space line"
grep -q 'A:LEGCOPY .TXT' "$work" ||
	bad stat "STAT's own row did not name A:LEGCOPY.TXT"

filek=`sed -n 's/^ *[0-9][0-9]* *\([0-9][0-9]*\)k .*A:LEGCOPY \.TXT.*/\1/p' \
	"$work" | head -1`
totk=`sed -n 's/^Total:.*, *\([0-9][0-9]*\)-1k blocks).*/\1/p' "$work" | head -1`
if [ -z "$filek" ] || [ -z "$totk" ]; then
	bad stat "STAT printed no per-file k column, or no Total: line to check it against"
elif [ "$filek" != "$totk" ]; then
	bad stat "STAT totalled ${totk}-1k blocks over one file whose own column \
says ${filek}k -- display()'s kblks is summing into an uninitialised automatic again"
fi

# ---- DUMP: known bytes.  The literal is read out of the fixture DUMP
# was pointed at (build/diska/HELLO.C, written by tools/stage-devpack.sh)
# rather than pinned here, so editing the fixture is not a failure.  DUMP
# wraps its ASCII column at 16 bytes/line, so only the first 10 -- one
# line's worth with room to spare -- is looked for.
LIT=`sed -n 's/.*printf("\([^"]*\)\\\\n");.*/\1/p' "$fs/HELLO.C" | head -1`
if [ -z "$LIT" ]; then
	bad dump "no printf() literal found in $fs/HELLO.C"
else
	SNIP=`printf '%s' "$LIT" | cut -c1-10`
	grep -qF "$SNIP" "$work" ||
		bad dump "DUMP's hex dump does not render \`$SNIP'"
fi

# ---- the six that the split-I/D shim carries: they must not trap.
record() {
	_cmd=$2
	if ! grep -q "^A>$_cmd\$" "$work"; then
		bad "$1" "\`$_cmd' was not attempted -- the session did not reach it"
		return
	fi
	_vec=`sed -n "/^A>$_cmd\$/,/^A>/{/TRAP vec=/p}" "$work" | head -1 |
		sed -n 's/.*\(TRAP vec=[0-9A-Fa-f]*\).*/\1/p'`
	if [ -n "$_vec" ]; then
		bad "$1" "\`$_cmd' took a $_vec -- a split-I/D program faulted; \
this is the shim regressing, not a known failure (see the head of this file)"
	else
		echo "RAN $1: $_cmd -- no trap"
	fi
}
record asz8k  'ASZ8K MINI.8KN'
record xcon   'XCON -o MINI.O MINI.OBJ'
record xdump  'XDUMP MINI.O'
record ar8k   'AR8K rv TEST.A MINI.O'
record nmz8k  'NMZ8K STARTUP.O'
record sizez8k 'SIZEZ8K MHELLO.Z8K'

if [ $fail -ne 0 ]; then
	echo "verify-legacy: FAIL -- $fail assertion(s)"
	exit 1
fi
echo "RECORD `date -u +%Y-%m-%dT%H:%MZ` verify-legacy: PASS -- PIP/STAT/DUMP OK; ASZ8K/XCON/XDUMP/AR8K/NMZ8K/SIZEZ8K ran without trapping"
exit 0
