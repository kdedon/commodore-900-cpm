#!/bin/sh
#
# sdbchk.sh -- Gate A: SDB, compiled on the machine, doing database work.
#
#	sh tests/sdbchk.sh RUNLOG ARTDIR
#
# Run from the repository root.  RUNLOG is the transcript of the cold
# boot that ran SDB; ARTDIR is the directory that boot's cpma partition
# was extracted into afterwards.  tests/appchk.sh has already established
# that the SDB.Z8K which produced this transcript is byte-for-byte the
# source in src/app/ -- so what is left to establish is that it WORKS.
#
# import three tuples from SDBIN.TXT, print it twice (once whole and once
# is the file layer, which is why SDB was
# chosen first: creatb() to make EMP.SDB, a 512-byte header write,
# lseek() to a computed tuple offset and a random-record read for every
# tuple the query touches.  There is no terminal handling in any of it.
#
# Two of the assertions below are about the answer being RIGHT rather
# than merely present:
#
#   the WHERE clause must select two tuples of three, and the two it
#   names.  `[ 3 found ]' would mean the predicate matched everything --
#   a table still prints and a count still appears, so counting rows is
#   the only way to tell a working comparison from a broken one.  The
#   comparison itself is MTH.C's: a `num' attribute in SDB is a digit
#   STRING, compared digit by digit by code this target just compiled.
#
#   and the export must come back off the DISK, not off the screen.  SDB
#   right-justifies a num attribute in its field width, so SDBOUT.TXT is
#   not a copy of SDBIN.TXT; it is what those values look like after a
#   round trip through the relation file.  Strip the leading blanks and
#   it has to be the input again, every value, in order.
#
# Prints one `FAIL [name] ...' line per broken assertion and exits
# nonzero if there were any.

log=$1; art=$2
if [ $# -ne 2 ]; then
	echo "usage: $0 RUNLOG ARTDIR" >&2
	exit 2
fi

fail=0
bad() {
	echo "FAIL [$1] $2"
	fail=`expr $fail + 1`
}

work=`mktemp`
trap 'rm -f "$work"' 0

if [ ! -s "$log" ]; then
	echo "FAIL [run] $log is missing or empty: the boot produced nothing"
	echo "sdbchk: FAIL -- 1 assertion(s)"
	exit 1
fi
# tests/tstamp.py prefixes every line with an emulated-time stamp; strip
# it, so a table row starts with the `|' it starts with on the screen.
tr -d '\r' < "$log" | sed 's/^\[ *[0-9.]*\] //' > "$work"

has() {
	# `--': the help text this looks for starts with a dash.
	grep -qF -- "$2" "$work" || bad "$1" "\`$2' is not in $log"
}

# SDB's own sign-on, so a transcript from something else cannot pass.
has start 'SDB - version 2.0'
# `help' is CMD.C's fopen("sdb.hlp") read out to the console -- so this is
# the assertion that SDB.HLP is a RUNTIME file and belongs on the release
# disk beside the program, not a document that could be held back with the
# source.  Without it SDB prints "No online help available".
has help '- create a new relation'
# No statement may have been rejected.
grep -q '\*\* error:' "$work" &&
	bad syntax "SDB reported: `grep -m1 '\*\* error:' \"$work\"`"

has import '[ 3 imported ]'
has all	   '[ 3 found ]'
has where  '[ 2 found ]'
has export '[ 3 exported ]'

# The two rows the WHERE clause selects, and the one it must not.  SMITH
# is legitimately on screen -- the unqualified `print * from emp' printed
# it -- so the exclusion can only be checked inside the SECOND table.
# Everything after the first table's `[ 3 found ]' is that table.
sed -n '/\[ 3 found \]/,$p' "$work" | grep '^|' | grep -v '| name' > "$work.sel"
for who in JONES CLARKE; do
	grep -q "| $who" "$work.sel" ||
		bad where "$who is not in the selection"
done
grep -q '| SMITH' "$work.sel" &&
	bad where "SMITH is in the selection, and its salary is below the bound"
rm -f "$work.sel"

# The relation file SDB says it made.
if [ ! -s "$art/EMP.SDB" ]; then
	bad relation "$art/EMP.SDB is not on the partition: the relation SDB"
	echo "     reported creating did not reach the disk."
else
	# A relation file is a 512-byte header block and then tuples; SDB
	# preallocates the capacity `create' asked for, so it cannot be
	# just the header.
	sz=`wc -c < "$art/EMP.SDB"`
	[ "$sz" -gt 512 ] ||
		bad relation "$art/EMP.SDB is $sz bytes: header only, no tuple space"
fi

# The export, normalised: text ends at the CP/M soft-EOF, num attributes
# come back right-justified, and record padding follows.
if [ ! -s "$art/SDBOUT.TXT" ]; then
	bad export "$art/SDBOUT.TXT is not on the partition"
else
	python3 -c 'import sys; d = open(sys.argv[1], "rb").read(); \
	    i = d.find(b"\x1a"); d = d[:i] if i >= 0 else d; \
	    d = d.replace(b"\r\n", b"\n").replace(b"\r", b"\n"); \
	    sys.stdout.buffer.write(b"".join(l.strip() + b"\n" \
	        for l in d.split(b"\n") if l.strip()))' \
	    "$art/SDBOUT.TXT" > "$work.out"
	tr -d '\r' < src/dist/disk-a/SDBIN.TXT > "$work.in"
	if cmp -s "$work.out" "$work.in"; then
		echo "  ok  SDBOUT.TXT -- every value imported came back out of the"
		echo "      relation file unchanged, in order"
	else
		bad export "what SDB exported is not what was imported"
		diff "$work.in" "$work.out" | sed 's/^/       /'
	fi
	rm -f "$work.out" "$work.in"
fi

if [ $fail -ne 0 ]; then
	echo "sdbchk: FAIL -- $fail assertion(s)"
	exit 1
fi
echo "sdbchk: PASS -- created a relation, imported three tuples, selected"
exit 0
