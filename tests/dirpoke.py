#!/usr/bin/env python3
# Copyright (c) 2026 Kevin Dedon.
# SPDX-License-Identifier: MIT

"""dirpoke.py -- put ONE corrupt block number into a CP/M directory entry.

    python3 tests/dirpoke.py <cpma-partition-image> <block>

The fixture for F1(c) (verify-dirbnd): the BDOS's drive login scan
(src/bdos/fileio.c alloc()) hands every block number in every directory
entry to setaloc(), and a block number past the drive's DSM used to set a
bit outside that drive's allocation vector -- which, because every drive's
map is carved out of one pool (src/bios/bios900.c drvinit), means a bit
inside ANOTHER drive's live map.

This edits a drive-A: partition image in place: it finds a file entry whose
last big-map slot is unused and writes <block> there, little-endian, the way
the medium stores it (src/bdos/fileio.c swaps it on the way in).  The entry's
extent and record count are left alone, so the file's logical size does not
change -- the entry simply claims one block it has no business claiming.

Exits non-zero if there is no entry to corrupt, so a verify target cannot
pass by running on a clean disk.  It does not fail any build: the image it
edits is a test fixture, built only by the target that uses it.
"""

import sys

DIRBYTES = 4 * 4096            # dir_al 0xF000: four 4096-byte alloc blocks
ENTLEN = 32
MAPOFF = 16                    # dskmap starts at byte 16 of an entry
DE_XFCB = 0x10                 # entry bytes below this are file FCBs


def main(argv):
    if len(argv) != 3:
        sys.stderr.write("usage: dirpoke.py <cpma-image> <block>\n")
        return 2
    path, block = argv[1], int(argv[2])
    if not 0 <= block <= 0xffff:
        sys.stderr.write("dirpoke: block %d is not a 16-bit block number\n"
                         % block)
        return 2

    with open(path, "r+b") as fp:
        d = bytearray(fp.read(DIRBYTES))
        if len(d) < DIRBYTES:
            sys.stderr.write("dirpoke: %s is shorter than one directory\n"
                             % path)
            return 1
        for i in range(0, len(d), ENTLEN):
            e = d[i:i + ENTLEN]
            if e[0] >= DE_XFCB:
                continue                       # label, SFCB, XFCB or empty
            slot = i + MAPOFF + 14             # big-map word 7
            if e[MAPOFF + 14] or e[MAPOFF + 15]:
                continue                       # that slot is a real block
            name = "".join(chr(c & 0x7f) for c in e[1:9]).rstrip()
            typ = "".join(chr(c & 0x7f) for c in e[9:12]).rstrip()
            d[slot] = block & 0xff
            d[slot + 1] = (block >> 8) & 0xff
            fp.seek(0)
            fp.write(d)
            print("dirpoke: %s.%s (entry %d) now claims block %d"
                  % (name, typ, i // ENTLEN, block))
            return 0

    sys.stderr.write("dirpoke: no file entry with a free big-map slot in %s\n"
                     % path)
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
