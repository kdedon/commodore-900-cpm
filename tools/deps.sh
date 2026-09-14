#!/bin/sh
# deps.sh -- resolve the four things this build consumes from other
# repositories, and refuse by name when one is missing.
#
#   sh tools/deps.sh <dep>              print the resolved path, or nothing
#   sh tools/deps.sh -n <dep> [value]   print nothing; refuse and exit 2 if
#                                       <value> (or, empty, the search) does
#                                       not resolve
#
# The two modes exist because the four edges are wanted at different times.
# The search runs when the Makefile is read, so a variable can be assigned
# from it; the REFUSAL belongs in the recipe that wanted the thing, because
# `all' needs only the toolchain and must not be blocked by a missing
# emulator.  -n takes the make variable's current value so that a
# `make EMU=/wrong' is refused as what the user asked for rather than
# silently re-searched.
#
#   kbootsrc      KBOOTSRC          the CHECKOUT, for include/bootinfo.h
#
# Search order for each: the variable wins; then the PINNED release in deps/
# -- named by the tag DEPS gives, so bumping the pin stops an older unpack
# answering to it -- then (emulator only) a c900 on $PATH; then a sibling
# checkout, walking
# outward AT MOST THREE PARENTS, then one inside a `repos/' directory beside
# this repository.  Three parents is what reaches the enclosing workspace from
# a repository staged at <workspace>/repos/<repo>; further out is not a
# sibling, it is a coincidence -- an unbounded walk finds another job's
# checkout on a CI runner and reports a false success.

root=$(cd "$(dirname "$0")/.." && pwd)

# The tag DEPS pins a release to.  `make deps' unpacks into deps/<dir>-<ref>
# and leaves deps/<dir> pointing at it, so the pin has a path of its own: the
# search below names that path FIRST, and a bump of DEPS therefore stops the
# previous unpack from answering as the pin.  This is the whole of what makes
# the pin the default -- there is nothing to enforce and nothing to refuse.
pinned() {
	awk -v n="$1" '$1 == n { print $4 }' "$root/DEPS" 2>/dev/null
}

# The sibling search list for a repository name: three parents, then repos/.
siblings() {
	_d=$root
	_n=0
	while [ $_n -lt 3 ] && [ "$_d" != / ]; do
		_d=$(cd "$_d/.." && pwd)
		echo "$_d/$1"
		_n=$((_n + 1))
	done
	echo "$root/repos/$1"
}

# Per dep: VAR names the variable, WANT what is being looked for, LIST the
# candidate paths, ok() the test that a candidate is the real thing, and
case "$1" in
-n) mode=need; dep=$2; given=$3 ;;
*)  mode=find; dep=$1; given= ;;
esac

case "$dep" in
emu)
	VAR="EMU"
	WANT="the Commodore 900 emulator"
	LIST="$root/deps/commodore-900-emulator-$(pinned emu) \
	      $root/deps/commodore-900-emulator"
	p=$(command -v c900 2>/dev/null) &&
		LIST="$LIST $(dirname "$(dirname "$p")")"
	LIST="$LIST $(siblings commodore-900-emulator)"
	[ -n "$given" ] || given=${EMU:-${C900_EMU:-}}
	# A file names bin/c900; a directory names the checkout.  Both are
	# accepted, and the checkout is what is printed: the verify targets
	# run `cd $EMU/bin && ./c900', which is also how c900 finds rom/.
	fixup() {
		case "$1" in
		*/bin/c900) dirname "$(dirname "$1")" ;;
		*) echo "$1" ;;
		esac
	}
	ok() { [ -x "$1/bin/c900" ]; }
	HOW="  Clone https://github.com/MichalPleban/commodore-900-emulator
  and \`make' it, to one of the paths above -- or put its c900 on \$PATH,
  or set EMU to the checkout or to its bin/c900.
  Only the verify suite needs it; \`make all' does not.
  \`make deps' unpacks the release DEPS pins."
	;;
kboot)
	VAR="KBOOT"
	WANT="a built kboot loader"
	LIST=
	for d in $(siblings commodore-900-kboot); do
		LIST="$LIST $d/build/kboot"
	done
	[ -n "$given" ] || given=${KBOOT:-}
	# A directory names the checkout, a file the loader itself.
	fixup() { if [ -d "$1" ]; then echo "$1/build/kboot"; else echo "$1"; fi; }
	ok() { [ -f "$1" ]; }
	HOW="  kboot is a built artifact of another repository, not part of this one:
      git clone <...>/commodore-900-kboot
      make -C commodore-900-kboot
  or point KBOOT= at an already-built copy.  \`make deps' clones it."
	;;
kbootsrc)
	VAR="KBOOTSRC"
	WANT="a kboot checkout, for the handoff ABI header"
	LIST=
	for d in $(siblings commodore-900-kboot); do
		LIST="$LIST $d"
	done
	[ -n "$given" ] || given=${KBOOTSRC:-}
	fixup() { echo "$1"; }
	ok() { [ -f "$1/include/bootinfo.h" ]; }
	HOW="  The BIOS compiles kboot's include/bootinfo.h, which is the layout of
  the block the loader writes; a copy of it here would drift against the
  loader that fills it in.  Clone the checkout:
      git clone <...>/commodore-900-kboot
  or point KBOOTSRC= at one.  \`make deps DEP=kboot' clones it."
	;;
toolchain)
	VAR="C900_TOOLCHAIN"
	WANT="the Z8001 cross toolchain"
	# DEPS pins this as a release, unpacked by `make deps' into deps/ -- tried
	# first, same as a sibling checkout would be.
	LIST="$root/deps/commodore-900-toolchain-$(pinned toolchain)
	      $root/deps/commodore-900-toolchain
	      $(siblings commodore-900-toolchain)"
	[ -n "$given" ] || given=${C900_TOOLCHAIN:-${Z8001_TOOLCHAIN:-}}
	fixup() { echo "$1"; }
	# The checkout is printed, not its host/build: this build wants both the
	# compiler passes under host/build and the lout2cpm source under tools/.
	ok() {
		[ -x "$1/host/build/z8001/cc0-z8001" ] &&
		[ -x "$1/host/build/z8001/cc1-z8001" ] &&
		[ -x "$1/host/build/z8001/cc2-z8001" ] &&
		[ -x "$1/host/build/as-z8001" ] &&
		[ -x "$1/host/build/ld-z8001" ]
	}
	HOW="  The compiler, assembler and linker are a repository of their own,
  because COHERENT, CP/M and kboot all consume them.  DEPS pins a RELEASE:
      make deps DEP=toolchain
  unpacks it to deps/commodore-900-toolchain -- or point C900_TOOLCHAIN= at
  a built checkout of your own (a checkout that is present but not yet
  built resolves to nothing here, on purpose)."
	;;
	VAR="COHERENT_OS"
	WANT="a COHERENT userland checkout"
	LIST="$(siblings commodore-900-coh-userland)"
	[ -n "$given" ] || given=${COHERENT_OS:-}
	HOW="  Two targets cross-check this directory format against COHERENT's own
  reader of it, cpm(1), so they need that source tree:
      git clone <...>/commodore-900-coh-userland
  or point COHERENT_OS= at your checkout.  It is the only thing in this
  repository that reads the COHERENT tree, and no other target needs it.
  \`make deps' clones the repository named in DEPS."
	;;
*)
	exit 2
	;;
esac

found=
if [ -n "$given" ]; then
	given=$(fixup "$given")
	ok "$given" && found=$given
else
	for c in $LIST; do
		c=$(fixup "$c")
		ok "$c" && { found=$c; break; }
	done
fi

if [ -n "$found" ]; then
	exit 0
fi

[ "$mode" = find ] && exit 0

{
	if [ -n "$given" ]; then
		echo "*** $WANT: nothing usable at $VAR=$given."
		echo "*** That is $VAR's own value, so nothing else was tried."
		echo "*** Unset it to search these instead:"
	else
		echo "*** $WANT: none found, and this target needs one."
		echo "*** $VAR is unset; the paths tried were:"
	fi
	for c in $LIST; do echo "***     $c"; done
	echo "$HOW" | sed 's/^/*** /'
} >&2
exit 2
