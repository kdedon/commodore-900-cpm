#!/usr/bin/env python3
"""mkrsx.py -- turn a linked RSX module into the flat .RSX file RSXLDR
loads.

    mkrsx.py ucrsx.lout UCASE.RSX 0xF000

An RSX is a flat image, not an x.out: RSXLDR reads it record by record
into a buffer and hands the buffer to the system (BDOS function 60
sub-function 127), which copies it to the TPA offset the module names in
its own prefix.  There is no relocation anywhere in that path -- see
src/bdos/rsxhdr.h note 3 -- so the one thing that can silently go wrong is the
module being linked for an address other than the one its prefix claims.
That is what this script exists to catch: it takes the link address on
the command line, and every RSX in the build passes through it.

The prefix layout is src/bdos/rsxhdr.h, which is c900oses/cpm8000/ref/cpm3/getrsx.asm:124-137
with the jump fields turned into offsets.
"""

import struct
import sys

RSXMAGIC = 0x5253
RSXHDRLEN = 32

L_SHRI, L_PRVI, L_BSSI, L_SHRD, L_PRVD, L_BSSD, L_DEBUG, L_SYM, L_REL = range(9)


def die(msg):
    sys.exit("mkrsx: %s" % msg)


def gl(b, o):
    """One PDP-order (high word first, each word little-endian) long."""
    return (struct.unpack_from("<H", b, o)[0] << 16) | \
        struct.unpack_from("<H", b, o + 2)[0]


def main():
    if len(sys.argv) != 4:
        die("usage: mkrsx.py module.lout NAME.RSX linkaddr")
    modf, outf, org = sys.argv[1], sys.argv[2], int(sys.argv[3], 0)
    b = open(modf, "rb").read()
    if len(b) < 48:
        die("%s: too short for an l.out header" % modf)
    magic, flag, machine, tbase = struct.unpack_from("<hhhh", b, 0)
    if magic != 0o407:
        die("%s: bad l.out magic 0%o" % (modf, magic))
    ssize = [gl(b, 8 + 4 * i) for i in range(9)]

    # A module is one contiguous image with no bss: the system copies
    # exactly `len' bytes and nothing zeroes anything afterwards, so
    # storage a module writes to has to be in the image it ships.
    for seg, name in ((L_PRVI, "PRVI"), (L_BSSI, "BSSI"), (L_SHRD, "SHRD"),
                      (L_PRVD, "PRVD"), (L_BSSD, "BSSD")):
        if ssize[seg]:
            die("%s: %s is %d bytes; an RSX must be SHRI only -- it is "
                "copied as one block and its bss would not be cleared"
                % (modf, name, ssize[seg]))

    imglen = ssize[L_SHRI]
    img = b[tbase:tbase + imglen]
    if len(img) != imglen:
        die("%s: file holds %d image bytes, header says %d"
            % (modf, len(img), imglen))
    if imglen < RSXHDRLEN:
        die("%s: %d bytes is shorter than the %d-byte prefix"
            % (modf, imglen, RSXHDRLEN))

    (entry, hmagic, nxt, prev) = struct.unpack_from(">HHHH", img, 6)
    (horg, hlen) = struct.unpack_from(">HH", img, 0x1c)
    if hmagic != RSXMAGIC:
        die("%s: prefix magic is 0x%04x, expected 0x%04x (rsxhdr.h)"
            % (modf, hmagic, RSXMAGIC))
    if horg != org:
        die("%s: the prefix says it is linked for 0x%04x, the link address "
            "is 0x%04x -- the module would run at an address its own code "
            "does not believe in" % (modf, horg, org))
    if hlen != imglen:
        die("%s: the prefix says the module is %d bytes, the image is %d"
            % (modf, hlen, imglen))
    if not org <= entry < org + imglen:
        die("%s: the entry offset 0x%04x is outside the module"
            % (modf, entry))
    if nxt or prev:
        die("%s: next/prev must ship as zero; the loader owns them" % modf)

    open(outf, "wb").write(img)
    print("mkrsx: %s -> %s (%d bytes at TPA:0x%04x, entry 0x%04x)"
          % (modf, outf, imglen, org, entry))


main()
