#!/bin/sh
# stage-devpack.sh -- stage the full DRI CP/M-8000 dev pack onto drive A:.
# Copies from z8001mb/cpm8k/packages/base/
# into the drive-A: staging directory under CP/M 8.3 UPPERCASE names, bytes verbatim, and
# writes the on-target test source HELLO.C.
#
# Idempotent: a file is only rewritten when its content differs (cmp), so
# re-runs do not churn mtimes and do not force a cpma.img repack.  Stale
# .CPM-named copies (pre-M5 naming; the CCP only finds .Z8K) are removed.
#
# Makefile hook (run before packing cpma.img):
#     $(CPMAIMG): ... tools/stage-devpack.sh ...
#         sh tools/stage-devpack.sh
#
# Usage: stage-devpack.sh [basedir [diskadir]]

set -eu

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
BASE=${1:-"$here/../vendor/z8001mb/cpm8k/packages/base"}
DISKA=${2:-"$here/../build/diska"}

[ -d "$BASE" ]  || { echo "stage-devpack: no such dir: $BASE" >&2; exit 1; }
mkdir -p -- "$DISKA"
[ -d "$DISKA" ] || { echo "stage-devpack: no such dir: $DISKA" >&2; exit 1; }

# install SRC DST -- copy only if missing or different (keeps mtimes stable)
install() {
	if [ ! -f "$BASE/$1" ]; then
		echo "stage-devpack: missing source $BASE/$1" >&2
		exit 1
	fi
	if ! cmp -s -- "$BASE/$1" "$DISKA/$2"; then
		cp -- "$BASE/$1" "$DISKA/$2"
		echo "staged $2"
	fi
}

# Stale pre-M5 names: the CCP searches NAME.Z8K (then blank, then .SUB),
# never NAME.CPM.
rm -f -- "$DISKA"/*.CPM

# Utilities
install dump.z8k    DUMP.Z8K
install ddt.z8k     DDT.Z8K
install ed.z8k      ED.Z8K
# PIP.Z8K: not staged from here -- the Makefile builds it from src/cmd/
# pip.c (DRI's own source, with the multio() BYTE/RECORD-count fix) and
# copies it over this directory itself.  Staging the vendor binary here
# too would race the rebuilt one for the same name.
#
# STAT.Z8K: no longer staged from here either, as of the 0x2031 bump.
# The Makefile builds it from src/cmd/stat.c and copies it over this
# directory, because DRI's columns() reads the wrong SCB byte at 3.x and
# the vendor binary still has that bug.  See the USTAT comment there.
install ar8k.z8k    AR8K.Z8K
install nmz8k.z8k   NMZ8K.Z8K
install sizez8k.z8k SIZEZ8K.Z8K
install xcon.z8k    XCON.Z8K
install xdump.z8k   XDUMP.Z8K

# Compiler chain (zcc chains zcc1 -> zcc2 -> zcc3 by name; asz8k reads
# ASZ8K.PD, its opcode/symbol table, from the current drive)
install zcc.z8k     ZCC.Z8K
install zcc1.z8k    ZCC1.Z8K
install zcc2.z8k    ZCC2.Z8K
install zcc3.z8k    ZCC3.Z8K
install asz8k.z8k   ASZ8K.Z8K
install asz8k.pd    ASZ8K.PD
install ld8k.z8k    LD8K.Z8K

# Runtime
install libcpm.a    LIBCPM.A
install startup.o   STARTUP.O
install startup.8kn STARTUP.8KN

# Headers (all *.h in the base package)
for h in "$BASE"/*.h; do
	bn=$(basename -- "$h")
	up=$(printf %s "$bn" | tr '[:lower:]' '[:upper:]')
	install "$bn" "$up"
done

# On-target test source: minimal K&R C, compilable by the 1983 DRI zcc.
# (Write via a temp + cmp so re-runs stay mtime-stable.)
tmp="$DISKA/.hello.c.tmp"
cat > "$tmp" <<'EOF'
#include <stdio.h>

main()
{
	printf("Hello from zcc on the Commodore 900!\n");
	return 0;
}
EOF
if ! cmp -s -- "$tmp" "$DISKA/HELLO.C"; then
	mv -- "$tmp" "$DISKA/HELLO.C"
	echo "staged HELLO.C"
else
	rm -f -- "$tmp"
fi

echo "stage-devpack: $DISKA is current"
