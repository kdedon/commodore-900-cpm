#!/usr/bin/env python3
"""bipatch.py -- write a bootinfo HANDOFF into a copy of cpm.sys.

WHAT THIS IS, AND WHAT IT IS NOT.  This is not a second implementation of
the handoff; it is a stand-in for one loader field that no loader in this
tree writes yet.  kboot fills the block while the image is staged, and it
fills down to the version the KERNEL declared (kboot src/bmain.c bifill).
`bi_serial' is version 4, and src/bios/bootinfo.h BI_ASK still asks for 3
because a kboot that does not know version 4 refuses to fill a block that
asks for it -- and then will not boot a medium that wanted a partition
table at all.  Until the v4 kboot is installed, this script is the only
way to put a serial map in front of the BIOS, so `make verify-conN' can
say whether the console table is sized from the map or from a constant.

It therefore writes exactly what a v4 kboot would write and nothing else:
the same magic, the same field order, the same "sixteen-bit words sum to
zero" checksum, and BI_SRC_KBOOT.  The BIOS cannot tell the difference,
which is the point -- and when the real thing lands, this test's medium
becomes the ordinary case and the script goes.

Usage:
  bipatch.py --in=cpm.sys --out=patched.sys --serial=0x7 \\
             --part8=BASE,COUNT [--part9=BASE,COUNT]

The block is patched in the FILE, before the medium is built, because the
file is contiguous and the filesystem's copy of it need not be.
"""

import argparse
import struct
import sys

MAGIC = b'KBOOTPTB'

# Field offsets from the magic, struct bootinfo (src/bios/bootinfo.h) as
# this compiler lays it out -- 16-bit shorts, 32-bit longs, 2-byte
# alignment.  Checked against the block cpm.sys is built with: the
# version there reads 3 and the length 160, which is BI_LEN3.
O_VERSION = 8
O_LEN = 10
O_NPART = 12
O_SUM = 14
O_SRC = 16
O_PART = 28			# 16 x { unsigned long bstart, bcount }
O_FLAGS = 156
O_CONSOLE = 158
O_SERIAL = 160
BI_LEN3 = 160
BI_LEN4 = 162
BI_NPART = 16
BI_SRC_KBOOT = 1


def extent(s):
    base, count = s.split(',')
    return int(base, 0), int(count, 0)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--in', dest='inp', required=True)
    ap.add_argument('--out', dest='out', required=True)
    ap.add_argument('--serial', required=True,
                    help='bi_serial bitmap, e.g. 0x7 for channels 0,1,2')
    ap.add_argument('--part8', required=True, type=extent,
                    help='BASE,COUNT of drive A: in 512-byte blocks')
    ap.add_argument('--part9', type=extent,
                    help='BASE,COUNT of drive B:')
    a = ap.parse_args()

    d = bytearray(open(a.inp, 'rb').read())
    i = d.find(MAGIC)
    if i < 0:
        sys.exit('bipatch: no bootinfo block in %s' % a.inp)
    if d.find(MAGIC, i + 1) >= 0:
        sys.exit('bipatch: more than one bootinfo block in %s' % a.inp)
    have = struct.unpack_from('>H', d, i + O_LEN)[0]
    if have not in (BI_LEN3, BI_LEN4):
        sys.exit('bipatch: %s declares bi_len %d, which is neither BI_LEN3 '
                 '(%d) nor BI_LEN4 (%d) -- the struct has moved and this '
                 'script must be rechecked against bootinfo.h'
                 % (a.inp, have, BI_LEN3, BI_LEN4))

    struct.pack_into('>H', d, i + O_VERSION, 4)
    struct.pack_into('>H', d, i + O_LEN, BI_LEN4)
    struct.pack_into('>H', d, i + O_NPART, BI_NPART)
    struct.pack_into('>H', d, i + O_SRC, BI_SRC_KBOOT)
    struct.pack_into('>H', d, i + O_FLAGS, 0)
    struct.pack_into('>H', d, i + O_CONSOLE, 0)
    struct.pack_into('>H', d, i + O_SERIAL, int(a.serial, 0))
    for slot in range(BI_NPART):
        struct.pack_into('>LL', d, i + O_PART + 8 * slot, 0, 0)
    struct.pack_into('>LL', d, i + O_PART + 8 * 8, *a.part8)
    if a.part9:
        struct.pack_into('>LL', d, i + O_PART + 8 * 9, *a.part9)

    # The checksum, computed the way bisum() does: the 16-bit words of the
    # first bi_len bytes must sum to zero.
    struct.pack_into('>H', d, i + O_SUM, 0)
    s = sum(struct.unpack_from('>%dH' % (BI_LEN4 // 2), d, i)) & 0xffff
    struct.pack_into('>H', d, i + O_SUM, (-s) & 0xffff)
    s = sum(struct.unpack_from('>%dH' % (BI_LEN4 // 2), d, i)) & 0xffff
    assert s == 0, 'bipatch: checksum did not close'

    open(a.out, 'wb').write(bytes(d))
    print('bipatch: %s -> %s: v4 handoff, bi_serial=%s, A: at %d+%d'
          % (a.inp, a.out, a.serial, a.part8[0], a.part8[1]))


main()
