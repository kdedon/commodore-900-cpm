#!/usr/bin/env python3
# mkxout.py -- build the malformed x.out fixtures verify-xout feeds the
# program loader (src/bdos/pgmld.c).  Each fixture is derived from a GOOD
# binary so that everything except the one defect under test is real: the
# magic, the segment types and the code bytes are the linker's.
#
#   BADSEG.Z8K   the header's segment count says 17, one more than the
#                loader's sixteen-element seglim/segsiz/segloc/x_sg arrays.
#   HUGESEG.Z8K  the count says 1000: far past the arrays, so a loader
#                that believes it writes through the whole resident data
#                area rather than just off the end of one array.
#   TRUNCX.Z8K   the last 128-byte record is gone, so the declared
#                segment lengths run past the records that exist.  The
#                code segment is intact: a loader that accepts this file
#                RUNS the program, which is what the transcript looks for.
#
# The exit status is the answer: zero only if every fixture was written
# AND re-read as having exactly the defect it is meant to have.  Usage:
#
#   python3 tests/mkxout.py GOOD.Z8K OUTDIR

import os
import struct
import sys

SECLEN = 128
HDRLEN = 16
SGLEN = 4
NSEG = 16			# pgmld.c's array bound


def hdr(d):
    magic, nseg = struct.unpack('>hh', d[:4])
    return magic & 0xffff, nseg


def segs(d, n):
    return [struct.unpack('>bbH', d[HDRLEN + SGLEN * i:HDRLEN + SGLEN * (i + 1)])
            for i in range(n)]


def fail(msg):
    sys.stderr.write('mkxout: %s\n' % msg)
    sys.exit(1)


def write(path, data):
    with open(path, 'wb') as f:
        f.write(data)
    return data


def main():
    if len(sys.argv) != 3:
        fail('usage: mkxout.py GOOD.Z8K OUTDIR')
    src, out = sys.argv[1], sys.argv[2]
    good = open(src, 'rb').read()
    magic, nseg = hdr(good)
    if magic not in (0xEE01, 0xEE03, 0xEE07, 0xEE0B):
        fail('%s is not an executable x.out (magic %04X)' % (src, magic))
    if nseg < 1 or nseg > NSEG:
        fail('%s has %d segments; need a normal one to mutate' % (src, nseg))
    if not os.path.isdir(out):
        fail('%s is not a directory' % out)

    # --- the two bad counts.  Everything else is left alone, so the bytes
    # the loader then reads as surplus segment headers are the program's
    # own code: exactly what a malformed file hands it.
    for name, count in (('BADSEG.Z8K', NSEG + 1), ('HUGESEG.Z8K', 1000)):
        p = os.path.join(out, name)
        write(p, good[:2] + struct.pack('>h', count) + good[4:])
        m, n = hdr(open(p, 'rb').read())
        if m != magic or n != count:
            fail('%s did not come back with count %d' % (name, count))
        if n <= NSEG:
            fail('%s count %d does not exceed the %d-element arrays'
                 % (name, n, NSEG))

    # --- the truncated image.  Drop whole records only: CP/M files are
    # record-granular and a partial record would be invisible to the
    # loader, which sees EOF only at a record boundary.
    if len(good) <= SECLEN * 2:
        fail('%s is too short to truncate meaningfully' % src)
    keep = ((len(good) - 1) // SECLEN) * SECLEN	 # drop the last record
    p = os.path.join(out, 'TRUNCX.Z8K')
    write(p, good[:keep])
    back = open(p, 'rb').read()
    m, n = hdr(back)
    if m != magic or n != nseg:
        fail('TRUNCX.Z8K lost its header')
    if len(back) % SECLEN:
        fail('TRUNCX.Z8K is not a whole number of records')
    declared = HDRLEN + SGLEN * n + sum(s[2] for s in segs(back, n)
                                        if s[1] not in (1, 2))
    if declared <= len(back):
        fail('TRUNCX.Z8K declares %d bytes but holds %d: not truncated'
             % (declared, len(back)))
    # The code segment must survive whole, or "the loader ran it" is not
    # what the transcript would be showing.
    code = [s for s in segs(back, n) if s[1] in (3, 6, 7)]
    if not code:
        fail('TRUNCX.Z8K has no code segment')
    if HDRLEN + SGLEN * n + code[0][2] > len(back):
        fail('TRUNCX.Z8K lost part of its code segment')

    print('mkxout: BADSEG.Z8K (%d segs) HUGESEG.Z8K (1000 segs) '
          'TRUNCX.Z8K (%d of %d bytes, %d declared)'
          % (NSEG + 1, keep, len(good), declared))
    return 0


sys.exit(main())
