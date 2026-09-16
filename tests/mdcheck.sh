#!/bin/sh
# mdcheck.sh -- no file on shipped CP/M disk may cite a .md document.
# Reads packed images (string literals visible only when compiled).
# Also scans src/dist/ fixtures.
# Usage: mdcheck.sh IMAGE...

set -eu

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
DIST="$here/../src/dist"

# Three or more name characters before the extension, and a non-name
# character after it.  The bound is what keeps a 10 MB binary from matching
# ".md" out of three unlucky bytes; every real citation is far longer.
PAT='[A-Za-z][A-Za-z0-9_-][A-Za-z0-9_-]+\.md([^A-Za-z0-9_-]|$)'

fail=0
scan() {			# scan FILE LABEL
	hits=$(grep -aoE "$PAT" -- "$1" 2>/dev/null | sed 's/[^A-Za-z0-9_.-]*$//' \
		| sort -u) || true
	if [ -n "$hits" ]; then
		echo "mdcheck: FAIL -- $2 cites documentation the machine has not got:" >&2
		printf '  %s\n' $hits >&2
		fail=1
	fi
}

for img in "$@"; do
	scan "$img" "$img"
done

if [ -d "$DIST" ]; then
	for f in $(find "$DIST" -type f | sort); do
		scan "$f" "$f"
	done
fi

if [ "$fail" -ne 0 ]; then
	echo "mdcheck: say the thing itself instead of pointing at a document." >&2
	echo "mdcheck: a document that exists only in the c900oses workshop is" >&2
	echo "mdcheck: not on the target and not in this repository either --" >&2
	echo "mdcheck: no source comment may cite it, and a string the operator" >&2
	echo "mdcheck: can see certainly may not.  Comments may cite only what" >&2
	echo "mdcheck: exists in this tree." >&2
	exit 1
fi

echo "mdcheck: OK -- $# image(s) and the src/dist fixtures cite no .md file"
