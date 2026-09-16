#!/usr/bin/env python3
"""ccpuser.py - move a packed CP/M file into another user area, in place.

Usage: ccpuser.py IMG NAME.TYP USER[R][S][A] [NAME.TYP USER... ...]

Test fixture for the CCP search-path / named-directory sessions
(`make verify-ccp'): mkcpmfs.py packs every file as user 0, and no
utility on the running system can write into another user area (a
transient found by the user-0 fallback runs *in* user 0, so PIP [Gn]
copies back into 0).  This rewrites the user byte of the matching
directory entries of an already-packed drive-A image, which is all the
CCP needs to see the file where the path points.

The user number may carry attribute letters -- R read-only, S system,
A archive -- which set bit 7 of directory bytes 9, 10 and 11.  S is the
one the user-0 fallback turns on: function 15 shares a user-0 file out
to another user area only when it is a SYS file (c900oses/cpm8000/ref/cpm3/bdos30.asm:
4006-4015), so a fixture that wants the fallback to fire has to say so,
and a fixture that wants it NOT to fire leaves the letter off.

Directory layout is drive A:'s DPB contract: entries are 32 bytes from
byte 0 of the image, DRM=511, so the first 16 KB is the directory.
"""

import sys

NENT = 512                              # DRM + 1
ESIZE = 32
DIRBYTES = NENT * ESIZE


def name11(s):
    """'FOO.TYP' -> the 11-byte padded directory name."""
    s = s.upper()
    name, _, typ = s.partition('.')
    if len(name) > 8 or len(typ) > 3:
        sys.exit("ccpuser: not an 8.3 name: %s" % s)
    return (name.ljust(8) + typ.ljust(3)).encode('ascii')


ATTRBIT = {'R': 9, 'S': 10, 'A': 11}     # directory byte, counted as in an FCB


def userspec(s):
    """'3' -> (3, []);  '0S' -> (0, [10])."""
    s = s.upper()
    n = 0
    while n < len(s) and s[n].isdigit():
        n += 1
    if n == 0:
        sys.exit("ccpuser: %r does not start with a user number" % s)
    bits = []
    for c in s[n:]:
        if c not in ATTRBIT:
            sys.exit("ccpuser: %r is not an attribute letter (RSA)" % c)
        bits.append(ATTRBIT[c])
    return int(s[:n]), bits


def main():
    if len(sys.argv) < 4 or len(sys.argv) % 2 != 0:
        sys.exit(__doc__)
    img = sys.argv[1]
    pairs = []
    for i in range(2, len(sys.argv), 2):
        user, bits = userspec(sys.argv[i + 1])
        pairs.append((name11(sys.argv[i]), user, bits))
    with open(img, 'r+b') as f:
        d = bytearray(f.read(DIRBYTES))
        for want, user, bits in pairs:
            n = 0
            for k in range(NENT):
                e = d[k * ESIZE:(k + 1) * ESIZE]
                if e[0] == 0xE5 or e[0] >= 0x10:
                    continue
                if bytes(b & 0x7F for b in e[1:12]) == want:
                    d[k * ESIZE] = user
                    for b in bits:
                        d[k * ESIZE + b] |= 0x80
                    n += 1
            if n == 0:
                sys.exit("ccpuser: %s not found in %s"
                         % (want.decode(), img))
            print("ccpuser: %s -> user %d%s (%d entr%s)"
                  % (want.decode(), user,
                     ''.join(k for k, b in sorted(ATTRBIT.items())
                             if b in bits),
                     n, "y" if n == 1 else "ies"))
        f.seek(0)
        f.write(d)


main()
