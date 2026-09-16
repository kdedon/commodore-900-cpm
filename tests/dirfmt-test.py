#!/usr/bin/env python3
"""dirfmt-test.py - agreement + backward-compatibility tests for the CP/M
directory format contract shared by this project's directory packer and
COHERENT's `cpm(1)` reader.

Two implementations are exercised against the same images:
  mkcpmfs.py            the host packer/reader/initialiser
  cpm(1)                coherent/os/cmd/cpm.c, built here with the host cc
                        purely to test its logic (the shipping binary is the
                        MWC Z8001 build)

Run via `make dirfmt-check` from the repository root.  Every image is created fresh in
the work directory; nothing outside it is touched.

Usage: dirfmt-test.py <workdir> <cpm-binary>
"""

import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
MKCPMFS = os.path.join(HERE, '..', 'tools', 'mkcpmfs.py')

BLOCKS = 20480
ENTSIZE = 32
NENT = 512
DIRBYTES = NENT * ENTSIZE

T_FREE = 0xe5
T_SFCB = 0x21
T_LABEL = 0x20

WORK = None
CPMBIN = None
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


def run(argv, expect_ok=True):
    p = subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    out = p.stdout.decode('utf-8', 'replace')
    if expect_ok and p.returncode != 0:
        failures.append("command failed: %s\n%s" % (' '.join(argv), out))
        print("FAIL  command %s\n%s" % (' '.join(argv), out))
    return p.returncode, out


def mk(*args):
    return run([sys.executable, MKCPMFS] + list(args))


def cpm(img, *args):
    return run([CPMBIN, '-f', img] + list(args))


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


def srcdir(name, files):
    p = path(name)
    shutil.rmtree(p, ignore_errors=True)
    os.makedirs(p)
    for fn, data in files.items():
        with open(os.path.join(p, fn), 'wb') as f:
            f.write(data)
    return p


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
    _, mout = mk('--list', path('legacy.img'))
    _, cout = cpm(path('legacy.img'), 'ls')
    check(filelines(mout) == filelines(cout),
          "legacy image: mkcpmfs --list and cpm ls agree")

    # cpm(1) round-trips it without inventing extensions
    cpm(path('legacy.img'), 'write', os.path.join(src, 'HELLO.TXT'), 'COPY.TXT')
    cpm(path('legacy.img'), 'rm', 'SMALL.BIN')
    d = getdir('legacy.img')
    check(len(ext_image(d)) == 0,
          "legacy image stays extension-free through cpm write/rm")
    _, cout = cpm(path('legacy.img'), 'ls')
    check('COPY.TXT' in cout and 'SMALL.BIN' not in cout,
          "legacy image: cpm write/rm took effect")
    _, mout = mk('--list', path('legacy.img'))
    check(filelines(mout) == filelines(cout),
          "legacy image after cpm edits: both tools still agree")


FILEROW = re.compile(r'^\s*(\d+) (\S+)\s+(\d+) bytes\s+(\d+) blocks\s+'
                     r'\d+ entr\w+\s*(.*)$')
STAMP = re.compile(r'\d{4}-\d\d-\d\d \d\d:\d\d|-')


def filerows(out):
    """The file rows of a listing: (user, name, size, blocks) each."""
    rows = []
    for line in out.splitlines():
        m = FILEROW.match(line)
        if m:
            rows.append(m.group(1, 2, 3, 4))
    return sorted(rows)


def filelines(out):
    return filerows(out)


def stamplines(out):
    """{name: (create, update)} from either tool's file rows."""
    st = {}
    for line in out.splitlines():
        m = FILEROW.match(line)
        if m:
            s = STAMP.findall(m.group(5))
            if len(s) == 2:
                st[m.group(2)] = tuple(s)
    return st


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
    check(all(d[i * ENTSIZE] != T_SFCB or True for i in range(NENT)) and
          len(entries(d, lambda t: t < 0x10)) <= NENT - NENT // 4,
          "file entries fit in the 384 non-SFCB slots")

    # data survives: extraction still matches the sources
    ex = path('ex-stamped')
    shutil.rmtree(ex, ignore_errors=True)
    mk('--extract', path('stamped.img'), ex)
    check(cmpfile(os.path.join(ex, 'BIG.DAT'), FILES['BIG.DAT']),
          "stamped image: extracted BIG.DAT matches the source")


def cmpfile(p, want):
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

    _, out = cpm(path('reloc.img'), 'ls')
    check(all(fn in out for fn in files), "cpm ls sees the relocated files")

    # idempotent
    d1 = getdir('reloc.img')
    mk('--initdir', path('reloc.img'))
    check(getdir('reloc.img') == d1, "--initdir is idempotent")


def t_preserve_through_cpm():
    """cpm write/rm must leave every extension entry byte-identical."""
    src = srcdir('src', FILES)
    mk('--initdir', '--label', 'PRESERVE', path('pres.img'), str(BLOCKS), src)
    inject_oddities('pres.img')
    base = ext_image(getdir('pres.img'))

    with open(path('extra.txt'), 'wb') as f:
        f.write(b'a file written by cpm(1)\n' * 40)
    cpm(path('pres.img'), 'write', path('extra.txt'), 'EXTRA.TXT')
    cpm(path('pres.img'), 'rm', 'SMALL.BIN')
    cpm(path('pres.img'), 'write', os.path.join(src, 'HELLO.TXT'), 'HELLO.TXT')

    after = ext_image(getdir('pres.img'))
    check(set(after) == set(base),
          "cpm write/rm: the same set of extension entries survives")
    # SFCB contents may change only in the sub-records of slots cpm touched;
    # the label, the XFCB and the unknown-type entry must be untouched.
    stable = [i for i in base if base[i][0] != T_SFCB]
    check(all(after[i] == base[i] for i in stable),
          "cpm write/rm: label, XFCB and unknown-type entries are byte-identical")
    check(after != base, "cpm write did update SFCB stamps (label asks for it)")

    _, mout = mk('--list', path('pres.img'))
    _, cout = cpm(path('pres.img'), 'ls')
    check(filelines(mout) == filelines(cout),
          "stamped image: mkcpmfs --list and cpm ls agree on files")
    check(stamplines(mout) == stamplines(cout) and stamplines(mout) != {},
          "stamped image: both tools decode the SAME stamps")
    check('PRESERVE' in mout and 'PRESERVE' in cout,
          "both tools show the directory label")
    check('password' in mout and 'password' in cout,
          "both tools report the XFCB without interpreting it")
    check('unknown' in mout and 'unknown' in cout,
          "both tools report the unknown-type entry as preserved")


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


def t_rm_clears_stamps():
    """Freeing an FCB clears its stamps, so no stale stamp is inherited."""
    src = srcdir('src', FILES)
    mk('--initdir', '--label', 'STAMPS', path('rm.img'), str(BLOCKS), src)
    with open(path('one.txt'), 'wb') as f:
        f.write(b'one\n')
    cpm(path('rm.img'), 'write', path('one.txt'), 'ONE.TXT')
    d = getdir('rm.img')
    i = [i for i in range(NENT)
         if d[i * ENTSIZE] < 0x10 and d[i * ENTSIZE + 1:i * ENTSIZE + 12]
         == b'ONE     TXT'][0]
    sub = lambda dd: dd[(i | 3) * ENTSIZE + 1 + 10 * (i & 3):
                        (i | 3) * ENTSIZE + 11 + 10 * (i & 3)]
    check(sub(d) != b'\0' * 10, "cpm write stamped the new file")
    cpm(path('rm.img'), 'rm', 'ONE.TXT')
    d = getdir('rm.img')
    check(d[i * ENTSIZE] == T_FREE, "cpm rm freed the FCB")
    check(sub(d) == b'\0' * 10, "cpm rm cleared the file's SFCB sub-record")
    check(d[(i | 3) * ENTSIZE] == T_SFCB, "the SFCB entry itself survives rm")


def t_cross_roundtrip():
    """A directory each tool wrote must survive the other tool untouched."""
    src = srcdir('src', FILES)
    mk('--initdir', '--label', 'XROUND', path('x.img'), str(BLOCKS), src)
    inject_oddities('x.img')

    # mkcpmfs-written directory -> read and rewritten by cpm(1)
    d0 = getdir('x.img')
    cpm(path('x.img'), 'ls')                       # read-only: no change
    check(getdir('x.img') == d0, "cpm ls does not modify the directory")
    mk('--list', path('x.img'))
    check(getdir('x.img') == d0, "mkcpmfs --list does not modify the directory")

    # cpm(1)-written directory -> re-initialised by mkcpmfs, nothing disturbed
    with open(path('two.txt'), 'wb') as f:
        f.write(b'two\n' * 500)
    cpm(path('x.img'), 'write', path('two.txt'), 'TWO.TXT')
    d1 = getdir('x.img')
    mk('--initdir', path('x.img'))                 # already stamped: no-op
    check(getdir('x.img') == d1,
          "mkcpmfs --initdir over a cpm(1)-written stamped directory is a no-op")

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
          "mkcpmfs extracts the file cpm(1) wrote")
    cpm(path('x.img'), 'read', 'TWO.TXT', path('two.out'))
    check(cmpfile(path('two.out'), b'two\n' * 500),
          "cpm read returns the file cpm(1) wrote")


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
    import datetime
    want = (datetime.date(2026, 7, 30) - datetime.date(1977, 12, 31)).days
    check(days == want, "date word = days since 1977-12-31 (%d)" % want)
    check(e[30] == 0x14 and e[31] == 0x35, "hour/minute are packed BCD")


def t_capacity():
    """The stamped directory really does hold 384 file entries, not 512."""
    files = dict(('F%03d.TXT' % i, b'x\n') for i in range(384))
    src = srcdir('src384', files)
    rc, out = mk('--initdir', path('cap.img'), str(BLOCKS), src)
    check(rc == 0, "384 files fit in a stamped directory")
    files['OVER.TXT'] = b'x\n'
    src = srcdir('src385', files)
    rc, out = run([sys.executable, MKCPMFS, '--initdir', path('cap2.img'),
                   str(BLOCKS), src], expect_ok=False)
    check(rc != 0 and 'usable slots' in out,
          "385 files are refused with a slot-count error, not silently lost")


def main():
    global WORK, CPMBIN
    if len(sys.argv) != 3:
        sys.exit("usage: dirfmt-test.py <workdir> <cpm-binary>")
    WORK, CPMBIN = sys.argv[1], os.path.abspath(sys.argv[2])
    shutil.rmtree(WORK, ignore_errors=True)
    os.makedirs(WORK)

    for t in (t_unstamped_unchanged, t_initdir_layout, t_initdir_relocates,
              t_preserve_through_cpm, t_rm_clears_stamps, t_cross_roundtrip,
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
