#!/bin/sh
# localt.sh -- prove `make cpmlocal' with files this test invents.
#
# The target exists so the operator can put programs of their OWN on a boot
# medium -- programs this project does not ship and may not redistribute.  So
# the test may not use any of them: it writes its own two files, one named
# like a CP/M-80 transient and one like a CP/M-86 transient, and asserts that
# the mechanism carries a name and a byte range through staging unchanged.
# Nothing here needs the operator's files to be present.
#
# Three assertions:
#   1. an output path inside the checkout is REFUSED, and nothing is written;
#   2. the medium's drive A: still holds the development disk (CCP.Z8K);
#   3. both invented files reached drive A: with their names upper-cased by
#      the packer's 8.3 rules and their bytes intact.  CP/M stores whole
#      128-byte records, so a file is compared over its own length -- the
#      packer's zero padding beyond it is the file system's, not a change.
#
# Usage: localt.sh [MAKE]

set -eu

MK=${1:-make}
REPO=$(pwd -P)

tmp=${TMPDIR:-/tmp}/c900-localt.$$
case "$tmp" in "$REPO"/*) echo "localt: refusing: TMPDIR is inside the checkout" >&2; exit 1;; esac
trap 'rm -rf -- "$tmp"' EXIT INT HUP TERM
rm -rf -- "$tmp"
mkdir -p -- "$tmp/extra"

# The two synthetic guests.  Deterministic bytes, one length not a multiple
# of 128 and one that is, so both sides of the record rounding are covered.
python3 - "$tmp/extra" <<'PY'
import os, sys
d = sys.argv[1]
open(os.path.join(d, "localt.com"), "wb").write(
    bytes((i * 37 + 11) & 0xff for i in range(300)))
open(os.path.join(d, "LOCALT.CMD"), "wb").write(
    bytes((i * 91 + 5) & 0xff for i in range(256)))
PY

# ---- 1. the refusal -------------------------------------------------------
bad=build/localt-refused.bin
rm -f -- "$bad"
if $MK cpmlocal LOCALDIR="$tmp/extra" LOCALOUT="$bad" >"$tmp/refuse.log" 2>&1; then
	echo "localt: FAIL -- an output path inside the checkout was accepted" >&2
	echo "localt: the licence boundary is not being enforced." >&2
	exit 1
fi
if [ -e "$bad" ]; then
	echo "localt: FAIL -- the refused run still wrote $bad" >&2
	exit 1
fi
grep -q 'REFUSED' "$tmp/refuse.log" || {
	echo "localt: FAIL -- cpmlocal failed for some other reason:" >&2
	tail -20 "$tmp/refuse.log" >&2
	exit 1
}

# ---- 2 and 3. the medium --------------------------------------------------
out=$tmp/medium.bin
$MK cpmlocal LOCALDIR="$tmp/extra" LOCALOUT="$out" >"$tmp/build.log" 2>&1 || {
	echo "localt: FAIL -- cpmlocal did not build the medium:" >&2
	tail -20 "$tmp/build.log" >&2
	exit 1
}
[ -f "$out" ] || { echo "localt: FAIL -- no medium at $out" >&2; exit 1; }

# Drive A: lives at the offset mkcpmdisk.py itself defines; read it from
# there rather than repeating the number.
eval "$(python3 -c "import sys; sys.path.insert(0, 'tools'); import mkcpmdisk as m; print('base=%d blks=%d' % (m.CPMA_BASE, m.CPMA_BLKS))")"
dd if="$out" of="$tmp/cpma.img" bs=512 skip="$base" count="$blks" \
	status=none 2>/dev/null
python3 tools/mkcpmfs.py --extract "$tmp/cpma.img" "$tmp/got" >/dev/null

[ -f "$tmp/got/CCP.Z8K" ] || {
	echo "localt: FAIL -- the medium's drive A: is not the development disk" >&2
	exit 1
}

for n in LOCALT.COM LOCALT.CMD; do
	[ -f "$tmp/got/$n" ] || {
		echo "localt: FAIL -- $n did not reach the medium's drive A:" >&2
		echo "localt: a supplied file was dropped, or its name was changed." >&2
		exit 1
	}
done

python3 - "$tmp" <<'PY' || exit 1
import os, sys
t = sys.argv[1]
for src, got in (("localt.com", "LOCALT.COM"), ("LOCALT.CMD", "LOCALT.CMD")):
    a = open(os.path.join(t, "extra", src), "rb").read()
    b = open(os.path.join(t, "got", got), "rb").read()
    if len(b) < len(a) or b[:len(a)] != a:
        sys.stderr.write("localt: FAIL -- %s reached drive A: with different"
                         " bytes\n" % got)
        sys.exit(1)
    if b[len(a):].strip(b"\0"):
        sys.stderr.write("localt: FAIL -- %s was padded with something other"
                         " than zeroes\n" % got)
        sys.exit(1)
PY

echo "localt: PASS -- cpmlocal refuses an in-tree output; both supplied" \
     "transients reached drive A: named and byte-for-byte intact"
