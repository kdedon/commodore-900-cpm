#!/usr/bin/env python3
# Copyright (c) 2026 Kevin Dedon.
# SPDX-License-Identifier: MIT

"""dirattr.py -- print the FILE ATTRIBUTE BITS of a CP/M directory.

    dirattr.py <cpma.img> [--user N]

One line per live file entry.  With --user N only entries whose user
byte is N are printed; without it every user area is, which is what a
drive packed by mkcpmfs.py (all user 0) looks like either way.  The
filter is what tells a SET run in user area 3 apart from one in user 0:
the two areas hold different files under the same directory.


    HELLO.TXT     R S 1 3

The attributes are bit 7 of the name and type bytes, at the offsets
c900oses/cpm8000/ref/cpm3/set.plm:395-401 names them (counted from the FCB drive byte):
9 read-only, 10 system, 11 archive, 1-4 the user attributes F1-F4.
mkcpmfs.py --list and cpm(1) both strip those bits before they print a
name, so neither can answer whether SET set them; this can, host-side,
from the bytes on the disk.
"""

import sys

ENTSIZE = 32
DRM = 511                               # directory entries on drive A:

BITS = ((9, 'R'), (10, 'S'), (11, 'A'),
        (1, '1'), (2, '2'), (3, '3'), (4, '4'))


def main(argv):
    want = None
    if len(argv) == 4 and argv[2] == '--user':
        want = int(argv[3])
    elif len(argv) != 2:
        sys.exit("usage: dirattr.py <img> [--user N]")
    with open(argv[1], 'rb') as f:
        dirbuf = f.read((DRM + 1) * ENTSIZE)
    seen = set()
    for i in range(DRM + 1):
        e = dirbuf[i * ENTSIZE:(i + 1) * ENTSIZE]
        if not e or e[0] > 15:          # free, or a CP/M 3 extension entry
            continue
        if want is not None and e[0] != want:
            continue
        raw = e[1:12]
        n = bytes(c & 0x7f for c in raw).decode('ascii', 'replace')
        name = n[:8].rstrip() + '.' + n[8:].rstrip()
        if name in seen:                # a later extent of the same file
            continue
        seen.add(name)
        flags = ' '.join(k for b, k in BITS if raw[b - 1] & 0x80)
        print("%-13s %s" % (name, flags))


main(sys.argv)
