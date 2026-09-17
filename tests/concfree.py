# Copyright (c) 2026 Kevin Dedon.
# SPDX-License-Identifier: MIT

"""concfree.py -- does verify-concerr's A: image put the two free directory
slots CONCTGT.TXT will take in DIFFERENT directory records?

That straddle is the whole point of verify-concerr (H7): it is what makes
the nested dirscan in filero() (src/bdos/bdosmisc.c) read a second record
into the one directory buffer, leaving the delete() that asked holding a
pointer into the wrong record.  With both entries in one record the session
runs to completion having tested nothing.

CONCF deletes CONCTGT.TXT (it is not there) and then creates it, so its
first entry takes the first free slot on the medium and, once that slot is
taken, its second extent takes the next one.  So the two slots to look at
are simply the first two 0xE5 entries of the packed directory.

usage: python3 tests/concfree.py <cpm filesystem image>
exit 0 and print the two indices when they are in different records.
"""

import sys

NENT = 512
ENTLEN = 32


def main(argv):
    if len(argv) != 2:
        sys.exit(__doc__)
    with open(argv[1], 'rb') as f:
        d = f.read(NENT * ENTLEN)
    free = [i for i in range(NENT) if d[i * ENTLEN] == 0xe5][:2]
    if len(free) < 2:
        sys.exit("concfree: fewer than two free directory entries")
    a, b = free
    if a >> 2 == b >> 2:
        sys.exit("concfree: free slots %d and %d are both in directory record"
                 " %d, so CONCTGT.TXT's two entries share a record and the\n"
                 "  nested scan never displaces the directory buffer -- the"
                 " session would pass without testing anything.\n"
                 "  tests/concpad.sh exists to keep this from happening;"
                 " see its head for the arithmetic." % (a, b, a >> 2))
    print("concfree: OK -- free slots %d (record %d) and %d (record %d)"
          % (a, a >> 2, b, b >> 2))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
