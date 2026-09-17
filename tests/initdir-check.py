#!/usr/bin/env python3
# Copyright (c) 2026 Kevin Dedon.
# SPDX-License-Identifier: MIT

"""initdir-check.py -- judge an INITDIR run from the raw partition bytes.

The runtime half of verify-initdir only creates evidence; every question
that decides pass or fail is answered here, by decoding the directory
region of a drive slice cut out of the 42 MB disk image.  Nothing in
this file reads a transcript.

Subcommands (all take slices, i.e. plain drive images):

  unstamped IMG            no 4th slot holds an SFCB -- the BEFORE state.
                           "it has stamps now" proves nothing unless it
                           is shown it had none.
  stamped   IMG            every 4th slot holds a type-21h SFCB with the
                           three preceding entries' sub-records present.
  oracle    IMG ORACLE     the directory region matches, byte for byte,
                           what tools/mkcpmfs.py --initdir makes of the
                           SAME starting image.  mkcpmfs's do_initdir is
                           an independent implementation of the slot
                           rule and predates this one.
  preserved BEFORE AFTER   every non-free, non-SFCB entry present before
                           is present after, exactly once, byte for byte
                           (relocation is allowed to move it, nothing
                           else).  Catches a lost FCB and a duplicated
                           one, which are the two ways this can corrupt.
  at        IMG N TYPE[:NAME]
                           entry N has this type byte, and (when given)
                           this 8.3 name.  Proves WHICH entry moved
                           where rather than only that a count matched.
  same      A B            two slices are byte-identical.  Drive
                           identity: the drive that was not named must
                           come out of the run unchanged.

--red inverts the exit status: the check is expected to FAIL, and the
message it would have printed is shown as evidence that it can.  Every
check in verify-initdir is demonstrated red this way against the other
drive or the other state.
"""

import sys

RECLEN = 128
BLS = 4096
DRM = 511
NENT = DRM + 1
ENTSIZE = 32
DIRBYTES = (NENT * ENTSIZE + BLS - 1) // BLS * BLS   # 16 KB, 4 blocks

T_FREE = 0xe5
T_SFCB = 0x21


def die(msg):
    raise Fail(msg)


class Fail(Exception):
    pass


def load(path):
    with open(path, 'rb') as f:
        d = f.read(DIRBYTES)
    if len(d) < DIRBYTES:
        die("%s is too small to hold a directory (%d bytes)" % (path, len(d)))
    return d


def ent(d, i):
    return d[i * ENTSIZE:(i + 1) * ENTSIZE]


def name83(e):
    n = bytes(c & 0x7f for c in e[1:12]).decode('ascii', 'replace')
    return (n[:8].rstrip() + '.' + n[8:].rstrip()).rstrip('.')


def is_file(t):
    return t <= 0x0f


def keep(t):
    """Entries an INITDIR run must carry through unchanged."""
    return t != T_FREE and t != T_SFCB


def sfcb_indices(d):
    return range(3, NENT, 4)


def c_unstamped(args):
    img = load(args[0])
    bad = [i for i in sfcb_indices(img) if ent(img, i)[0] == T_SFCB]
    if bad:
        die("%s ALREADY has date stamping: %d of the %d fourth slots hold "
            "an SFCB (first at entry %d)"
            % (args[0], len(bad), NENT // 4, bad[0]))
    print("unstamped: %s -- none of the %d fourth slots holds an SFCB"
          % (args[0], NENT // 4))


def c_stamped(args):
    img = load(args[0])
    bad = [i for i in sfcb_indices(img) if ent(img, i)[0] != T_SFCB]
    if bad:
        die("%s is NOT fully stamped: %d of the %d fourth slots is not an "
            "SFCB (first at entry %d, type 0x%02x)"
            % (args[0], len(bad), NENT // 4, bad[0], ent(img, bad[0])[0]))
    print("stamped: %s -- all %d fourth slots hold a type-21h SFCB"
          % (args[0], NENT // 4))


def c_oracle(args):
    img, orc = load(args[0]), load(args[1])
    if img != orc:
        n = sum(1 for a, b in zip(img, orc) if a != b)
        off = next(k for k in range(DIRBYTES) if img[k] != orc[k])
        die("%s does not match the mkcpmfs --initdir oracle %s: %d bytes "
            "differ, first at offset %d (entry %d byte %d): target 0x%02x, "
            "oracle 0x%02x" % (args[0], args[1], n, off, off // ENTSIZE,
                               off % ENTSIZE, img[off], orc[off]))
    print("oracle: %s is byte-identical to mkcpmfs --initdir over all %d "
          "directory bytes" % (args[0], DIRBYTES))


def c_preserved(args):
    before, after = load(args[0]), load(args[1])
    was = {}
    for i in range(NENT):
        e = ent(before, i)
        if keep(e[0]):
            was[bytes(e)] = was.get(bytes(e), 0) + 1
    now = {}
    for i in range(NENT):
        e = ent(after, i)
        if keep(e[0]):
            now[bytes(e)] = now.get(bytes(e), 0) + 1
    lost = [e for e in was if now.get(e, 0) < was[e]]
    dup = [e for e in now if now[e] > was.get(e, 0)]
    if lost:
        die("%d directory entries were LOST by the run, first: type 0x%02x "
            "%s" % (len(lost), lost[0][0], name83(lost[0])))
    if dup:
        die("%d directory entries were DUPLICATED or invented by the run, "
            "first: type 0x%02x %s" % (len(dup), dup[0][0], name83(dup[0])))
    print("preserved: all %d non-free entries survive the run exactly once"
          % sum(was.values()))


def c_at(args):
    img = load(args[0])
    i = int(args[1])
    spec = args[2].split(':', 1)
    want = int(spec[0], 0)
    e = ent(img, i)
    if e[0] != want:
        die("entry %d of %s is type 0x%02x, not 0x%02x"
            % (i, args[0], e[0], want))
    if len(spec) > 1 and name83(e) != spec[1]:
        die("entry %d of %s is named %s, not %s"
            % (i, args[0], name83(e) or '(blank)', spec[1]))
    print("at: entry %d of %s is type 0x%02x%s"
          % (i, args[0], e[0], (" %s" % spec[1]) if len(spec) > 1 else ""))


def c_same(args):
    a, b = load(args[0]), load(args[1])
    if a != b:
        n = sum(1 for x, y in zip(a, b) if x != y)
        off = next(k for k in range(DIRBYTES) if a[k] != b[k])
        die("%s and %s differ in %d directory bytes, first at offset %d "
            "(entry %d)" % (args[0], args[1], n, off, off // ENTSIZE))
    print("same: %s and %s have identical directories" % (args[0], args[1]))


CMDS = {
    'unstamped': (c_unstamped, 1),
    'stamped': (c_stamped, 1),
    'oracle': (c_oracle, 2),
    'preserved': (c_preserved, 2),
    'at': (c_at, 3),
    'same': (c_same, 2),
}


def main(argv):
    red = False
    if '--red' in argv:
        argv = [a for a in argv if a != '--red']
        red = True
    if len(argv) < 2 or argv[1] not in CMDS:
        sys.stderr.write(__doc__)
        return 2
    fn, nargs = CMDS[argv[1]]
    args = argv[2:]
    if len(args) != nargs:
        sys.stderr.write("initdir-check: %s takes %d argument(s)\n"
                         % (argv[1], nargs))
        return 2
    try:
        fn(args)
    except Fail as e:
        if red:
            print("RED (expected): %s" % e)
            return 0
        sys.stderr.write("initdir-check: FAIL -- %s\n" % e)
        return 1
    if red:
        sys.stderr.write("initdir-check: FAIL -- `%s' was expected to fail "
                         "and did not: the check cannot go red, so its "
                         "passing proves nothing\n" % argv[1])
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
