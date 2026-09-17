#!/usr/bin/env python3
# Copyright (c) 2026 Kevin Dedon.
# SPDX-License-Identifier: MIT

"""setbfill.py - build the drive-B: source tree the SET coverage target packs.

    setbfill.py <destdir>

`make verify-setb' needs a drive with more files on it than any checked-in
fixture should carry, so it generates them.  The three families each exist
to make one SET limit reachable, and the counts are the point:

  FILLnnn.TXT   140   with the rest, more than 128 matching names in one
                      directory -- the old MAXFILES, where the expansion
                      table silently stopped collecting and SET reported
                      success on a subset (tests/dirattr.py counts them)
  PAGEnn.TXT     30   more than one 24-line page, so [PAGE] has to stop
                      and ask for a RETURN (set.plm:206-228)
  SPECx.TXT       9   more file specs than the old MAXSPEC of 8

U3FILE.TXT is the file `make verify-setb' moves into user area 3 with
tools/ccpuser.py; nothing here puts it there.
"""

import os
import sys


def main(argv):
    if len(argv) != 2:
        sys.exit("usage: setbfill.py <destdir>")
    d = argv[1]
    if not os.path.isdir(d):
        sys.exit("setbfill: %s is not a directory" % d)
    n = 0
    for i in range(1, 141):
        open(os.path.join(d, 'FILL%03d.TXT' % i), 'w').write('fill %d\n' % i)
        n += 1
    for i in range(1, 31):
        open(os.path.join(d, 'PAGE%02d.TXT' % i), 'w').write('page %d\n' % i)
        n += 1
    for c in 'ABCDEFGHI':
        open(os.path.join(d, 'SPEC%s.TXT' % c), 'w').write('spec %s\n' % c)
        n += 1
    open(os.path.join(d, 'U3FILE.TXT'), 'w').write('user three\n')
    n += 1
    print("setbfill: %d files in %s" % (n, d))


main(sys.argv)
