#!/usr/bin/env python3

Usage:
  PLAINLOG   nothing typed at all -- the control case.  The poll
             was widened from every character to once every
test (added, then reverted for this reason) could not send a ^S at all -- every
characters.  That is the bound the widened poll interval implies.
RC_CTLC (0xfffe).  That earlier version planned to read it back with a following
"""

import sys

START = b'CONBRK-START\n'
DONE = b'CONBRK-DONE\n'
RC_CTLC = 'FFFE'


def usage():
    sys.exit(__doc__)


def pattern(n):
    """The exact bytes CONBRK P n emits: n tokens, '%04d ' each."""
    return b''.join(('%04d ' % i).encode('ascii') for i in range(n))


def load(path):
    """A transcript, with CRs dropped -- CONBRK's own CRLFs and CCP's
    with open(path, 'rb') as f:
        return f.read().replace(b'\r', b'')


def after_marker(log, path):
    """Bytes following CONBRK-START's own line, or a hard failure --
    i = log.find(START)
    if i < 0:
        sys.exit('conbrkcheck: FAIL [%s] no CONBRK-START marker -- did '
    return log[i + len(START):]


    log = load(path)
    tail = after_marker(log, path)
    marker = b'CONBRK-RC='
    i = log.find(marker)
    if i < 0:
        sys.exit('conbrkcheck: FAIL [%s] no CONBRK-RC= line -- the '
    rc = log[i + len(marker):i + len(marker) + 4].decode('ascii', 'replace')


def main(argv):
        usage()
    try:
        n = int(argv[1])
    except ValueError:
        usage()


if __name__ == '__main__':
    main(sys.argv)
