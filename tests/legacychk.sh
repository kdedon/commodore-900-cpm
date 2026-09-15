#!/bin/sh
#
# legacychk.sh -- row 11 ("stock CP/M-8000 1.3 binaries keep running").
#
#   sh tests/legacychk.sh LOG FSDIR
#
# LOG is the verify-legacy transcript, FSDIR the cpma partition extracted
#
#     read back out of its hex dump, not pinned here -- see LIT below)
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
grep -q '^A: RW, Free Space: ' "$work" ||
	bad stat "STAT printed no drive free-space line"
grep -q 'A:LEGCOPY .TXT' "$work" ||
	bad stat "STAT's own row did not name A:LEGCOPY.TXT"

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

record() {
	_cmd=$2
	if ! grep -q "^A>$_cmd\$" "$work"; then
		bad "$1" "\`$_cmd' was not attempted -- the session did not reach it"
		return
	fi
	_vec=`sed -n "/^A>$_cmd\$/,/^A>/{/TRAP vec=/p}" "$work" | head -1 |
		sed -n 's/.*\(TRAP vec=[0-9A-Fa-f]*\).*/\1/p'`
	if [ -n "$_vec" ]; then
	else
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
exit 0
