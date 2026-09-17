#!/bin/sh
# Copyright (c) 2026 Kevin Dedon.
# SPDX-License-Identifier: MIT

# Usage: sh tools/tcstamp.sh TOOLCHAIN
# Emit the source identity followed by a digest of the five compiler passes.
# Make preserves the stamp's mtime unless these change, invalidating objects
# when a compiler is replaced even within the same checkout.
set -e
# Called with nothing at all -- no toolchain resolved -- this still prints a
# line and still succeeds.  It is a record, and a build with no compiler has
# already been refused by the resolver, by name, long before here.
TCP=$1
if [ -z "$TCP" ]; then
	printf 'toolchain none resolved\ntoolchain fingerprint unknown\n'
	exit 0
fi

# What DEPS pins, printed beside what actually resolved.  Building against
# something else is ordinary -- that is what a working checkout beside this
# one is FOR -- so this is context, not a complaint.
HERE=$(cd "$(dirname "$0")/.." && pwd)
REF=$(awk '$1 == "toolchain" { print $4 }' "$HERE/DEPS" 2>/dev/null || true)

# Is this the pin?  A release says so in VERSION; a checkout says so only when
# it is clean and sitting exactly on the tag.  When it is not, the line names
# the one command that puts the pin on this machine -- an offer, not a
# complaint: `make all' compiles with what it found either way.
is_pin() {
	[ -n "$REF" ] || return 1
	if [ -f "$TCP/VERSION" ]; then
		# The archive writes the bare number; DEPS names the git tag.
		[ "${REF#v}" = "$(sed 's/^v//' "$TCP/VERSION")" ]
	elif [ -d "$TCP/.git" ]; then
		[ -z "$(git -C "$TCP" status --porcelain 2>/dev/null)" ] &&
		[ -n "$(git -C "$TCP" rev-parse HEAD 2>/dev/null)" ] &&
		[ "$(git -C "$TCP" rev-parse "$REF^{commit}" 2>/dev/null)" = \
		  "$(git -C "$TCP" rev-parse HEAD 2>/dev/null)" ]
	else
		return 1
	fi
}
if [ -z "$REF" ]; then
	PIN=
elif [ "$REF" = latest ]; then
	PIN="  [DEPS takes the latest release]"
elif is_pin; then
	PIN="  [the release DEPS pins]"
else
	PIN="  [DEPS pins $REF -- \`make deps DEP=toolchain' fetches it]"
fi

if [ -f "$TCP/VERSION" ]; then
	# an unpacked release: the tag is written into the package
	printf 'toolchain release %s  %s%s\n' "$(cat "$TCP/VERSION")" "$TCP" "$PIN"
elif [ -d "$TCP/.git" ]; then
	# a checkout: the commit, and whether the tree it built from was clean.
	# A dirty checkout is a perfectly normal thing to build from while
	# working on the compiler -- it is only worth recording that the bytes
	# match no commit, so a later "which release was this?" has an answer.
	c=$(git -C "$TCP" rev-parse --short HEAD 2>/dev/null || echo unknown)
	d=$(git -C "$TCP" describe --tags --always --dirty 2>/dev/null || echo '')
	if [ -n "$(git -C "$TCP" status --porcelain 2>/dev/null)" ]; then
		printf 'toolchain checkout %s (%s) DIRTY -- matches no commit  %s%s\n' \
			"$c" "$d" "$TCP" "$PIN"
	else
		printf 'toolchain checkout %s (%s)  %s%s\n' "$c" "$d" "$TCP" "$PIN"
	fi
else
	printf 'toolchain %s%s\n' "$TCP" "$PIN"
fi

# The fingerprint.  A commit id names the source a compiler was built from,
# which is not the same thing as the compiler: a checkout rebuilt in place
# reports the same commit and can emit different code.  Digesting the five
# passes themselves closes that, and it is the line make watches.  Passes it
# cannot read digest to `unknown' rather than stopping anything -- the
# resolver has already vouched that they are there, and this is a record, not
# a check.
PASSES="$TCP/host/build/z8001/cc0-z8001 $TCP/host/build/z8001/cc1-z8001 \
	$TCP/host/build/z8001/cc2-z8001 $TCP/host/build/as-z8001 \
	$TCP/host/build/ld-z8001"
fp=
for p in $PASSES; do [ -r "$p" ] || { fp=; break; }; fp=y; done
[ -n "$fp" ] && fp=$(cat $PASSES 2>/dev/null |
     { md5sum 2>/dev/null || cksum 2>/dev/null; } |
     awk '{ print $1 }')
printf 'toolchain fingerprint %s\n' "${fp:-unknown}"
