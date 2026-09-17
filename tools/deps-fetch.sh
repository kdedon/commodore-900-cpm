#!/bin/sh
# Copyright (c) 2026 Kevin Dedon.
# SPDX-License-Identifier: MIT

# deps-fetch.sh -- `make deps': acquire what DEPS says this repository consumes.
#
#   sh <dir>/deps-fetch.sh            place every dependency in DEPS
#   sh <dir>/deps-fetch.sh <name>     just that one
#
# This is NOT a way to FIND things.  It is a way to PUT them where the
# resolvers already look, so the resolver contract is untouched: a named
# variable still wins, the search list is still the convenience, and a missing
# dependency is still refused by name where it is wanted.  Nothing here is
# consulted at build time.
#
# DEPS is `name kind url ref [asset]', one line per edge, # for a comment:
#
#   kind git      one of OUR repositories.  Cloned to ../<basename of url> on
#                 branch <ref> and left FLOATING there -- no detach, no
#                 lockfile.  Four repositories are edited in the same
#                 afternoon; a pin would record what a build should have used,
#                 and the release stamp already records what it did.
#   kind release  a third-party BINARY.  <ref> is a TAG, or `latest' for the
#                 newest published one, unpacked into
#                 deps/<basename of url>-<ref>/, with deps/<basename of url>
#                 left pointing at it; both are gitignored.  The unpack is
#                 named by the tag so that the pin has a path of its own,
#                 which is what lets tools/deps.sh prefer the pin over
#                 whatever else is lying around the machine.  Pinned
#                 because we cannot fix it and nothing about a binary is
#                 recoverable from our own history: "which one ran this" has
#                 to be a number chosen in advance.  <asset> is the release
#                 asset's file name -- several, comma-separated, unpack into
#                 one directory, and a non-archive is placed as is -- with
#                 @REF@ standing for the tag and
#                 @HOST@ for the platform suffix INCLUDING the archive
#                 extension -- the two axes are not independent, since a
#                 Windows asset is a .zip and a Linux one a .tar.gz.  We build
#                 on two hosts now, so a one-host asset name would make `make
#                 deps' work on one of them only.
#
# Idempotent, and it never writes over an existing checkout.  For a CLONE,
# "already there" means the resolver finds one -- our own repositories are
# floating branches and any checkout of one will do.  For a PINNED RELEASE it
# means the pinned unpack is on disk, and nothing weaker: a sibling checkout
# of the toolchain resolves, but it is not v0.1.4, and treating it as though
# it were is how `make deps DEP=toolchain' came to decline to fetch the very
# thing DEPS pins.  Asking for the pin now gets the pin.
set -e

root=$(cd "$(dirname "$0")/.." && pwd)
here=$(cd "$(dirname "$0")" && pwd)
deps=$root/DEPS

[ -f "$deps" ] || { echo "deps-fetch.sh: no DEPS file at $deps" >&2; exit 2; }

only=${1:-}

# The repository's own resolver, if it has one, is the authority on whether a
# dependency is already resolvable -- asking it is what keeps `make deps' from
# cloning a second copy of something the build can already see.
resolve() {
	[ -f "$here/deps.sh" ] || return 1
	_r=$(sh "$here/deps.sh" "$1" 2>/dev/null) || return 1
	[ -n "$_r" ] || return 1
	echo "$_r"
}

# deps/<dir> is the name the resolvers have always searched; deps/<dir>-<ref>
# is the pin's own.  Keep the plain name pointing at the pin so both answer,
# and so bumping DEPS moves the plain name too.  A copy stands in where
# symlinks do not exist; failing to place it is not an error -- the ref-named
# directory is the one the resolver looks at first.
point_at() {
	# $1 the unpacked pin  $2 the plain name
	[ -n "$2" ] || return 0
	[ "$1" = "$2" ] && return 0
	if [ -L "$2" ] || [ ! -e "$2" ]; then
		rm -f "$2"
		ln -s "$(basename "$1")" "$2" 2>/dev/null ||
			cp -a "$1" "$2" 2>/dev/null || return 0
		echo "  $2 -> $(basename "$1")"
	fi
	return 0
}

fetch_git() {
	# $1 name  $2 url  $3 ref  $4 dest
	if git -C "$4" rev-parse --git-dir >/dev/null 2>&1; then
		echo "$1: checkout already at $4 -- left alone"
		return 0
	fi
	if [ -e "$4" ]; then
		echo "$1: $4 exists and is not a git checkout -- left alone" >&2
		return 1
	fi
	echo "$1: cloning $2 ($3) -> $4"
	git clone --branch "$3" "$2" "$4" || return 1
}

# The newest published release's tag, read off the redirect /releases/latest
# answers with.  Not the API: that is rate-limited per IP, and CI runners share
# them.  A repository with no release redirects to /releases, which has no tag
# in it, so this prints nothing and the caller refuses by name.
latest_tag() {
	_lt=$(curl -fsLI -o /dev/null -w '%{url_effective}' "$1/releases/latest") || return 1
	case $_lt in
	*/releases/tag/*) echo "${_lt##*/releases/tag/}" ;;
	esac
}

fetch_release() {
	# $1 name  $2 url  $3 ref  $4 dest (deps/<dir>-<ref>)  $5 asset  $6 link
	if [ -d "$4" ]; then
		echo "$1: $3 already unpacked at $4 -- left alone"
		point_at "$4" "$6"
		return 0
	fi
	[ -n "$5" ] || { echo "$1: a release line needs an asset name" >&2; return 1; }
	# @HOST@ is resolved only for an asset that USES it, so an unrecognised
	# `uname -s' refuses only a per-host edge and never a HOST-INDEPENDENT
	# one, like kboot's, whose assets are the same files on every machine.
	case "$5" in
	*@HOST@*)
		case $(uname -s) in
		Linux)			host=linux-x86_64.tar.gz ;;
		MINGW*|MSYS*|CYGWIN*)	host=windows-x86_64.zip ;;
		*)	echo "$1: the asset name is per-host (@HOST@) and none is" >&2
			echo "  published for $(uname -s);" >&2
			echo "  build the dependency and name it by variable." >&2
			return 1 ;;
		esac ;;
	*)	host= ;;
	esac
	tmp=$4.tmp.$$
	rm -rf "$tmp"
	mkdir -p "$tmp/.dl"
	# <asset> may name several, comma-separated, all unpacked into one
	# directory: kboot publishes its loader and its header apart.
	for a in $(echo "$5" | tr , ' '); do
		asset=$(echo "$a" | sed "s/@REF@/$3/g; s/@HOST@/$host/g")
		from=$2/releases/download/$3/$asset
		echo "$1: downloading $from"
		if ! curl -fL --retry 2 -o "$tmp/.dl/$asset" "$from"; then
			rm -rf "$tmp"
			echo "$1: no release asset at $from" >&2
			echo "  The tag in DEPS is the pin: it is deliberate and bumped by hand," >&2
			echo "  so a missing one means that release has not been published yet." >&2
			echo "  Until it is, build the dependency yourself and name it by variable;" >&2
			echo "  the resolver's refusal says which variable." >&2
			return 1
		fi
		rm -rf "$tmp/.x"
		mkdir "$tmp/.x"
		case "$asset" in
		*.tar.gz|*.tgz) tar xzf "$tmp/.dl/$asset" -C "$tmp/.x" ;;
		*.zip)          unzip -q "$tmp/.dl/$asset" -d "$tmp/.x" ;;
		# Anything else is the file itself, placed under its own name.
		*) mv "$tmp/.dl/$asset" "$tmp/$asset"; continue ;;
		esac
		# An archive carries one top directory (bin/, rom/, disk/ inside it);
		# it is stripped so deps/<name>/bin/c900 is the path the resolvers
		# search for.
		inner=
		for d in "$tmp/.x"/*; do
			[ -d "$d" ] || { inner=; break; }
			[ -z "$inner" ] || { inner=; break; }
			inner=$d
		done
		cp -a "${inner:-$tmp/.x}/." "$tmp/"
	done
	rm -rf "$tmp/.dl" "$tmp/.x"
	mkdir -p "$(dirname "$4")"
	mv "$tmp" "$4"
	echo "$1: unpacked $3 -> $4"
	point_at "$4" "$6"
}

rc=0
# DEPS is read on fd 3: git and curl inherit stdin, and a clone that consumed
# the rest of the file would silently skip the remaining edges.
while read -r name kind url ref asset <&3; do
	case "$name" in ''|\#*) continue ;; esac
	[ -z "$only" ] || [ "$only" = "$name" ] || continue
	dir=$(basename "$url" .git)
	# A clone is satisfied by any checkout the resolver can see; a pinned
	# release is satisfied only by that pin, so the resolver's answer does
	# not get a vote there.  fetch_release does its own idempotence on the
	# ref-named directory.
	if [ "$kind" != release ]; then
		got=$(resolve "$name") || got=
		if [ -n "$got" ]; then
			echo "$name: already resolves to $got"
			continue
		fi
	fi
	# `latest' names no tag, so the newest published one is asked for first:
	# the unpack is then named by that tag like any pin, and a newer release
	# lands beside the old one with deps/<dir> moved onto it.
	if [ "$kind" = release ] && [ "$ref" = latest ]; then
		ref=$(latest_tag "$url") || ref=
		[ -n "$ref" ] || { echo "$name: no published release at $url" >&2; rc=1; continue; }
		echo "$name: latest release is $ref"
	fi
	case "$kind" in
	git)     fetch_git "$name" "$url" "$ref" "$(cd "$root/.." && pwd)/$dir" || rc=1 ;;
	release) fetch_release "$name" "$url" "$ref" "$root/deps/$dir-$ref" "$asset" \
			"$root/deps/$dir" || rc=1 ;;
	*)       echo "$name: unknown kind \`$kind' in DEPS" >&2; rc=1 ;;
	esac
	got=$(resolve "$name") || got=
	[ -n "$got" ] && echo "$name: resolves to $got" || :
done 3< "$deps"

# A clone that resolves to nothing is not a failure: our own repositories are
# source, and most of them have to be BUILT before a resolver will accept them.
exit $rc
