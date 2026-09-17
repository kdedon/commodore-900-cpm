#!/bin/sh
# Copyright (c) 2026 Kevin Dedon.
# SPDX-License-Identifier: MIT

# initdir-mutate.sh -- mutation gate for verify-initdir.
#
# Lives in worktree (not shared scratchpad) to avoid cross-lane mutations.
# Pristine copies written once and never refreshed; restore checked by git.
#
#   sh tests/initdir-mutate.sh save          take the pristine copies (once)
#   sh tests/initdir-mutate.sh M1..M4        apply one mutation
#   sh tests/initdir-mutate.sh restore       put the sources back

set -e
cd "`dirname $0`/.."
P=build/pristine
F="src/cmd/initdir.c src/bdos/iosys.c"

case "$1" in
save)
	test -d $P && { echo "mut: $P already exists -- NOT overwriting it"; exit 1; }
	mkdir -p $P
	for f in $F; do cp $f $P/`basename $f`; done
	echo "mut: pristine copies saved (once, never refreshed)"
	;;

# M1 -- the cache invalidation.  Drop BDOS function 13 from restore().
# The BDOS then keeps this drive logged in with the directory hash
# signatures, allocation vector and directory-buffer record it had
# BEFORE the run, so a relocated file is reported missing on a disk
# where it is present.  Targets the "TYPE both before AND after" check.
M1)
	sed -i 's/^\t__bdos(BDOS_RESET, 0L);$/\t\/* M1 mutant: no reset *\//' src/cmd/initdir.c
	grep -q 'M1 mutant' src/cmd/initdir.c || { echo "mut: M1 did not apply"; exit 1; }
	echo "mut: M1 applied -- restore() no longer resets the disk system"
	;;

# M2 -- fresh SFCB must be all zeros ("no stamp").
# Leave the displaced entry's 31 tail bytes in place instead of clearing
# them.  Every count matches, every file still reads, and DIR looks
# right: only the byte-for-byte comparison against
# tools/mkcpmfs.py --initdir can see it.
M2)
	sed -i 's|^\t\tfor (j = 1; j < ENTSIZE; j++)$|\t\tfor (j = 1; j < 1; j++)\t/* M2 mutant */|' src/cmd/initdir.c
	grep -q 'M2 mutant' src/cmd/initdir.c || { echo "mut: M2 did not apply"; exit 1; }
	echo "mut: M2 applied -- a new SFCB keeps the old entry's tail bytes"
	;;

# M4 -- skip the relocation: stamp the fourth slot without first moving
# what was in it.  This is the corruption INITDIR exists to avoid, and
# it is invisible to every count in the transcript.
M4)
	sed -i 's|^\t\t\tputent(dst, rec + k \* ENTSIZE, r);$|\t\t\t;\t/* M4 mutant: no relocation */|' src/cmd/initdir.c
	grep -q 'M4 mutant' src/cmd/initdir.c || { echo "mut: M4 did not apply"; exit 1; }
	echo "mut: M4 applied -- the displaced entry is not moved anywhere"
	;;

# M3 -- the function 50 allow/refuse list.  Let BIOS code 4 (CONOUT)
# through.  Targets the BIOSET contract checks; nothing else notices.
M3)
	sed -i 's|^\tcase 8:\t\t\t/\* HOME\t\t\t\t\*/$|\tcase 4:\t\t\t/* M3 mutant: CONOUT */\n\tcase 8:\t\t\t/* HOME\t\t\t\t*/|' src/bdos/iosys.c
	grep -q 'M3 mutant' src/bdos/iosys.c || { echo "mut: M3 did not apply"; exit 1; }
	echo "mut: M3 applied -- function 50 also allows BIOS code 4"
	;;

restore)
	test -d $P || { echo "mut: no $P -- run save first"; exit 1; }
	for f in $F; do cp $P/`basename $f` $f; done
	# Do not trust a diff against $P: the authority is the repository.
	git diff --stat -- $F
	grep -n 'mutant' $F && { echo "mut: A MUTANT IS STILL IN THE TREE"; exit 1; }
	echo "mut: restored"
	;;
*)
	echo "usage: $0 save|M1|M2|M3|M4|restore"; exit 2;;
esac
