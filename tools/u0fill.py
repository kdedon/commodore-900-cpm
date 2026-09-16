#!/usr/bin/env python3
"""u0fill.py - the three files `make verify-user0' packs onto drive A:.

    u0fill.py <destdir>

The user-0 fallback (function 15's search$user0, c900oses/cpm8000/ref/cpm3/bdos30.asm:
3940-3974) is decided by ONE bit, so the fixture needs a matched pair --
a user-0 file with the SYS attribute and one without -- plus a file that
belongs to the user area the session runs in.  Nothing here sets the
attributes or the user bytes; tools/ccpuser.py does that after packing,
because both live in the DIRECTORY entry, not in the file.

  U0SHARE.TXT   300 records, i.e. 37.5 KB, so it needs a SECOND directory
                entry: drive A: has BLS 4096 and DSM > 255, so one entry
                covers 32 KB (256 records), and EXM 1 folds two extents
                into it.  Each record spells its own number in its first
                four bytes, which is what makes a read from the wrong
                entry read as the wrong number instead of as an error --
                the next-entry open is the one place the fallback flag
                has to survive from one BDOS call into the next.
  U0PLAIN.TXT   the control: same user area, no SYS.
  U3ONLY.TXT    the file that proves where the program RAN.

The length is an exact multiple of 128 on purpose: mkcpmfs.py pads a
final partial record of a .TXT file with ^Z, which would overwrite the
record number of the last record.
"""

import os
import sys

SECLEN = 128
NREC = 300                              # > one directory entry (256 records)


def main(argv):
    if len(argv) != 2:
        sys.exit("usage: u0fill.py <destdir>")
    d = argv[1]
    if not os.path.isdir(d):
        sys.exit("u0fill: %s is not a directory" % d)
    body = []
    for k in range(NREC):
        rec = ('%04d' % k) + ' record of U0SHARE, shared out of user area 0'
        body.append(rec.ljust(SECLEN, '.'))
    open(os.path.join(d, 'U0SHARE.TXT'), 'w').write(''.join(body))
    open(os.path.join(d, 'U0PLAIN.TXT'), 'w').write(
        'user zero, no SYS attribute: private to user area 0\n')
    open(os.path.join(d, 'U3ONLY.TXT'), 'w').write(
        'user area three, and only user area three\n')
    print("u0fill: U0SHARE.TXT (%d records), U0PLAIN.TXT, U3ONLY.TXT in %s"
          % (NREC, d))


main(sys.argv)
