#!/bin/sh
# Copyright (c) 2026 Kevin Dedon.
# SPDX-License-Identifier: MIT

# bootgate.sh LOG -- did the boot chain reach CP/M, and if not, whose fault?
#
# The chain is ROM -> kboot -> cpm.sys -> BDOS -> CCP, and a break anywhere in
# it otherwise surfaces as whatever test happens to run next failing on a
# transcript with no CP/M in it.  This walks the chain in order and names the
# FIRST stage that did not happen, so a loader regression reads as a loader
# regression.  Every stage below is a line one specific piece of software
# prints; nothing here matches on the absence of an error.
#
# verify-bootgate builds three broken media and requires this to fail on each;
# boot check is the easiest kind of check to write vacuously.
#
# Exit 0 = the medium boots CP/M.  Exit 1 = it does not, with the reason.

LOG="$1"
[ -n "$LOG" ] || { echo "usage: bootgate.sh LOG" >&2; exit 2; }

# The transcript is a SERIAL console log: every line ends CR-LF, so a `$'
# anchor never matches and an allowlist built from anchored patterns rejects
# the lines it was meant to accept.  That mistake made the first version of
# this script report a loader error on a perfect boot.  Match on a
# CR-stripped copy; report from the original.
NORM=`mktemp`
trap 'rm -f "$NORM"' 0
[ -f "$LOG" ] && tr -d '\r' < "$LOG" > "$NORM"

fail() {
	echo "bootgate: FAIL -- $1"
	shift
	for l in "$@"; do echo "           $l"; done
	echo "         last 12 lines of $LOG:"
	tail -12 "$LOG" | sed 's/^/           | /'
	exit 1
}

has() { grep -q "$1" "$NORM"; }

[ -s "$LOG" ] || fail "$LOG is empty or missing: the emulator produced no transcript."

has 'Commodore C900 diagnostics' || \
	fail "the ROM never ran: no power-on diagnostics in the transcript." \
	     "Suspect the emulator invocation or the firmware directory, not the disk."

# --- loader stages.  Anything failing from here to `launching kernel' is the
# --- LOADER or the medium's boot partition, and says so.
has 'kboot: Commodore 900 boot' || \
	fail "THE BOOTLOADER DID NOT RUN." \
	     "The ROM read the boot partition (blocks 0..135) and did not end up in kboot." \
	     "Suspect: the 'coherent' file in the boot partition (the kboot binary), the" \
	     "boot filesystem itself, or a boot partition whose used blocks ran past the" \
	     "ROM-safe span.  This is a LOADER/medium fault; no CP/M code has run yet."

! has 'kboot: no kboot.cfg; using defaults' || \
	fail "THE BOOTLOADER COULD NOT READ kboot.cfg." \
	     "kboot fell back to its compiled-in Coherent default (block 136), which on a" \
	     "CP/M-only medium points at nothing.  Suspect: kboot.cfg missing from the boot" \
	     "partition, corrupt, or carrying no parseable 'os' line." \
	     "This is a LOADER CONFIG fault, not a CP/M one."

# The same fault, reported the other way round.  kboot separates absence from
# corruption -- a disk with NO kboot.cfg falls back to the compiled-in
# defaults and says so (above), a kboot.cfg that IS there and names nothing
# bootable is damage, so it says THAT and enters recovery rather than booting
# a guess.  Both are the config, and both must read as the config here: with
# only the check above, a corrupt config fell through to the generic
# loader-error catch below, which sends the reader to the `os' line's base
# block of a file that could not be parsed at all.
! has 'kboot: kboot.cfg is there and names nothing bootable' || \
	fail "THE BOOTLOADER COULD NOT READ kboot.cfg." \
	     "kboot parsed the file and found no usable 'os' line, which it treats as" \
	     "damage rather than as a disk built without a config: it refused the" \
	     "compiled-in defaults and entered recovery.  Suspect: kboot.cfg truncated," \
	     "hand-edited, or written by a builder that names its entries differently." \
	     "This is a LOADER CONFIG fault, not a CP/M one."

# Any kboot line that is not one of its four normal ones is a loader
# diagnostic (bad l.out magic, iread failed, dirlook error, text too large).
KERR=`grep '^kboot: ' "$NORM" | grep -v -E \
	'^kboot: (Commodore 900 boot|loading [^ ]*|staged, copying down|launching kernel)$'`
[ -z "$KERR" ] || \
	fail "THE BOOTLOADER REPORTED AN ERROR: $KERR" \
	     "kboot got as far as reading the medium and refused to launch." \
	     "Suspect: the 'os' line's base block (does it name the partition cpm.sys is" \
	     "actually in?), the kernel filename, or the l.out header of cpm.sys." \
	     "This is a LOADER/medium fault: CP/M was never entered."

has 'kboot: loading cpm.sys' || \
	fail "THE BOOTLOADER DID NOT TRY TO LOAD cpm.sys." \
	     "It ran and chose an OS, but not this one.  Suspect: the 'os' line names" \
	     "another kernel file, or a second menu entry was selected." \
	     "This is a LOADER CONFIG fault."

has 'kboot: launching kernel' || \
	fail "THE BOOTLOADER FOUND cpm.sys BUT NEVER LAUNCHED IT." \
	     "It stopped between reading the file and the jump.  Suspect: the staging /" \
	     "copy-down path, or a cpm.sys too large for it.  LOADER fault."

# --- from here the loader has done its job: failures are CP/M's.
has 'CP/M-8000(tm) for the Commodore 900' || \
	fail "the loader launched cpm.sys and CP/M never signed on." \
	     "The loader did its job (it reported 'launching kernel'); suspect src/bios/crt.s," \
	     "the BIOS entry, or a bad link -- NOT kboot."

has 'CPM-Z8000 Version' || \
	fail "the BIOS banner printed but the BDOS did not sign on." \
	     "Suspect src/bdos/bdosmisc.c bdosinit or the BIOS/BDOS handoff -- not the loader."

N=`grep -c 'A>' "$NORM"`
[ "$N" -ge 2 ] || \
	fail "CP/M signed on but never came back to a second A> prompt (saw $N)." \
	     "The CCP printed no prompt, or did not finish the command it was sent." \
	     "Suspect the CCP or the console input path, or too small a --max budget." \
	     "Not the loader: it delivered a system that signed on."

echo "bootgate: PASS -- ROM -> kboot -> cpm.sys -> BDOS -> CCP, $N A> prompts"
exit 0
