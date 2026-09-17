#!/usr/bin/env python3
# Copyright (c) 2026 Kevin Dedon.
# SPDX-License-Identifier: MIT

"""payload-measure.py -- K2: payload measurement for the installer.

Prints three tables (as CSV-ish plain text, one line per measurement) that
answer the three questions in ROADMAP-2026-09.md's K2 row:

  (a) compressed size of build/cpma-rel.img, build/cpmb.img, and a freshly
      initialised A: image (directory all 0xE5, no files), under gzip -9
      and under a 12..16-bit LZW coder standing in for Unix `compress`
      (neither this host nor the target toolchain ships an ncompress
      binary -- checked: `which compress` fails, apt has no cached
      ncompress package, and there is no sudo here, so installing one
      would be the network-dependent step this task must not add).  The
      "compress" column comes from lzw_compress() below: a plain adaptive
      LZW coder, 9->16 bit codes, clear-on-full-table.  It omits
      ncompress's block-mode ratio-triggered early CLEAR, which only
      changes output size on adversarial input, not disk images.  It is
      NOT byte-identical to ncompress output; it answers the SIZE
      question, which is what R5-3 needs.

  (b) what writes the 0xE5 directory area for a NEW drive.  This does not
      run a search each time it is invoked; it states the answer this task
      found by reading src/cmd/ and src/bios/bios900.c, and says how that
      finding was checked.

  (c) the minimum file set A: must carry to boot to A> and run the DRI/v3
      utilities, with each file's size, read out of `mkcpmfs.py --list`
      against build/cpma-rel.img.

Usage:
    python3 tools/payload-measure.py

Everything this script needs (the two built images, tools/mkcpmfs.py) must
already exist; run `make all` first.  Re-run after any image or file-list
change to get fresh numbers without repeating the reasoning above.
"""
import gzip
import io
import os
import subprocess
import sys
import tempfile


def repo_root():
    here = os.path.dirname(os.path.abspath(__file__))
    return os.path.dirname(here)


# ---------------------------------------------------------------- LZW ----

def lzw_compress(data):
    """Adaptive LZW, 9..16 bit codes, LSB-first packing, clear code 256.
    Modeled on the classic Unix `compress` algorithm (Welch 1984) minus
    ncompress's block-mode ratio check -- see module docstring for why."""
    maxbits = 16
    clear_code = 256
    table = {bytes([i]): i for i in range(256)}
    next_code = 257
    bits = 9
    limit = 1 << bits

    out_bits = []

    def emit(code, width):
        out_bits.append((code, width))

    emit(clear_code, bits)
    w = b""
    for byte in data:
        c = bytes([byte])
        wc = w + c
        if wc in table:
            w = wc
            continue
        emit(table[w], bits)
        table[wc] = next_code
        next_code += 1
        if next_code > limit:
            if bits < maxbits:
                bits += 1
                limit = 1 << bits
            else:
                emit(clear_code, bits)
                table = {bytes([i]): i for i in range(256)}
                next_code = 257
                bits = 9
                limit = 1 << bits
        w = c
    if w:
        emit(table[w], bits)

    buf = 0
    nbits = 0
    out = bytearray()
    for code, width in out_bits:
        buf |= code << nbits
        nbits += width
        while nbits >= 8:
            out.append(buf & 0xFF)
            buf >>= 8
            nbits -= 8
    if nbits:
        out.append(buf & 0xFF)
    # .Z-style 3-byte header so the size is comparable to real `compress`
    # output (magic + max-bits/block-mode byte).
    return b"\x1f\x9d\x90" + bytes(out)


def gzip9_size(data):
    buf = io.BytesIO()
    with gzip.GzipFile(fileobj=buf, mode="wb", compresslevel=9, mtime=0) as f:
        f.write(data)
    return len(buf.getvalue())


def compress_size(data):
    return len(lzw_compress(data))


# ------------------------------------------------------------- part a ----

def make_empty_a_image(root, blocks=20480):
    """A freshly initialised A: image: mkcpmfs.py over an empty source
    directory, no --initdir, no --label -- directory all 0xE5, no files,
    data area zero (verified by hand: set(bytes) == {0xE5} over the 16 KB
    directory region, {0} over the rest)."""
    tmpdir = tempfile.mkdtemp(prefix="k2-emptyA-src-")
    imgfd, imgpath = tempfile.mkstemp(prefix="k2-emptyA-", suffix=".img")
    os.close(imgfd)
    os.remove(imgpath)
    subprocess.run(
        [sys.executable, os.path.join(root, "tools", "mkcpmfs.py"),
         imgpath, str(blocks), tmpdir],
        cwd=root, check=True, capture_output=True, text=True,
    )
    os.rmdir(tmpdir)
    return imgpath


def table_a(root):
    images = [
        ("build/cpma-rel.img (as built)",
         os.path.join(root, "build", "cpma-rel.img")),
        ("build/cpmb.img (as built)",
         os.path.join(root, "build", "cpmb.img")),
    ]
    empty_a = make_empty_a_image(root)
    images.append(("fresh A: (0xE5 dir, no files)", empty_a))

    rows = []
    for label, path in images:
        with open(path, "rb") as f:
            data = f.read()
        raw = len(data)
        gz = gzip9_size(data)
        cz = compress_size(data)
        rows.append((label, raw, gz, cz,
                     "%.1f%%" % (100.0 * gz / raw),
                     "%.1f%%" % (100.0 * cz / raw)))
    os.remove(empty_a)
    return rows


# ------------------------------------------------------------- part c ----

# The v3 utility set named in ROADMAP-2026-09.md section V5 ("the v3 set
# on the release disk is PIP, STAT, SDIR, SET, SHOW, DATE, INITDIR,
# SUBMIT, GENCOM, ED, SETDEF-equivalent") plus CCP.Z8K (the transient CCP
# itself, reloaded on warm boot -- ccprun.c/ccpgo.c) and DUMP.Z8K (native
# DUMP, V5's replacement for the one DRI v3 utility we don't carry).
# SETDEF-equivalent is the CCP's built-in PATH, not a file.
MINIMUM_SET = [
    "CCP.Z8K", "PIP.Z8K", "STAT.Z8K", "SDIR.Z8K", "SET.Z8K", "SHOW.Z8K",
    "DATE.Z8K", "INITDIR.Z8K", "SUBMIT.Z8K", "GENCOM.Z8K", "ED.Z8K",
    "DUMP.Z8K",
]


def table_c(root):
    img = os.path.join(root, "build", "cpma-rel.img")
    out = subprocess.run(
        [sys.executable, os.path.join(root, "tools", "mkcpmfs.py"),
         "--list", img],
        cwd=root, check=True, capture_output=True, text=True,
    ).stdout
    # Line layout: "user  NAME  SIZE  bytes  N  blocks  M  entries"
    sizes = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 4 and parts[0].isdigit() and parts[3] == "bytes":
            sizes[parts[1]] = int(parts[2])

    rows = []
    total = 0
    missing = []
    for name in MINIMUM_SET:
        if name in sizes:
            rows.append((name, sizes[name]))
            total += sizes[name]
        else:
            missing.append(name)
    return rows, total, missing


# ------------------------------------------------------------- part b ----

PART_B_ANSWER = """\
Nothing on the target creates a new drive; drives are fixed at BIOS build
time, not created at install/run time.

  - src/bios/bios900.c defines exactly two DPB/DPH pairs, `dpba` and
    `dpbb`, each with a compiled-in track offset (trk_off) and size (dsm);
    there is no third slot and no runtime path that allocates one.
  - src/cmd/initdir.c (INITDIR, ported from DRI's INITDIR.PLI) does NOT
    create a drive or blank a directory to 0xE5.  It reformats an
    ALREADY-FORMATTED directory in place, moving existing entries out of
    every 4th slot and turning that slot into a type-21h SFCB (CP/M 3
    date-stamping).  Its own countdir()/buildnew() pass assumes real
    entries or already-free (0xE5) slots are there; there is no code path
    that writes 0xE5 across a raw/garbage region.  tests/initdir-check.py
    checks exactly this transformation (unstamped -> stamped), not disk
    creation.
  - The only thing that ever writes a fresh 0xE5-filled directory area is
    the HOST packer, tools/mkcpmfs.py, at image-BUILD time (pack()/
    do_initdir()) -- confirmed directly: mkcpmfs.py with no --initdir and
    no --label, run over an empty source directory, produces a 10 MB
    image whose 16 KB directory region is byte-for-byte 0xE5 and whose
    data region is byte-for-byte 0x00 (checked with a one-off Python
    set()-over-bytes read; table_a()'s make_empty_a_image() reproduces the
    same image on every run of this script).

So: "create a drive on target" is not a thing this system can do today.
Shipping compressed pre-built images (mkcpmfs.py output, gzip'd) is the
only route that exists without new code; an on-target FORMAT/MKFS utility
that partitions free space and 0xE5-fills a new directory region would be
new work -- no existing src/cmd/ program does it, and it would also need a
new BIOS DPB/DPH slot mechanism instead of the current compiled-in pair,
which is a bigger change than writing the directory bytes.
"""


def fmt_table(rows, headers, widths):
    line = "  ".join(h.ljust(w) for h, w in zip(headers, widths))
    out = [line, "  ".join("-" * w for w in widths)]
    for row in rows:
        out.append("  ".join(str(c).ljust(w) for c, w in zip(row, widths)))
    return "\n".join(out)


def main():
    root = repo_root()
    for name in ("build/cpma-rel.img", "build/cpmb.img"):
        if not os.path.exists(os.path.join(root, name)):
            print("error: %s missing -- run `make all` first" % name,
                  file=sys.stderr)
            return 1

    print("=== Table A: payload compression, gzip -9 vs. compress (LZW) ===")
    rows_a = table_a(root)
    print(fmt_table(
        rows_a,
        ["image", "raw bytes", "gzip -9 bytes", "compress bytes",
         "gzip %", "compress %"],
        [32, 10, 14, 15, 8, 11],
    ))

    print()
    print("=== Table B: can a drive be created on the target? ===")
    print(PART_B_ANSWER)

    print("=== Table C: minimum A: file set to boot to A> "
          "and run the DRI/v3 utilities ===")
    rows_c, total, missing = table_c(root)
    print(fmt_table(rows_c, ["file", "bytes"], [16, 8]))
    print("  " + "-" * 8)
    print("  TOTAL %d bytes, %d files" % (total, len(rows_c)))
    if missing:
        print("  MISSING from build/cpma-rel.img: %s" % ", ".join(missing))
    return 0


if __name__ == "__main__":
    sys.exit(main())
