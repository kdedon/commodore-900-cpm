#!/bin/sh
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
#   kind release  a third-party BINARY.  <ref> is a TAG, unpacked into
#                 because we cannot fix it and nothing about a binary is
#                 recoverable from our own history: "which one ran this" has
#                 to be a number chosen in advance.  <asset> is the release
#                 asset's file name, with @REF@ standing for the tag and
#                 @HOST@ for the platform suffix INCLUDING the archive
#                 extension -- the two axes are not independent, since a
#                 Windows asset is a .zip and a Linux one a .tar.gz.  We build
#                 on two hosts now, so a one-host asset name would make `make
#                 deps' work on one of them only.
#
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

fetch_release() {
	if [ -d "$4" ]; then
		echo "$1: $3 already unpacked at $4 -- left alone"
		return 0
	fi
	[ -n "$5" ] || { echo "$1: a release line needs an asset name" >&2; return 1; }
	case $(uname -s) in
	Linux)			host=linux-x86_64.tar.gz ;;
	MINGW*|MSYS*|CYGWIN*)	host=windows-x86_64.zip ;;
	*)	echo "$1: no release asset is published for $(uname -s);" >&2
		echo "  build the dependency and name it by variable." >&2
		return 1 ;;
	esac
	asset=$(echo "$5" | sed "s/@REF@/$3/g; s/@HOST@/$host/g")
	from=$2/releases/download/$3/$asset
	tmp=$4.tmp.$$
	rm -rf "$tmp"
	mkdir -p "$tmp"
	echo "$1: downloading $from"
	if ! curl -fL --retry 2 -o "$tmp/$asset" "$from"; then
		rm -rf "$tmp"
		echo "$1: no release asset at $from" >&2
		echo "  The tag in DEPS is the pin: it is deliberate and bumped by hand," >&2
		echo "  so a missing one means that release has not been published yet." >&2
		echo "  Until it is, build the dependency yourself and name it by variable;" >&2
		echo "  the resolver's refusal says which variable." >&2
		return 1
	fi
	case "$asset" in
	*.tar.gz|*.tgz) tar xzf "$tmp/$asset" -C "$tmp" ;;
	*.zip)          unzip -q "$tmp/$asset" -d "$tmp" ;;
	*) echo "$1: don't know how to unpack $asset" >&2; rm -rf "$tmp"; return 1 ;;
	esac
	rm -f "$tmp/$asset"
	# The asset carries one top directory (bin/, rom/, disk/ inside it); it is
	# stripped so deps/<name>/bin/c900 is the path the resolvers search for.
	inner=
	for d in "$tmp"/*; do
		[ -d "$d" ] || { inner=; break; }
		[ -z "$inner" ] || { inner=; break; }
		inner=$d
	done
	mkdir -p "$(dirname "$4")"
	if [ -n "$inner" ]; then mv "$inner" "$4"; rm -rf "$tmp"; else mv "$tmp" "$4"; fi
	echo "$1: unpacked $3 -> $4"
}

rc=0
# DEPS is read on fd 3: git and curl inherit stdin, and a clone that consumed
# the rest of the file would silently skip the remaining edges.
while read -r name kind url ref asset <&3; do
	case "$name" in ''|\#*) continue ;; esac
	[ -z "$only" ] || [ "$only" = "$name" ] || continue
	dir=$(basename "$url" .git)
	case "$kind" in
	git)     fetch_git "$name" "$url" "$ref" "$(cd "$root/.." && pwd)/$dir" || rc=1 ;;
	*)       echo "$name: unknown kind \`$kind' in DEPS" >&2; rc=1 ;;
	esac
	got=$(resolve "$name") || got=
	[ -n "$got" ] && echo "$name: resolves to $got" || :
done 3< "$deps"

# A clone that resolves to nothing is not a failure: our own repositories are
# source, and most of them have to be BUILT before a resolver will accept them.
exit $rc
