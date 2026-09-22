#!/usr/bin/env python3
# Copyright (c) 2026 Kevin Dedon.
# SPDX-License-Identifier: MIT

"""gencpm.py -- stamp operator settings into a linked system image.

    gencpm.py gencpm.dat build/cpm.sys
    gencpm.py --dump build/cpm.sys

The settings are data bytes, so a dist is made by patching the linked
image rather than recompiling the BDOS.  Each one is found through the
image's own symbol table.

Hardware facts -- memory map, bank layout, drive geometry -- are not
settings: they have one value on this machine and live in c900cfg.h with
the reasoning attached.  This tool configures only what an operator
chooses: the serial number, the drive a reset selects, and per-drive
directory hashing.
"""

import struct
import sys

L_SHRI, L_PRVI, L_BSSI, L_SHRD, L_PRVD, L_BSSD, L_DEBUG, L_SYM, L_REL = range(9)
SYMSZ = 22			# ldsym: 16 name + 2 type + 4 addr
HDRSZ = 48
DBASE = 0x31000000		# SYSDSEG: the data segment's link address

# symbol -> width, in the order --dump prints them
FIELDS = [("serial_", 6), ("dflt_drive_", 1), ("hashen_", 2)]


def die(msg):
    sys.exit("gencpm: %s" % msg)


def gl(b, o):
    """One PDP-order (high word first, each word little-endian) long."""
    return (struct.unpack_from("<H", b, o)[0] << 16) | \
        struct.unpack_from("<H", b, o + 2)[0]


def offsets(b, path):
    """File offset of each field, from the image's symbol table."""
    if len(b) < HDRSZ:
        die("%s: too short for an l.out header" % path)
    magic, flag, machine, tbase = struct.unpack_from("<hhhh", b, 0)
    if magic != 0o407:
        die("%s: bad l.out magic 0%o" % (path, magic))
    ssize = [gl(b, 8 + 4 * i) for i in range(9)]

    # Segments follow the header in file order; bss holds no bytes.
    doff = tbase + ssize[L_SHRI] + ssize[L_PRVI]
    dlen = ssize[L_SHRD] + ssize[L_PRVD]
    symoff = doff + dlen + ssize[L_DEBUG]

    syms = {}
    for i in range(ssize[L_SYM] // SYMSZ):
        o = symoff + i * SYMSZ
        nm = b[o:o + 16].split(b"\0")[0].decode("ascii", "replace")
        syms[nm] = gl(b, o + 18)

    out = []
    for nm, width in FIELDS:
        if nm not in syms:
            die("%s: %s is not in the symbol table" % (path, nm))
        a = syms[nm] - DBASE
        if a < 0 or a + width > dlen:
            die("%s: %s is at 0x%08x, outside the data segment"
                % (path, nm, syms[nm]))
        out.append((nm, doff + a, width))
    return out


def readcfg(path):
    """key = value lines; # comments."""
    cfg = {}
    try:
        lines = open(path).readlines()
    except IOError as e:
        die("%s: %s" % (path, e.strerror))
    for n, line in enumerate(lines, 1):
        line = line.split("#")[0].strip()
        if not line:
            continue
        if "=" not in line:
            die("%s:%d: not a `key = value' line" % (path, n))
        k, v = line.split("=", 1)
        cfg[k.strip()] = v.strip()
    return cfg


def onoff(cfg, key):
    v = cfg[key].lower()
    if v not in ("on", "off"):
        die("%s: expected on or off, got `%s'" % (key, v))
    return 1 if v == "on" else 0


KEYS = ("serial", "default_drive", "hash_a", "hash_b")


def bytes_for(cfg, path):
    """Config values as the bytes each field holds."""
    # Every setting is named, none defaulted: a misspelt key would
    # otherwise ship a value nobody chose.
    for k in sorted(set(cfg) - set(KEYS)):
        die("%s: `%s' is not a setting" % (path, k))
    for k in KEYS:
        if k not in cfg:
            die("%s: `%s' is missing" % (path, k))

    serial = cfg["serial"]
    if len(serial) != 6:
        die("serial: expected 6 characters, got %d" % len(serial))
    drive = cfg["default_drive"].upper()
    if drive not in ("A", "B"):
        die("default_drive: expected A or B, got `%s'" % drive)
    return {
        "serial_": serial.encode("ascii", "replace"),
        "dflt_drive_": bytes([ord(drive) - ord("A")]),
        "hashen_": bytes([onoff(cfg, "hash_a"), onoff(cfg, "hash_b")]),
    }


def main():
    av = sys.argv[1:]
    if len(av) == 2 and av[0] == "--dump":
        b = open(av[1], "rb").read()
        for nm, off, width in offsets(b, av[1]):
            print("%-12s %6d bytes at file offset 0x%x  %s"
                  % (nm, width, off, b[off:off + width].hex()))
        return
    if len(av) != 2:
        die("usage: gencpm.py gencpm.dat cpm.sys | gencpm.py --dump cpm.sys")

    cfgf, sysf = av
    vals = bytes_for(readcfg(cfgf), cfgf)
    b = bytearray(open(sysf, "rb").read())
    moved = []
    for nm, off, width in offsets(bytes(b), sysf):
        if b[off:off + width] != vals[nm]:
            moved.append(nm)
        b[off:off + width] = vals[nm]
    open(sysf, "wb").write(b)
    # The report is the build's evidence that a default build ships the
    # values its sources were compiled with.
    print("gencpm: %s %s from %s"
          % (sysf, ("changed " + " ".join(moved)) if moved else "unchanged",
             cfgf))


main()
