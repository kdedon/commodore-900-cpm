#!/usr/bin/env python3
"""driveb-check.py - prove drive B: is a different region of the disk than A:.

Usage: driveb-check.py IMAGE ABASE ABLOCKS BBASE BBLOCKS PACKEDB

  IMAGE    the whole hd42 disk image a CP/M session has just run against
  ABASE/ABLOCKS, BBASE/BBLOCKS   the two drives' regions, in 512-byte blocks
  PACKEDB  build/cpmb.img as the packer wrote it, before the session

The interesting failure this is built against is ALIASING: a SELDSK that
handed drive B: drive A:'s disk parameter header would make every naive
check pass -- files "written on B:" would read back on B:, DIR B: would
list them, and only their ADDRESS would be wrong.  So nothing here asks a
drive about itself.  Every check is a statement about where bytes are in
the whole 42 MB image, made by searching the image for directory entries
whose 11-byte names cannot occur anywhere else:

  * a name created on B: must occur inside B:'s region and NOWHERE ELSE
    (under aliasing it would be inside A:'s, which fails on both halves);
  * a name created on A: must occur inside A:'s region and nowhere else;
  * the two directories must not be the same bytes;
  * B:'s region must differ from the image the packer wrote, or nothing
    the session did to B: reached the disk at all.

The region arithmetic is checked too: A: and B: must not overlap, which
is the one way the drive table could be wrong without any name moving.
"""

import sys

BS = 512
DIRBYTES = 4 * 4096             # 4 allocation blocks of directory


def name11(s):
    """'BNEW.TXT' -> the 11-byte padded directory form."""
    n, _, t = s.partition('.')
    return (n.ljust(8) + t.ljust(3)).upper().encode('ascii')


def occurrences(img, pat):
    out, i = [], img.find(pat)
    while i >= 0:
        out.append(i)
        i = img.find(pat, i + 1)
    return out


class Checker(object):
    def __init__(self):
        self.fails = 0

    def check(self, ok, what, detail=''):
        print("%-58s %s%s" % (what, "ok" if ok else "FAIL",
                              '' if ok else '  -- ' + detail))
        if not ok:
            self.fails += 1


def main():
    if len(sys.argv) != 7:
        sys.exit(__doc__)
    imgpath = sys.argv[1]
    abase, ablks, bbase, bblks = (int(x) for x in sys.argv[2:6])
    packedb = sys.argv[6]

    img = open(imgpath, 'rb').read()
    alo, ahi = abase * BS, (abase + ablks) * BS
    blo, bhi = bbase * BS, (bbase + bblks) * BS
    c = Checker()

    c.check(len(img) >= bhi, "image is large enough to hold drive B:",
            "%d bytes, B: ends at %d" % (len(img), bhi))
    c.check(ahi <= blo or bhi <= alo, "the two drive regions do not overlap",
            "A: [%d,%d) B: [%d,%d)" % (alo, ahi, blo, bhi))

    adir = img[alo:alo + DIRBYTES]
    bdir = img[blo:blo + DIRBYTES]
    c.check(adir != bdir, "A: and B: have different directories on disk",
            "identical %d-byte directories: B: is an alias of A:" % DIRBYTES)

    # Names the session created, one per drive.  Where they ARE is the proof.
    for who, nm, lo, hi, olo, ohi in (
            ("B:", "BNEW.TXT", blo, bhi, alo, ahi),
            ("A:", "AONLY.TXT", alo, ahi, blo, bhi),
            ("B:", "BONLY.TXT", blo, bhi, alo, ahi)):
        hits = occurrences(img, name11(nm))
        inside = [o for o in hits if lo <= o < hi]
        outside = [o for o in hits if not (lo <= o < hi)]
        c.check(len(inside) > 0, "%-11s appears inside drive %s" % (nm, who),
                "not found in [%d,%d); found at %s" % (lo, hi, hits))
        c.check(len(outside) == 0,
                "%-11s appears NOWHERE else on the disk" % nm,
                "also at %s (drive %s region is [%d,%d))"
                % (outside, "A:" if who == "B:" else "B:", olo, ohi))

    packed = open(packedb, 'rb').read()
    c.check(img[blo:bhi] != packed,
            "drive B:'s region changed under the session",
            "byte-identical to %s: nothing written on B: landed here"
            % packedb)
    c.check(img[blo:blo + DIRBYTES] != packed[:DIRBYTES],
            "drive B:'s directory on disk gained the new entry",
            "B:'s directory is still the packed one")

    print("driveb-check: %s" % ("PASS" if c.fails == 0 else
                                "FAIL (%d checks)" % c.fails))
    return 1 if c.fails else 0


if __name__ == '__main__':
    sys.exit(main())
