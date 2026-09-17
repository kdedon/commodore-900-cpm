#!/usr/bin/env python3
# Copyright (c) 2026 Kevin Dedon.
# SPDX-License-Identifier: MIT

"""extract-test.py - host tests for tools/mkcpmfs.py --extract against
directory entries that are hostile or merely awkward.

A filename decoded out of a CP/M directory is UNTRUSTED INPUT: the eleven
name bytes are whatever is on the medium, and --extract turns them into a
host path.  These tests build images whose directory says something a
well-behaved CP/M would never say, and check what lands on the host DISK --
not what the tool prints or returns.

  leg (a)  an entry named `../OUT.TXT' must not write outside the
           destination directory; the file it aims at must be untouched.
  leg (b)  a sparse file (allocation list 0,5) must place block 5's data at
           the offset its slot names, not compacted to the file's start.
  leg (c)  with EXM=1 a file over 16 KiB has raw extent 1 in its FIRST
           directory entry, so its timestamp and password mode must still be
           found there.

Every image is built fresh in the work directory; nothing outside it is
touched -- which is the point of leg (a), and the test fails if that is
untrue.

Usage: extract-test.py <workdir>
"""

import datetime
import importlib.util
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
MKCPMFS = os.path.join(HERE, '..', 'tools', 'mkcpmfs.py')

BLS = 4096
ENTSIZE = 32
NENT = 512
DIRBLKS = 4
RECLEN = 128
T_FREE = 0xe5
T_SFCB = 0x21
T_XFCB = 0x10

NBLOCKS = 64                    # 256 KB image: 4 directory + 60 data blocks

WORK = None
failures = []
checks = 0


def check(cond, what):
    global checks
    checks += 1
    if not cond:
        failures.append(what)
        print("FAIL  %s" % what)
    else:
        print("ok    %s" % what)


def mk(*args):
    """Run mkcpmfs.py; returns (rc, combined output).  Never asserts rc."""
    p = subprocess.run([sys.executable, MKCPMFS] + list(args),
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    return p.returncode, p.stdout.decode('utf-8', 'replace')


def path(*parts):
    return os.path.join(WORK, *parts)


# ---- image construction ------------------------------------------------------

def blank_image():
    """A directory-only image: 4 directory blocks of 0xE5, then zero data."""
    img = bytearray(NBLOCKS * BLS)
    for i in range(DIRBLKS * BLS):
        img[i] = T_FREE
    return img


def put_entry(img, i, user, name11, ex, s2, rc, al):
    """Write one file FCB at directory index i.  al is 8 block numbers."""
    e = bytearray(ENTSIZE)
    e[0] = user
    e[1:12] = name11
    e[12] = ex
    e[14] = s2
    e[15] = rc
    for j, b in enumerate(al):
        e[16 + 2 * j] = b & 0xff
        e[17 + 2 * j] = (b >> 8) & 0xff
    img[i * ENTSIZE:(i + 1) * ENTSIZE] = e


def put_stamp(img, i, create, update, pwmode):
    """Stamp entry i via the SFCB of its four-entry group (index i|3)."""
    s = i & 3
    assert s != 3
    sfcb = i | 3
    e = bytearray(img[sfcb * ENTSIZE:(sfcb + 1) * ENTSIZE])
    if e[0] != T_SFCB:
        e = bytearray(ENTSIZE)
        e[0] = T_SFCB
    e[1 + 10 * s:1 + 10 * s + 4] = create
    e[1 + 10 * s + 4:1 + 10 * s + 8] = update
    e[1 + 10 * s + 8] = pwmode
    img[sfcb * ENTSIZE:(sfcb + 1) * ENTSIZE] = e


def put_xfcb(img, i, user, name11, mode):
    e = bytearray(ENTSIZE)
    e[0] = T_XFCB + user
    e[1:12] = name11
    e[12] = mode
    img[i * ENTSIZE:(i + 1) * ENTSIZE] = e


def fill_block(img, b, byte):
    img[b * BLS:(b + 1) * BLS] = bytes([byte]) * BLS


def write_image(name, img):
    p = path(name)
    with open(p, 'wb') as f:
        f.write(bytes(img))
    return p


def stamp4(when):
    """4-byte CP/M stamp: LE days since 1977-12-31, then BCD hh, mm."""
    epoch = datetime.date(1977, 12, 31)
    days = (when.date() - epoch).days
    bcd = lambda v: ((v // 10) << 4) | (v % 10)
    return bytes([days & 0xff, (days >> 8) & 0xff,
                  bcd(when.hour), bcd(when.minute)])


# ---- leg (a): the decoded name is a path ------------------------------------

def leg_a_escape():
    """`../OUT.TXT' must not reach outside the destination directory."""
    img = blank_image()
    fill_block(img, 4, ord('E'))
    # 8 name bytes + 3 extension bytes decode to `../OUT.TXT'
    put_entry(img, 0, 0, b'../OUT  TXT', 0, 0, 1, [4, 0, 0, 0, 0, 0, 0, 0])
    imgpath = write_image('escape.img', img)

    outside = path('OUT.TXT')
    with open(outside, 'wb') as f:
        f.write(b'SENTINEL')
    dest = path('escape-dest')
    if os.path.isdir(dest):
        shutil.rmtree(dest)

    rc, out = mk('--extract', imgpath, dest)

    # The OBJECT, not the return code: is the outside file still the sentinel?
    with open(outside, 'rb') as f:
        after = f.read()
    check(after == b'SENTINEL',
          "escape: %s untouched by --extract (is %r)" % (outside, after[:16]))
    # And nothing at all may appear beside the destination directory.
    stray = sorted(n for n in os.listdir(WORK)
                   if n not in ('escape.img', 'OUT.TXT', 'escape-dest'))
    check(not stray, "escape: no stray files in the work directory (%s)" % stray)
    check(rc != 0, "escape: --extract reports failure (rc %d)" % rc)
    check('Traceback' not in out, "escape: refusal is a diagnostic, not a crash")

    # A well-formed name in the same image still extracts, so the check is a
    # validation and not a blanket refusal.
    img2 = blank_image()
    fill_block(img2, 4, ord('E'))
    put_entry(img2, 0, 0, b'GOOD    TXT', 0, 0, 1, [4, 0, 0, 0, 0, 0, 0, 0])
    good = write_image('good.img', img2)
    dest2 = path('good-dest')
    rc2, out2 = mk('--extract', good, dest2)
    check(rc2 == 0, "escape: a well-formed name still extracts (rc %d)\n%s"
          % (rc2, out2))
    check(os.path.isfile(os.path.join(dest2, 'GOOD.TXT')),
          "escape: GOOD.TXT written inside the destination")


def leg_a_absolute():
    """An absolute decoded name must not be joined away to the host root."""
    img = blank_image()
    fill_block(img, 4, ord('A'))
    put_entry(img, 0, 0, b'/OUT    TXT', 0, 0, 1, [4, 0, 0, 0, 0, 0, 0, 0])
    imgpath = write_image('abs.img', img)
    dest = path('abs-dest')
    rc, out = mk('--extract', imgpath, dest)
    check(not os.path.exists('/OUT.TXT'),
          "absolute: nothing written at /OUT.TXT")
    check(rc != 0, "absolute: --extract reports failure (rc %d)" % rc)
    check('Traceback' not in out,
          "absolute: refusal is a diagnostic, not a crash\n%s" % out)


def leg_a_separator():
    """A name with an interior separator must not create a subdirectory path."""
    img = blank_image()
    fill_block(img, 4, ord('S'))
    put_entry(img, 0, 0, b'SUB/X   TXT', 0, 0, 1, [4, 0, 0, 0, 0, 0, 0, 0])
    imgpath = write_image('sep.img', img)
    dest = path('sep-dest')
    rc, out = mk('--extract', imgpath, dest)
    check(not os.path.exists(os.path.join(dest, 'SUB')),
          "separator: no SUB/ path created under the destination")
    check(rc != 0, "separator: --extract reports failure (rc %d)" % rc)


# ---- leg (b): a zero allocation slot is a hole, not an absence --------------

def leg_b_sparse():
    """Allocation list 0,5: block 5's data belongs at offset 4096."""
    img = blank_image()
    fill_block(img, 5, ord('B'))
    # 64 records = 8192 bytes = two blocks; the first is a hole.
    put_entry(img, 0, 0, b'SPARSE  DAT', 0, 0, 64, [0, 5, 0, 0, 0, 0, 0, 0])
    imgpath = write_image('sparse.img', img)
    dest = path('sparse-dest')
    if os.path.isdir(dest):
        shutil.rmtree(dest)
    rc, out = mk('--extract', imgpath, dest)
    check(rc == 0, "sparse: --extract succeeds (rc %d)\n%s" % (rc, out))
    p = os.path.join(dest, 'SPARSE.DAT')
    if not os.path.isfile(p):
        check(False, "sparse: SPARSE.DAT extracted")
        return
    with open(p, 'rb') as f:
        got = f.read()
    want = bytes(BLS) + bytes([ord('B')]) * BLS
    check(len(got) == len(want),
          "sparse: extracted size %d (want %d)" % (len(got), len(want)))
    check(got == want,
          "sparse: the hole stays at offset 0 and block 5 lands at 4096 "
          "(first 4 bytes %r, bytes at 4096 %r)" % (got[:4], got[4096:4100]))


def leg_b_trailing_hole():
    """A hole between two allocated blocks displaces both following blocks."""
    img = blank_image()
    fill_block(img, 6, ord('1'))
    fill_block(img, 8, ord('2'))
    # 96 records = 12288 bytes = three blocks: data, hole, data.
    put_entry(img, 0, 0, b'HOLE    DAT', 0, 0, 96, [6, 0, 8, 0, 0, 0, 0, 0])
    imgpath = write_image('hole.img', img)
    dest = path('hole-dest')
    rc, out = mk('--extract', imgpath, dest)
    check(rc == 0, "hole: --extract succeeds (rc %d)\n%s" % (rc, out))
    p = os.path.join(dest, 'HOLE.DAT')
    if not os.path.isfile(p):
        check(False, "hole: HOLE.DAT extracted")
        return
    with open(p, 'rb') as f:
        got = f.read()
    want = (bytes([ord('1')]) * BLS + bytes(BLS) + bytes([ord('2')]) * BLS)
    check(got == want,
          "hole: middle block stays a hole (bytes at 4096 %r, at 8192 %r)"
          % (got[4096:4100], got[8192:8196]))


# ---- leg (c): with EXM=1 the first entry of a big file has raw extent 1 -----

BIG_CREATE = datetime.datetime(2026, 9, 12, 10, 30)
BIG_UPDATE = datetime.datetime(2026, 9, 12, 11, 45)
BIG_PWMODE = 0x8c


def big_image():
    """A 20480-byte file whose only directory entry carries raw extent 1."""
    img = blank_image()
    for b in range(6, 11):
        fill_block(img, b, ord('C'))
    # EXM=1: one entry maps two logical extents, and BDOS records the LAST
    # logical extent written in EX.  160 records = 20480 bytes > 16 KiB, so
    # EX is 1 and RC counts the records past the 16 KiB boundary.
    put_entry(img, 4, 0, b'BIG     DAT', 1, 0, 160 - 128,
              [6, 7, 8, 9, 10, 0, 0, 0])
    put_stamp(img, 4, stamp4(BIG_CREATE), stamp4(BIG_UPDATE), BIG_PWMODE)
    put_xfcb(img, 8, 0, b'BIG     DAT', BIG_PWMODE)
    return img


def leg_c_stamp_object():
    """stamp_of() must return the sub-record: the stamp AND the pw mode."""
    spec = importlib.util.spec_from_file_location('mkcpmfs', MKCPMFS)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    imgpath = write_image('big.img', big_image())
    img, files = m.read_dir(imgpath)
    dirbuf = img[0:m.DIRBLKS * m.BLS]
    keys = sorted(files)
    check(len(keys) == 1, "big: one file in the directory (%d)" % len(keys))
    if not keys:
        return
    sub = m.stamp_of(dirbuf, files, keys[0])
    check(sub is not None,
          "big: stamp_of finds the sub-record of the raw-extent-1 first entry")
    if sub is None:
        return
    check(m.decode_stamp(sub[0:4]) == BIG_CREATE.strftime('%Y-%m-%d %H:%M'),
          "big: create stamp is %s" % m.decode_stamp(sub[0:4]))
    check(m.decode_stamp(sub[4:8]) == BIG_UPDATE.strftime('%Y-%m-%d %H:%M'),
          "big: update stamp is %s" % m.decode_stamp(sub[4:8]))
    check(sub[8] == BIG_PWMODE,
          "big: password mode is 0x%02x (want 0x%02x)" % (sub[8], BIG_PWMODE))
    check(m.file_size(files[keys[0]]) == 20480,
          "big: size is %d bytes" % m.file_size(files[keys[0]]))


def leg_c_list():
    """--list must print the big file's stamps."""
    imgpath = write_image('big.img', big_image())
    rc, out = mk('--list', imgpath)
    check(rc == 0, "big: --list succeeds (rc %d)\n%s" % (rc, out))
    line = [l for l in out.splitlines() if 'BIG.DAT' in l and 'password' not in l]
    check(len(line) == 1, "big: one BIG.DAT line in --list\n%s" % out)
    if len(line) != 1:
        return
    check(BIG_CREATE.strftime('%Y-%m-%d %H:%M') in line[0],
          "big: --list shows the create stamp: %s" % line[0].rstrip())
    check(BIG_UPDATE.strftime('%Y-%m-%d %H:%M') in line[0],
          "big: --list shows the update stamp: %s" % line[0].rstrip())


def leg_c_extract():
    """The big file's data must still come out whole."""
    imgpath = write_image('big.img', big_image())
    dest = path('big-dest')
    rc, out = mk('--extract', imgpath, dest)
    check(rc == 0, "big: --extract succeeds (rc %d)\n%s" % (rc, out))
    p = os.path.join(dest, 'BIG.DAT')
    if not os.path.isfile(p):
        check(False, "big: BIG.DAT extracted")
        return
    with open(p, 'rb') as f:
        got = f.read()
    check(got == bytes([ord('C')]) * 20480,
          "big: 20480 bytes of C extracted (got %d bytes)" % len(got))


def main():
    global WORK
    if len(sys.argv) != 2:
        sys.stderr.write(__doc__)
        return 2
    WORK = os.path.abspath(sys.argv[1])
    if os.path.isdir(WORK):
        shutil.rmtree(WORK)
    os.makedirs(WORK)

    leg_a_escape()
    leg_a_absolute()
    leg_a_separator()
    leg_b_sparse()
    leg_b_trailing_hole()
    leg_c_stamp_object()
    leg_c_list()
    leg_c_extract()

    print("%d checks, %d failures" % (checks, len(failures)))
    if failures:
        for f in failures:
            print("failed: %s" % f)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
