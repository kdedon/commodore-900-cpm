#!/usr/bin/env python3
# Copyright (c) 2026 Kevin Dedon.
# SPDX-License-Identifier: MIT

"""mksig.py -- fill a CP/M drive image with a recognizable signature.

usage: mksig.py <output> <blocks>

Writes <blocks> 512-byte physical sectors.  Each of the four 128-byte
logical records in a sector carries a 16-byte header:

    'CPMA' + be32(record number) + be32(~record number) + 4 zero bytes

with the rest of the record zero.  Record number = sector*4 + quarter.
The on-target read test (src/bios/cmain.c mkexp) recomputes this from the
record number alone, so every in-sector offset is independently
verifiable.  Deterministic: no timestamps, no randomness.
"""
import struct
import sys


def main():
    out = sys.argv[1]
    blocks = int(sys.argv[2])
    with open(out, "wb") as f:
        for sec in range(blocks):
            buf = bytearray(512)
            for q in range(4):
                rec = sec * 4 + q
                buf[q * 128:q * 128 + 12] = b"CPMA" + struct.pack(
                    ">II", rec, ~rec & 0xFFFFFFFF)
            f.write(buf)


if __name__ == "__main__":
    main()
