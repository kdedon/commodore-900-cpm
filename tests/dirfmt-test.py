#!/usr/bin/env python3
# Copyright (c) 2026 Kevin Dedon.
# SPDX-License-Identifier: MIT

"""dirfmt-test.py - agreement + backward-compatibility tests for the CP/M 3
directory format contract shared by this project's directory packer and
cpmtools, a third-party reader/writer of the same format.

Two implementations are exercised against the same images:
  mkcpmfs.py            the host packer/reader/initialiser
  cpmtools              cpmls, cpmcp, cpmrm and fsck.cpm (Michael Haardt's
                        suite, Debian package `cpmtools'), driven through the
                        c900a entry of tests/cpmtools/diskdefs

cpmtools runs with TZ=UTC: it converts CP/M's local-time stamps through the
host's current UTC offset, and UTC makes that the identity.

What cpmtools cannot express, and so is not checked against it:
  - per-file allocated blocks: cpmls reports sizes, not block counts.  The
    listings are compared on user, name and size, and the TOTAL allocation
    cpmls -D reports is compared with the sum of mkcpmfs's per-file blocks.
  - "rm clears the SFCB sub-record": cpmrm only marks the FCB 0xE5 and leaves
    the stamps behind, so that is not a property of this writer.
  - reporting an unknown entry type: cpmtools has no view of one; that such
    an entry survives cpmtools writes byte-identical is still checked.
  - the label's name: cpmtools keeps it in a [label] pseudo-file that no
    cpmls style prints and cpmcp's user-prefixed names cannot reach.
    fsck.cpm does check the label entry itself (mode bits, BCD stamps).
  - XFCBs as such: cpmtools 2.23 with `os 3' lists a type-1xh entry as a file
    of user 16-31.  Those rows are kept out of the file comparison and are
    what shows that cpmtools saw the XFCB.  fsck.cpm is not run on images
    with the planted XFCB and unknown-type entry: it rejects both, by design
    of those plants (non-printable password bytes, type 30h).

Run via `make dirfmt-check` from the repository root.  Every image is created
fresh in the work directory; nothing outside it is touched.

Usage: dirfmt-test.py <workdir> <cpmtools-bindir>
"""

import calendar
import datetime
import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
MKCPMFS = os.path.join(HERE, '..', 'tools', 'mkcpmfs.py')
DISKDEFS_DIR = os.path.join(HERE, 'cpmtools')     # cpmtools reads ./diskdefs
FORMAT = 'c900a'

BLOCKS = 20480
BLS = 4096
ENTSIZE = 32
NENT = 512
DIRBYTES = NENT * ENTSIZE

T_FREE = 0xe5
T_SFCB = 0x21
T_LABEL = 0x20

WORK = None
CPMTOOLS = None
failures = []
checks = 0


# ---- harness -----------------------------------------------------------------

def check(cond, what):
    global checks
    checks += 1
    if not cond:
        failures.append(what)
        print("FAIL  %s" % what)
    else:
        print("ok    %s" % what)


def run(argv, expect_ok=True, cwd=None, env=None):
    p = subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                       cwd=cwd, env=env)
    out = p.stdout.decode('utf-8', 'replace')
    if expect_ok and p.returncode != 0:
        failures.append("command failed: %s\n%s" % (' '.join(argv), out))
        print("FAIL  command %s\n%s" % (' '.join(argv), out))
    return p.returncode, out


def mk(*args):
    return run([sys.executable, MKCPMFS] + list(args))


def ct(tool, img, *args, expect_ok=True):
    """Run a cpmtools program on an image, from the diskdefs directory.
    Options must precede the image: cpmtools does not permute arguments."""
    env = dict(os.environ, TZ='UTC')
    opts = [a for a in args if a.startswith('-')]
    rest = [a for a in args if not a.startswith('-')]
    return run([os.path.join(CPMTOOLS, tool), '-f', FORMAT] + opts
               + [path(img)] + rest,
               expect_ok=expect_ok, cwd=DISKDEFS_DIR, env=env)


def fsck(img):
    env = dict(os.environ, TZ='UTC')
    return run([os.path.join(CPMTOOLS, 'fsck.cpm'), '-n', '-f', FORMAT,
                path(img)], expect_ok=False, cwd=DISKDEFS_DIR, env=env)


def path(name):
    return os.path.join(WORK, name)


def getdir(img):
    with open(path(img), 'rb') as f:
        return f.read(DIRBYTES)


def entries(d, pred):
    return [i for i in range(NENT) if pred(d[i * ENTSIZE])]


def is_ext(t):
    return t != T_FREE and t >= 0x10


def ext_image(d):
    """The extension entries as {index: bytes} -- what must never change."""
    return dict((i, d[i * ENTSIZE:(i + 1) * ENTSIZE])
                for i in range(NENT) if is_ext(d[i * ENTSIZE]))


def fcb_index(d, name11):
    return [i for i in range(NENT)
            if d[i * ENTSIZE] < 0x10
            and d[i * ENTSIZE + 1:i * ENTSIZE + 12] == name11][0]


def sub_record(d, i):
    """The 10-byte SFCB sub-record describing FCB i."""
    base = (i | 3) * ENTSIZE + 1 + 10 * (i & 3)
    return d[base:base + 10]


def srcdir(name, files):
    p = path(name)
    shutil.rmtree(p, ignore_errors=True)
    os.makedirs(p)
    for fn, data in files.items():
        with open(os.path.join(p, fn), 'wb') as f:
            f.write(data)
    return p


def hostfile(name, data, mtime=None):
    with open(path(name), 'wb') as f:
        f.write(data)
    if mtime is not None:
        os.utime(path(name), (mtime, mtime))
    return path(name)


# ---- listings ----------------------------------------------------------------
#
# Both sides are reduced to the same three things: file rows (user, NAME,
# bytes rounded up to a record), stamps {NAME: (create, update)} for files
# that carry any, and the number of allocation blocks the files occupy.

MKROW = re.compile(r'^\s*(\d+) (\S+)\s+(\d+) bytes\s+(\d+) blocks\s+'
                   r'\d+ entr\w+\s*(.*)$')
MKSTAMP = re.compile(r'\d{4}-\d\d-\d\d \d\d:\d\d|-')


def mklisting(out):
    rows, stamps, blocks = [], {}, 0
    for line in out.splitlines():
        m = MKROW.match(line)
        if not m:
            continue
        rows.append((int(m.group(1)), m.group(2), int(m.group(3))))
        blocks += int(m.group(4))
        s = MKSTAMP.findall(m.group(5))
        if len(s) == 2:
            stamps[m.group(2)] = tuple(s)
    return sorted(rows), stamps, blocks


MONTHS = dict((calendar.month_abbr[i], i) for i in range(1, 13))
CTDATE = re.compile(r'(\d\d)-(\w{3})-(\d{4}) (\d\d):(\d\d)')
CTUSED = re.compile(r'Files occupying\s+(\d+)K')


def ctdate(m):
    s = '%s-%02d-%s %s:%s' % (m.group(3), MONTHS[m.group(2)], m.group(1),
                              m.group(4), m.group(5))
    # day 0, the zero stamp, is 1977-12-31 00:00 to cpmtools
    return '-' if s == '1977-12-31 00:00' else s


def ctlisting(img):
    """cpmls's view.  The fourth item is the rows of users 16-31: cpmtools
    2.23 lists a type-1xh XFCB as a file of user 16-31, so those are kept
    apart from the file rows."""
    rows, xfcbs = [], []
    _, out = ct('cpmls', img, '-l')
    user = 0
    for line in out.splitlines():
        m = re.match(r'^(\d+):$', line)
        if m:
            user = int(m.group(1))
            continue
        f = line.split()
        if len(f) == 6 and f[1].isdigit():
            row = (user, f[5].upper(), int(f[1]))
            (rows if user < 16 else xfcbs).append(row)

    stamps, blocks = {}, None
    _, out = ct('cpmls', img, '-D')
    user = 0
    for line in out.splitlines():
        m = re.match(r'^User (\d+):', line)
        if m:
            user = int(m.group(1))
            continue
        m = CTUSED.search(line)
        if m:
            blocks = int(m.group(1)) * 1024 // BLS
            continue
        if len(line) < 12 or line[8] != '.' or user >= 16:
            continue
        name = line[:8].strip() + ('.' + line[9:12].strip()
                                   if line[9:12].strip() else '')
        d = [ctdate(x) for x in CTDATE.finditer(line)]
        if len(d) == 2 and d != ['-', '-']:
            stamps[name] = (d[1], d[0])       # -D prints update, create
    return sorted(rows), stamps, blocks, sorted(xfcbs)


def agree(img, what):
    """mkcpmfs --list and cpmls agree on files, allocation and stamps."""
    _, mout = mk('--list', path(img))
    mrows, mstamps, mblocks = mklisting(mout)
    crows, cstamps, cblocks, xfcbs = ctlisting(img)
    check(mrows == crows and mrows != [],
          "%s: mkcpmfs --list and cpmls agree on files" % what)
    check(mblocks == cblocks,
          "%s: both tools count the same allocated blocks (%s/%s)"
          % (what, mblocks, cblocks))
    return mout, mstamps, cstamps, xfcbs


# ---- tests -------------------------------------------------------------------

FILES = {
    'HELLO.TXT': b'hello from the format test\n',
    'BIG.DAT': bytes(range(256)) * 160,          # 40960 B: 2 dir entries
    'SMALL.BIN': b'\x01\x02\x03',
}


def t_unstamped_unchanged():
    """A plain pack is still a plain CP/M 2.2 directory: no extensions."""
    src = srcdir('src', FILES)
    mk(path('legacy.img'), str(BLOCKS), src)
    d = getdir('legacy.img')
    check(len(ext_image(d)) == 0,
          "legacy pack writes no extension entries")

    # both tools read it, and agree on the file set
    agree('legacy.img', "legacy image")

    # cpmtools round-trips it without inventing extensions
    ct('cpmcp', 'legacy.img', os.path.join(src, 'HELLO.TXT'), '0:COPY.TXT')
    ct('cpmrm', 'legacy.img', '0:SMALL.BIN')
    d = getdir('legacy.img')
    check(len(ext_image(d)) == 0,
          "legacy image stays extension-free through cpmcp/cpmrm")
    _, cout = ct('cpmls', 'legacy.img')
    check('copy.txt' in cout and 'small.bin' not in cout,
          "legacy image: cpmcp/cpmrm took effect")
    agree('legacy.img', "legacy image after cpmtools edits")


def t_initdir_layout():
    """--initdir produces exactly the contract's SFCB layout."""
    src = srcdir('src', FILES)
    mk('--initdir', '--label', 'TESTVOL', '--stamp-date', '2026-01-02T03:04',
       path('stamped.img'), str(BLOCKS), src)
    d = getdir('stamped.img')
    check(all(d[i * ENTSIZE] == T_SFCB for i in range(3, NENT, 4)),
          "--initdir: every 4th entry is a type-21h SFCB")
    check(all(d[i * ENTSIZE] != T_SFCB for i in range(NENT) if i % 4 != 3),
          "--initdir: no SFCB outside a 4th slot")
    check(all(d[i * ENTSIZE + 1:(i + 1) * ENTSIZE] == b'\0' * 31
              for i in range(3, NENT, 4)),
          "--initdir: fresh SFCBs are 21h + 31 zero bytes")
    labs = entries(d, lambda t: t == T_LABEL)
    check(len(labs) == 1, "--initdir --label: exactly one type-20h label")
    lab = d[labs[0] * ENTSIZE:(labs[0] + 1) * ENTSIZE]
    check(lab[1:12] == b'TESTVOL    ', "label carries its 8.3 name")
    check(lab[12] == 0x31, "label mode = exists|create|update (0x31)")
    check(lab[24:28] == lab[28:32] != b'\0\0\0\0',
          "a new label carries both of its own stamps")
    check(len(entries(d, lambda t: t < 0x10)) <= NENT - NENT // 4,
          "file entries fit in the 384 non-SFCB slots")

    rc, out = fsck('stamped.img')
    check(rc == 0 and 'Error' not in out,
          "fsck.cpm finds the stamped, labelled directory consistent")

    # data survives: extraction still matches the sources
    ex = path('ex-stamped')
    shutil.rmtree(ex, ignore_errors=True)
    mk('--extract', path('stamped.img'), ex)
    check(cmpfile(os.path.join(ex, 'BIG.DAT'), FILES['BIG.DAT']),
          "stamped image: extracted BIG.DAT matches the source")


def cmpfile(p, want):
    if not os.path.exists(p):
        return False
    with open(p, 'rb') as f:
        got = f.read()
    return got[:len(want)] == want and set(got[len(want):]) <= {0, 0x1a}


def t_initdir_relocates():
    """--initdir on an existing image moves 4th-slot entries out, losing nothing."""
    files = dict(('F%d.TXT' % i, ('file %d\n' % i).encode()) for i in range(1, 9))
    src = srcdir('src8', files)
    mk(path('reloc.img'), str(BLOCKS), src)
    before = getdir('reloc.img')
    occupied = entries(before, lambda t: t < 0x10)
    check(3 in occupied, "setup: an entry really does sit in a 4th slot")

    mk('--initdir', path('reloc.img'))
    d = getdir('reloc.img')
    check(all(d[i * ENTSIZE] == T_SFCB for i in range(3, NENT, 4)),
          "--initdir in place: 4th slots became SFCBs")
    names = sorted(bytes(d[i * ENTSIZE + 1:i * ENTSIZE + 12])
                   for i in entries(d, lambda t: t < 0x10))
    want = sorted(bytes(before[i * ENTSIZE + 1:i * ENTSIZE + 12])
                  for i in occupied)
    check(names == want, "--initdir in place: no file entry was lost")

    ex = path('ex-reloc')
    shutil.rmtree(ex, ignore_errors=True)
    mk('--extract', path('reloc.img'), ex)
    ok = all(cmpfile(os.path.join(ex, fn), data) for fn, data in files.items())
    check(ok, "--initdir in place: every relocated file still reads back")

    _, out = ct('cpmls', 'reloc.img')
    check(all(fn.lower() in out for fn in files),
          "cpmls sees the relocated files")
    cx = path('cx-reloc')
    shutil.rmtree(cx, ignore_errors=True)
    os.makedirs(cx)
    ct('cpmcp', 'reloc.img', '0:*.*', cx)
    ok = all(cmpfile(os.path.join(cx, fn.lower()), data)
             for fn, data in files.items())
    check(ok, "cpmcp reads every relocated file back")

    # idempotent
    d1 = getdir('reloc.img')
    mk('--initdir', path('reloc.img'))
    check(getdir('reloc.img') == d1, "--initdir is idempotent")


def t_preserve_through_cpmtools():
    """cpmcp/cpmrm must leave every extension entry but SFCBs byte-identical."""
    src = srcdir('src', FILES)
    mk('--initdir', '--label', 'PRESERVE', path('pres.img'), str(BLOCKS), src)
    inject_oddities('pres.img')
    base = ext_image(getdir('pres.img'))

    ct('cpmcp', 'pres.img',
       hostfile('extra.txt', b'a file written by cpmtools\n' * 40),
       '0:EXTRA.TXT')
    ct('cpmrm', 'pres.img', '0:SMALL.BIN')
    # cpmcp will not overwrite, so a rewrite is a remove and a copy
    ct('cpmrm', 'pres.img', '0:HELLO.TXT')
    ct('cpmcp', 'pres.img', os.path.join(src, 'HELLO.TXT'), '0:HELLO.TXT')

    after = ext_image(getdir('pres.img'))
    check(set(after) == set(base),
          "cpmcp/cpmrm: the same set of extension entries survives")
    # SFCB contents may change only in the sub-records of slots cpmtools
    # touched; the label, the XFCB and the unknown-type entry must be untouched.
    stable = [i for i in base if base[i][0] != T_SFCB]
    check(all(after[i] == base[i] for i in stable),
          "cpmcp/cpmrm: label, XFCB and unknown-type entries are byte-identical")
    check(after != base, "cpmcp did update SFCB stamps")

    mout, mstamps, cstamps, xfcbs = agree('pres.img', "stamped image")
    check(mstamps == cstamps and mstamps != {},
          "stamped image: both tools decode the SAME stamps")
    check('PRESERVE' in mout, "mkcpmfs shows the directory label")
    check('password' in mout and [r[:2] for r in xfcbs] == [(16, 'HELLO.TXT')],
          "both tools see the one XFCB, for user 0's HELLO.TXT")
    check('unknown' in mout,
          "mkcpmfs reports the unknown-type entry as preserved")


def inject_oddities(img):
    """Plant an XFCB and a type this contract does not define, to prove both
    tools carry unknown entries through untouched."""
    with open(path(img), 'r+b') as f:
        d = bytearray(f.read(DIRBYTES))
        free = [i for i in range(NENT)
                if d[i * ENTSIZE] == T_FREE and i % 4 != 3]
        x = bytearray(32)
        x[0] = 0x10                       # XFCB for user 0
        x[1:12] = b'HELLO   TXT'
        x[12] = 0x80                      # read password
        x[13] = 0x42
        x[16:24] = bytes(range(0x40, 0x48))
        d[free[0] * ENTSIZE:(free[0] + 1) * ENTSIZE] = x
        u = bytearray(32)
        u[0] = 0x30                       # not a type we define: keep verbatim
        u[1:12] = b'FUTURE  ???'
        d[free[1] * ENTSIZE:(free[1] + 1) * ENTSIZE] = u
        f.seek(0)
        f.write(d)


def t_rm_stamped():
    """Removing a stamped file frees its FCB and keeps its SFCB entry."""
    src = srcdir('src', FILES)
    mk('--initdir', '--label', 'STAMPS', path('rm.img'), str(BLOCKS), src)
    ct('cpmcp', 'rm.img', hostfile('one.txt', b'one\n'), '0:ONE.TXT')
    d = getdir('rm.img')
    i = fcb_index(d, b'ONE     TXT')
    check(sub_record(d, i) != b'\0' * 10, "cpmcp stamped the new file")
    _, mout = mk('--list', path('rm.img'))
    check('ONE.TXT' in mklisting(mout)[1],
          "mkcpmfs decodes the stamp cpmcp wrote")
    ct('cpmrm', 'rm.img', '0:ONE.TXT')
    d = getdir('rm.img')
    check(d[i * ENTSIZE] == T_FREE, "cpmrm freed the FCB")
    check(d[(i | 3) * ENTSIZE] == T_SFCB, "the SFCB entry itself survives rm")
    _, mout = mk('--list', path('rm.img'))
    check('ONE.TXT' not in mout,
          "mkcpmfs does not list the removed file or its stale stamp")


def t_cross_roundtrip():
    """A directory each tool wrote must survive the other tool untouched."""
    src = srcdir('src', FILES)
    mk('--initdir', '--label', 'XROUND', path('x.img'), str(BLOCKS), src)
    inject_oddities('x.img')

    # mkcpmfs-written directory -> read and rewritten by cpmtools
    d0 = getdir('x.img')
    ct('cpmls', 'x.img')                           # read-only: no change
    ct('cpmls', 'x.img', '-D')
    check(getdir('x.img') == d0, "cpmls does not modify the directory")
    mk('--list', path('x.img'))
    check(getdir('x.img') == d0, "mkcpmfs --list does not modify the directory")

    # cpmtools-written directory -> re-initialised by mkcpmfs, nothing disturbed
    ct('cpmcp', 'x.img', hostfile('two.txt', b'two\n' * 500), '0:TWO.TXT')
    d1 = getdir('x.img')
    mk('--initdir', path('x.img'))                 # already stamped: no-op
    check(getdir('x.img') == d1,
          "mkcpmfs --initdir over a cpmtools-written stamped directory is a no-op")

    # relabelling keeps every file entry and every stamp
    mk('--label', 'RENAMED', path('x.img'))
    d2 = getdir('x.img')
    fcbs = lambda dd: [dd[i * ENTSIZE:(i + 1) * ENTSIZE]
                       for i in range(NENT) if dd[i * ENTSIZE] < 0x10]
    check(fcbs(d2) == fcbs(d1), "--label leaves every file FCB alone")
    sfcbs = lambda dd: [dd[i * ENTSIZE:(i + 1) * ENTSIZE]
                        for i in range(3, NENT, 4)]
    check(sfcbs(d2) == sfcbs(d1), "--label leaves every SFCB alone")

    # and the data still reads back through both tools
    ex = path('ex-x')
    shutil.rmtree(ex, ignore_errors=True)
    mk('--extract', path('x.img'), ex)
    check(cmpfile(os.path.join(ex, 'TWO.TXT'), b'two\n' * 500),
          "mkcpmfs extracts the file cpmcp wrote")
    if os.path.exists(path('two.out')):
        os.remove(path('two.out'))
    ct('cpmcp', 'x.img', '0:TWO.TXT', path('two.out'))
    check(cmpfile(path('two.out'), b'two\n' * 500),
          "cpmcp reads back the file it wrote")
    if os.path.exists(path('big.out')):
        os.remove(path('big.out'))
    ct('cpmcp', 'x.img', '0:BIG.DAT', path('big.out'))
    check(cmpfile(path('big.out'), FILES['BIG.DAT']),
          "cpmcp reads back the two-entry file mkcpmfs packed")


def t_stamp_encoding():
    """The stamp bytes are the contract's, not just self-consistent."""
    mk('--initdir', '--label', 'EPOCH', '--stamp-date', '1978-01-01T00:00',
       path('ep.img'), str(BLOCKS), srcdir('src1', {'A.TXT': b'a\n'}))
    d = getdir('ep.img')
    lab = [i for i in range(NENT) if d[i * ENTSIZE] == T_LABEL][0]
    e = d[lab * ENTSIZE:(lab + 1) * ENTSIZE]
    check(e[24:28] == b'\x01\x00\x00\x00',
          "1978-01-01 00:00 encodes as day 1, LE, BCD 00:00")

    mk('--label', 'EPOCH', '--stamp-date', '2026-07-30T14:35', path('ep.img'))
    d = getdir('ep.img')
    e = d[lab * ENTSIZE:(lab + 1) * ENTSIZE]
    days = e[28] | (e[29] << 8)
    want = (datetime.date(2026, 7, 30) - datetime.date(1977, 12, 31)).days
    check(days == want, "date word = days since 1977-12-31 (%d)" % want)
    check(e[30] == 0x14 and e[31] == 0x35, "hour/minute are packed BCD")
    rc, out = fsck('ep.img')
    check(rc == 0 and 'Error' not in out and 'Warning' not in out,
          "fsck.cpm accepts the label's stamps")

    # the same encoding from the other side: cpmtools stamps a file's update
    # time from the host mtime (cpmcp -p), and the bytes must be the contract's
    t = calendar.timegm((2026, 7, 30, 14, 35, 0))
    ct('cpmcp', 'ep.img', '-p', hostfile('b.txt', b'b\n', mtime=t), '0:B.TXT')
    d = getdir('ep.img')
    sub = sub_record(d, fcb_index(d, b'B       TXT'))
    check(sub[4:8] == bytes([want & 0xff, want >> 8, 0x14, 0x35]),
          "cpmcp -p writes 2026-07-30 14:35 as the same four bytes")
    _, mout = mk('--list', path('ep.img'))
    check(mklisting(mout)[1].get('B.TXT', ('', ''))[1] == '2026-07-30 14:35',
          "mkcpmfs decodes the update stamp cpmcp wrote")


def t_capacity():
    """The stamped directory really does hold 384 file entries, not 512."""
    files = dict(('F%03d.TXT' % i, b'x\n') for i in range(384))
    src = srcdir('src384', files)
    rc, out = mk('--initdir', path('cap.img'), str(BLOCKS), src)
    check(rc == 0, "384 files fit in a stamped directory")
    rows = ctlisting('cap.img')[0]
    check(len(rows) == 384, "cpmls lists all 384 files")
    d = getdir('cap.img')
    rc, out = ct('cpmcp', 'cap.img', hostfile('over.txt', b'x\n'),
                 '0:OVER.TXT', expect_ok=False)
    check(rc != 0 and 'directory full' in out and getdir('cap.img') == d,
          "cpmcp finds no free slot beside 128 SFCBs, and changes nothing")
    files['OVER.TXT'] = b'x\n'
    src = srcdir('src385', files)
    rc, out = run([sys.executable, MKCPMFS, '--initdir', path('cap2.img'),
                   str(BLOCKS), src], expect_ok=False)
    check(rc != 0 and 'usable slots' in out,
          "385 files are refused with a slot-count error, not silently lost")


def main():
    global WORK, CPMTOOLS
    if len(sys.argv) != 3:
        sys.exit("usage: dirfmt-test.py <workdir> <cpmtools-bindir>")
    WORK, CPMTOOLS = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
    shutil.rmtree(WORK, ignore_errors=True)
    os.makedirs(WORK)

    for t in (t_unstamped_unchanged, t_initdir_layout, t_initdir_relocates,
              t_preserve_through_cpmtools, t_rm_stamped, t_cross_roundtrip,
              t_stamp_encoding, t_capacity):
        print("\n-- %s: %s" % (t.__name__, t.__doc__.splitlines()[0]))
        t()

    print("\n%d checks, %d failures" % (checks, len(failures)))
    if failures:
        for f in failures:
            print("  FAILED: %s" % f)
        sys.exit(1)
    print("dirfmt-check: PASS")


if __name__ == '__main__':
    main()
