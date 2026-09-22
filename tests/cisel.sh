#!/bin/sh
# Copyright (c) 2026 Kevin Dedon.
# SPDX-License-Identifier: MIT
#
# cisel.sh -- which verify targets a set of changed files can affect.
#
#   git diff --name-only BEFORE AFTER | sh tests/cisel.sh
#
# Reads repository-relative paths, one per line, and prints one of:
#
#   (nothing)          no path can affect a verify target -- docs only
#   all                run the whole suite
#   verify-a verify-b  run just these (make verify-all VERIFYSET="...")
#
# Used by CI on an ordinary push; a release always runs the whole suite.
# It reads its input and nothing else: no git, no build.
#
# The table is kept small ON PURPOSE.  A narrow row that is wrong hides a
# regression until the release run; a row that is missing only costs runner
# time.  So anything not named below, and anything touching the system, runs
# everything.  Rows are tried in order and the first match wins.
#
#   docs: *.md, LICENSE, CONTRIBUTORS (not under src/ or vendor/,
#         which are staged onto the drives)             nothing
#   src/shim/z80*, src/shim/tests/z80*                  verify-z80 verify-z80pip verify-z80save
#                                                       verify-z80poll verify-shim
#   src/shim/i86*, src/shim/tests/i86*,                 verify-i86 verify-i86ddt
#     tools/mkcmdfix.py                                 verify-i86asm verify-i86util
#                                                       verify-i86poll verify-shim
#   src/app/*  (the application programs on drive A:)   verify-a3 verify-sdb verify-appbound
#                                                       verify-repl verify-put verify-xdospoll5
#   anything else                                       all
#
# verify-shim is in both shim rows because it is the host-side, ASan-built
# run of the same shim sources and corpora.  The src/app row is the targets
# whose recipes run those programs or build them on the host.

set -u

all=
sel=
while IFS= read -r p; do
	[ -n "$p" ] || continue
	case $p in
	src/*|vendor/*) ;;
	*.md|LICENSE|*/LICENSE|CONTRIBUTORS) continue ;;
	esac
	case $p in
	src/shim/z80*|src/shim/tests/z80*)
		sel="$sel verify-z80 verify-z80pip verify-z80save verify-z80poll verify-shim" ;;
	src/shim/i86*|src/shim/tests/i86*|tools/mkcmdfix.py)
		sel="$sel verify-i86 verify-i86ddt verify-i86asm verify-i86util verify-i86poll verify-shim" ;;
	src/app/*)
		sel="$sel verify-a3 verify-sdb verify-appbound verify-repl verify-put verify-xdospoll5" ;;
	*)
		all=1 ;;
	esac
done

if [ -n "$all" ]; then
	echo all
elif [ -n "$sel" ]; then
	echo `printf '%s\n' $sel | sort -u`
fi
