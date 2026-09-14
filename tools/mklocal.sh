#!/bin/sh
# mklocal.sh -- build a LOCAL boot medium: the development drive A: plus
# extra files the operator supplies from a directory of their own.
#
# WHY THIS EXISTS, AND WHY IT REFUSES.  The compatibility shims are meant to
# run guest programs that this project does not own and may not redistribute.
# Those bytes are the operator's own copies: they are never vendored here,
# never committed, and never named in this repository -- LOCALDIR is a
# parameter precisely so that no name from it appears in any tracked file.
# A medium that carries them is in the same class as the files themselves, so
# it must not be written into a checkout either -- not even into an ignored
# build directory, where an ignore rule is the only thing between it and a
# commit.  Hence the one refusal below: the output path may not lie inside
# this checkout, nor inside the directory that holds the sibling checkouts.
# Set LOCALGUARD to name that directory explicitly if the default is wrong.
#
# This is opt-in.  Nothing in `all' reaches it, and it gates no build.
#
# Usage: mklocal.sh LOCALDIR LOCALOUT DISKA BLOCKS LABEL LABELMODE \
#                   CPMSYS CPMBIMG KBOOT

set -eu

die() { echo "mklocal: $*" >&2; exit 1; }

[ $# -eq 9 ] || die "usage: mklocal.sh LOCALDIR LOCALOUT DISKA BLOCKS LABEL LABELMODE CPMSYS CPMBIMG KBOOT"

LOCALDIR=$1; LOCALOUT=$2; DISKA=$3; BLOCKS=$4; LABEL=$5; LABELMODE=$6
CPMSYS=$7; CPMBIMG=$8; KBOOT=$9

[ -n "$LOCALDIR" ] || die "LOCALDIR is empty -- make cpmlocal LOCALDIR=<dir of extra files> LOCALOUT=<path outside the checkout>"
[ -n "$LOCALOUT" ] || die "LOCALOUT is empty -- make cpmlocal LOCALDIR=<dir of extra files> LOCALOUT=<path outside the checkout>"
[ -d "$LOCALDIR" ] || die "no such directory: $LOCALDIR"

# ---- the licence boundary, made mechanical --------------------------------
# Resolve the output through its PARENT, because the output itself need not
# exist yet.  Both the checkout and its parent are forbidden; symlinks are
# resolved first, so a link into the tree is refused like the tree itself.
repo=$(pwd -P)
guard=${LOCALGUARD:-$(dirname -- "$repo")}
outdir=$(dirname -- "$LOCALOUT")
[ -d "$outdir" ] || die "no such directory for LOCALOUT: $outdir"
outdir=$(CDPATH= cd -- "$outdir" && pwd -P)
outbase=$(basename -- "$LOCALOUT")
[ -n "$outbase" ] && [ "$outbase" != "." ] && [ "$outbase" != ".." ] \
	|| die "LOCALOUT names no file: $LOCALOUT"
out="$outdir/$outbase"

refuse=''
case "$out/" in
"$repo"/*)  refuse="inside this checkout ($repo)";;
"$guard"/*) refuse="inside the source tree root ($guard)";;
esac
if [ -n "$refuse" ]; then
	echo "mklocal: REFUSED -- LOCALOUT resolves to $out," >&2
	echo "mklocal: which is $refuse." >&2
	echo "mklocal: This medium carries files this project does not own and" >&2
	echo "mklocal: may not redistribute, so it is not written anywhere a" >&2
	echo "mklocal: checkout could pick it up -- an ignored build directory" >&2
	echo "mklocal: included.  Name a path outside it.  Nothing was written." >&2
	exit 1
fi

for f in "$DISKA" "$CPMSYS" "$CPMBIMG"; do
	[ -e "$f" ] || die "missing build product: $f -- run make first"
done
[ -f "$KBOOT" ] || die "no boot loader at '$KBOOT' -- see 'make deps DEP=kboot'"

# ---- staging --------------------------------------------------------------
# The staging tree holds the operator's files, so it lives beside the output,
# outside the checkout, and is removed again on the way out.
work="$out.mklocal.$$"
trap 'rm -rf -- "$work"' EXIT INT HUP TERM
rm -rf -- "$work"
mkdir -p -- "$work/diska"

for f in "$DISKA"/*; do
	[ -e "$f" ] || continue
	cp -- "$f" "$work/diska/"
done

# Extra files keep their own basenames: mkcpmfs.py applies the 8.3 naming
# rules (upper case, one dot) and refuses a name that does not fit, which is
# the diagnostic we want rather than a silent rename here.
extra=0
for f in "$LOCALDIR"/*; do
	[ -e "$f" ] || continue
	if [ -d "$f" ]; then
		echo "mklocal: skipping directory $(basename -- "$f")" >&2
		continue
	fi
	[ -f "$f" ] || { echo "mklocal: skipping non-regular $(basename -- "$f")" >&2; continue; }
	cp -- "$f" "$work/diska/"
	extra=$((extra + 1))
done
[ "$extra" -gt 0 ] || echo "mklocal: warning -- $LOCALDIR added no files" >&2

python3 tools/mkcpmfs.py --initdir --label "$LABEL" --label-mode "$LABELMODE" \
	"$work/cpma.img" "$BLOCKS" "$work/diska"
python3 tools/mkcpmdisk.py --kboot="$KBOOT" "$work/medium.bin" \
	"$CPMSYS" "$work/cpma.img" "$CPMBIMG"

# Publish only once the medium is complete.
mv -- "$work/medium.bin" "$out"

echo "mklocal: wrote $out -- development drive A: plus $extra local file(s)"
