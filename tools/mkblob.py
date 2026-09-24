#!/usr/bin/env python3
# Copyright (c) 2026 Kevin Dedon.
# SPDX-License-Identifier: MIT

"""mkblob.py -- turn the linked modules into an assembler source file
and an image blob, then lay the blob into CPM.SYS.

    mkblob.py modimg.s NAME SEG FILE 'EXPORTS' [NAME SEG FILE 'EXPORTS' ...]
    mkblob.py --place LINKED modimg.bin CPM.SYS

Each FILE is an l.out linked alone at SEG<<24.  Its loaded image goes
into modimg.bin, and cmain.c copies it into physical page MODPHYSPAGE
at cold boot.  Images are packed there on 256-byte boundaries, and each
segment is mapped onto its own slice with a limit that ends it.

--place loads the blob over the start of CPM.SYS's bss, which nothing
touches before cold boot has copied the images out and zeroed it:

    linked   | data | bss ............ |
    CPM.SYS  | data | blob ....... | bss' |    (bss' = what the blob
                                                  leaves uncovered)

So the images cost resident memory only where they outgrow bss, and
only until cold boot.

The module's EXPORTS become absolute symbols, taken from the same link
as the image, so CPM.SYS calls or names them like any other global.  A
Z8001 segmented CALL carries the full address and RET pops it, so
crossing into a module needs nothing more.
"""

import os
import re
import struct
import sys

L_SHRI, L_PRVI, L_BSSI, L_SHRD, L_PRVD, L_BSSD, L_DEBUG, L_SYM, L_REL = range(9)
SYMSZ = 22			# ldsym: 16 name + 2 type + 4 addr


def die(msg):
    sys.exit("mkblob: %s" % msg)


def gl(b, o):
    """One PDP-order (high word first, each word little-endian) long."""
    return (struct.unpack_from("<H", b, o)[0] << 16) | \
        struct.unpack_from("<H", b, o + 2)[0]


def cfg(name):
    txt = open("src/bios/c900cfg.h").read()
    m = re.search(r"#define\s+%s\s+(0x[0-9a-fA-F]+|\d+)" % name, txt)
    if not m:
        die("c900cfg.h defines no %s" % name)
    return int(m.group(1), 0)


def load(name, seg, modf):
    b = open(modf, "rb").read()
    if len(b) < 48:
        die("%s: too short for an l.out header" % modf)
    magic, flag, machine, tbase = struct.unpack_from("<hhhh", b, 0)
    if magic != 0o407:
        die("%s: bad l.out magic 0%o" % (modf, magic))
    ssize = [gl(b, 8 + 4 * i) for i in range(9)]

    # The image is copied as one block, so the loaded layout has to be
    # SHRI then PRVD with nothing between them.  ld's baseall() puts
    # PRVI, BSSI and SHRD there when they are non-empty, and BSSI in
    # particular would land in the middle of what we copy.
    for s, sn in ((L_PRVI, "PRVI"), (L_BSSI, "BSSI"), (L_SHRD, "SHRD")):
        if ssize[s]:
            die("%s: %s is %d bytes; the module image must be SHRI+PRVD "
                "only (see the layout note in this script)"
                % (modf, sn, ssize[s]))

    imglen = ssize[L_SHRI] + ssize[L_PRVD]
    img = b[tbase:tbase + imglen]
    if len(img) != imglen:
        die("%s: file holds %d image bytes, header says %d"
            % (modf, len(img), imglen))

    syms = {}
    symoff = tbase + imglen + ssize[L_DEBUG]
    for i in range(ssize[L_SYM] // SYMSZ):
        o = symoff + i * SYMSZ
        nm = b[o:o + 16].split(b"\0")[0].decode("ascii", "replace")
        syms[nm] = gl(b, o + 18)
    return dict(name=name, seg=seg, img=img, text=ssize[L_SHRI],
                bss=ssize[L_BSSD], syms=syms)


def place(lnkf, binf, outf):
    b = open(lnkf, "rb").read()
    blob = open(binf, "rb").read()
    tbase = struct.unpack_from("<h", b, 6)[0]
    ssize = [gl(b, 8 + 4 * i) for i in range(9)]
    doff = tbase + ssize[L_SHRI] + ssize[L_PRVI]
    ds = ssize[L_SHRD] + ssize[L_PRVD]
    pad = ds & 1
    grow = pad + len(blob)

    syms = {}
    symoff = doff + ds + ssize[L_DEBUG]
    for i in range(ssize[L_SYM] // SYMSZ):
        o = symoff + i * SYMSZ
        nm = b[o:o + 16].split(b"\0")[0].decode("ascii", "replace")
        syms[nm] = gl(b, o + 18)
    if "modblob_" not in syms:
        die("%s: modblob_ is not in the symbol table" % lnkf)

    h = bytearray(b[:doff + ds])
    for s, n in ((L_PRVD, ssize[L_PRVD] + grow),
                 (L_BSSD, max(0, ssize[L_BSSD] - grow))):
        struct.pack_into("<HH", h, 8 + 4 * s, n >> 16, n & 0xFFFF)
    a = (syms["modblob_"] & 0xFFFFFF) + doff
    v = (syms["modblob_"] & 0xFF000000) + ds + pad
    struct.pack_into(">L", h, a, v)		# image bytes are big-endian
    open(outf, "wb").write(bytes(h) + b"\0" * pad + blob + b[doff + ds:])


def main():
    if sys.argv[1:2] == ["--place"] and len(sys.argv) == 5:
        return place(*sys.argv[2:])
    a = sys.argv[2:]
    if len(sys.argv) < 6 or len(a) % 4:
        die("usage: mkblob.py modimg.s NAME SEG FILE 'EXPORTS' ...")
    outf = sys.argv[1]
    top = (cfg("MODLIM") + 1) * 256
    mods, off = [], 0
    for i in range(0, len(a), 4):
        name, seg, modf, exps = a[i], int(a[i + 1], 0), a[i + 2], a[i + 3]
        m = load(name, seg, modf)
        m["off"] = off
        m["size"] = len(m["img"]) + m["bss"]
        m["blocks"] = (m["size"] + 255) // 256
        m["exps"] = exps.split()
        for nm in m["exps"]:
            if nm not in m["syms"]:
                die("%s: exported symbol %s is not defined" % (modf, nm))
        off += m["blocks"] * 256
        mods.append(m)
    if off > top:
        die("modules need %d bytes; page 0x%02X holds %d"
            % (off, cfg("MODPHYSPAGE"), top))

    page = cfg("MODPHYSPAGE") << 8
    blob = b""
    for m in mods:
        m["boff"] = len(blob)
        blob += m["img"] + b"\0" * (len(m["img"]) & 1)
    open(os.path.splitext(outf)[0] + ".bin", "wb").write(blob)
    with open(outf, "w") as f:
        f.write("/ Generated by tools/mkblob.py -- do not edit.\n"
                "/ The linked modules' exports and where their images go;\n"
                "/ --place sets modblob_ to where the images load.\n\n")
        for m in mods:
            for nm in m["exps"]:
                f.write("\t.globl\t%s\n%s = 0x%08x\n"
                        % (nm, nm, m["syms"][nm]))
        f.write("\n\t.prvd\n\t.even\n\n"
                "\t.globl\tmodblob_\nmodblob_:\n\t.long\t0\n"
                "\t.globl\tmodlen_\nmodlen_:\n\t.word\t%d\n"
                "/ seg, base page, limit, image length, bss length, offset\n"
                "\t.globl\tmodn_\nmodn_:\n\t.word\t%d\n"
                "\t.globl\tmodtab_\nmodtab_:\n" % (len(blob), len(mods)))
        for m in mods:
            f.write("\t.word\t0x%02x, 0x%04x, 0x%02x, %d, %d\n\t.long\t%d\n"
                    % (m["seg"], page + m["off"] // 256, m["blocks"] - 1,
                       len(m["img"]), m["bss"], m["boff"]))
    for m in mods:
        print("%s.mod: seg 0x%02x at phys 0x%06x, text %d + data %d + bss %d"
              " = %d bytes, %d free in its segment"
              % (m["name"], m["seg"], (page << 8) + m["off"], m["text"],
                 len(m["img"]) - m["text"], m["bss"], m["size"],
                 0x10000 - m["size"]))
    print("modules: %d of %d bytes in page 0x%02X"
          % (off, top, cfg("MODPHYSPAGE")))


main()
