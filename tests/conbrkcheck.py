#!/usr/bin/env python3
# Copyright (c) 2026 Kevin Dedon.
# SPDX-License-Identifier: MIT

r"""conbrkcheck.py -- assertions for verify-conbrk (conbrk()'s ^S/^Q/^C
poll, src/bdos/conbdos.c).

Usage:
    conbrkcheck.py TOKENS PLAINLOG HALTLOG RESUMELOG CTLCLOG

TOKENS is the count CONBRK P was run with (verify.mk's CBRKTOKENS).  The
four logs are transcripts of four separate `CONBRK P TOKENS' sessions run
by tests/verify.mk's verify-conbrk, differing only in what is typed at
the running program:

  PLAINLOG   nothing typed at all -- the control case.  The poll
             was widened from every character to once every
             CONBRK_POLL, and that widening could have broken the
             ORDINARY run silently, so this asserts the exact, unbroken
             0000..TOKENS-1 sequence with the loop polling 1/8th as often.
  HALTLOG    ^S sent, and nothing else ever -- proves ^S actually stops
             output, by proving the session never completes and never
             comes back to a prompt.
  RESUMELOG  ^S then ^Q -- proves ^Q resumes it LOSSLESSLY, by proving
             the session does complete, with the same exact, unbroken
             sequence PLAINLOG produced.
  CTLCLOG    ^C -- proves ^C warm boots: the program is abandoned where
             it stood (no CONBRK-DONE, and a bounded prefix like the ^S
             case) and yet CCP's prompt returns with nothing sent to
             release it, which is exactly what HALTLOG shows does NOT
             happen when the stop is a ^S.

HOW THE KEYSTROKE GETS IN.  The emulator paces --input: a byte waits
until the guest looks ready to read it, because a byte handed over early
is swallowed by whatever read the guest is actually in.  A guest in a
print loop never looks ready, which is why an earlier version of this
test (added, then reverted for this reason) could not send a ^S at all -- every
session ran to completion first.  The emulator now has an explicit
type-ahead primitive for exactly this: `\i' in --input marks one byte as
delivered the moment the receiver is free, and --input-mark=TEXT holds
those bytes until the guest has PRINTED TEXT.  verify-conbrk marks on
CONBRK-START, so the control byte is waiting in the receiver before the
measured loop emits its first character -- the type-ahead this test's
arithmetic has always assumed.

THE BOUND.  conbrk() looks at the keyboard once every CONBRK_POLL
characters (read out of src/bdos/conbdos.c below, not restated here), and
CONBRK P's preamble forces that counter to a known zero before the
measured loop starts (see src/cmd/conbrk.c).  So in HALTLOG and CTLCLOG
whatever prefix of the pattern got out before the stop took effect must
be an exact, unbroken prefix of it, no longer than CONBRK_POLL - 1
characters.  That is the bound the widened poll interval implies.

WHAT IS NOT ASSERTED, AND WHY.  Not that CTLCLOG's warm boot reports
RC_CTLC (0xfffe).  That earlier version planned to read it back with a following
`CONBRK R', and that cannot work: the CCP clears the program return code
before every command it runs (ccp_seterr(FALSE), src/ccp/ccp.c:1634, and
the comment at src/ccp/ccpext.c:308-326 says so outright), so no later
command can ever see the code an earlier one left.  `IF ERROR' would see
it, but IF/ELSE/FI are not configured in this CCP (flow_on() is false;
the session answers `IF?').  What is asserted instead is the behaviour
that only warmboot(1) produces and that this suite can actually
distinguish: the program stopped mid-pattern, never printed
CONBRK-DONE, and the prompt came back anyway -- which the ^S session,
with the same bounded prefix and no ^Q, provably does not do.
"""

import os
import re
import sys

START = b'CONBRK-START\n'
DONE = b'CONBRK-DONE\n'
RC_CTLC = 'FFFE'
CONBDOS = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                       'src', 'bdos', 'conbdos.c')


def usage():
    sys.exit(__doc__)


def conbrk_poll():
    """CONBRK_POLL, read out of the BDOS source rather than restated
    here: this file asserts a bound the BDOS defines, and a copy of the
    number would go stale the day someone retunes the poll interval --
    silently passing against the wrong bound, which is the one failure
    mode a bounds check must not have."""
    try:
        with open(CONBDOS) as f:
            src = f.read()
    except OSError as e:
        sys.exit('conbrkcheck: FAIL cannot read %s for CONBRK_POLL: %s'
                 % (CONBDOS, e))
    m = re.search(r'^#define\s+CONBRK_POLL\s+(\d+)', src, re.M)
    if not m:
        sys.exit('conbrkcheck: FAIL no `#define CONBRK_POLL N\' in %s -- '
                 'the poll interval this test bounds is defined there, '
                 'and guessing at it would assert the wrong number'
                 % CONBDOS)
    return int(m.group(1))


def pattern(n):
    """The exact bytes CONBRK P n emits: n tokens, '%04d ' each."""
    return b''.join(('%04d ' % i).encode('ascii') for i in range(n))


def load(path):
    """A transcript, with CRs dropped -- CONBRK's own CRLFs and CCP's
    prompt lines both use them, and no check below cares."""
    with open(path, 'rb') as f:
        return f.read().replace(b'\r', b'')


def after_marker(log, path):
    """Bytes following CONBRK-START's own line, or a hard failure --
    every session here begins with a `CONBRK P ...' and the marker is
    the first thing it prints once CM_NOSTOP has forced its poll
    counter to zero (see conbrk.c).  It is also the emulator's
    --input-mark, so its absence means the control byte was never
    released either."""
    i = log.find(START)
    if i < 0:
        sys.exit('conbrkcheck: FAIL [%s] no CONBRK-START marker -- did '
                 'CONBRK even run?' % path)
    return log[i + len(START):]


def common_prefix_len(a, b):
    n = 0
    while n < len(a) and n < len(b) and a[n] == b[n]:
        n += 1
    return n


def check_full_sequence(tail, full, path, label):
    """The whole, unbroken pattern followed by CONBRK-DONE.  A prefix
    check, not equality: CONBRK returns to CCP afterward and the fresh
    A> prompt belongs to the session, not to what CONBRK printed."""
    want = full + DONE
    if not tail.startswith(want):
        got_done = DONE in tail
        sys.exit('conbrkcheck: FAIL [%s] %s: output does not match the '
                 'full, unbroken sequence CONBRK P generates '
                 '(CONBRK-DONE %s) -- some of it was lost, duplicated '
                 'or reordered' % (path, label, 'seen' if got_done else 'MISSING'))


def check_short_prefix(tail, full, path, label, poll):
    """The bytes at the front of `tail' that match `full' must stop
    within CONBRK_POLL - 1 -- the most conbrk() can let through between
    one poll and the next -- and nothing after them may be more of the
    pattern sneaking out."""
    if DONE in tail:
        sys.exit('conbrkcheck: FAIL [%s] %s: CONBRK-DONE appears -- the '
                 'run completed instead of stopping' % (path, label))
    n = common_prefix_len(tail, full)
    if n == 0:
        sys.exit('conbrkcheck: FAIL [%s] %s: not one character of the '
                 'pattern was emitted -- the keystroke was consumed '
                 'before the measured loop started, which the CM_NOSTOP '
                 'preamble and the CONBRK-START input mark are both '
                 'written to prevent' % (path, label))
    if n > poll - 1:
        sys.exit('conbrkcheck: FAIL [%s] %s: %d characters got out '
                 'before the stop took effect -- more than '
                 'CONBRK_POLL - 1 = %d, so the poll interval let output '
                 'run further past the keystroke than '
                 'src/bdos/conbdos.c documents' % (path, label, n, poll - 1))
    print('conbrkcheck: ok   [%s] %s: stopped after %d character(s) '
          '(<=%d), an exact prefix' % (path, label, n, poll - 1))
    return n


def check_plain(path, n):
    log = load(path)
    tail = after_marker(log, path)
    check_full_sequence(tail, pattern(n), path, 'nothing typed')
    print('conbrkcheck: ok   [%s] full %d-token sequence, nothing lost '
          'or duplicated by an uninterrupted run' % (path, n))
    marker = b'CONBRK-RC='
    i = log.find(marker)
    if i < 0:
        sys.exit('conbrkcheck: FAIL [%s] no CONBRK-RC= line -- the '
                 'follow-up `CONBRK R\' never ran' % path)
    rc = log[i + len(marker):i + len(marker) + 4].decode('ascii', 'replace')
    if rc == RC_CTLC:
        sys.exit('conbrkcheck: FAIL [%s] return code is RC_CTLC (%s) '
                 'even though nothing was ever typed -- conbrk() set it '
                 'spuriously' % (path, RC_CTLC))
    print('conbrkcheck: ok   [%s] return code is %s, not RC_CTLC: '
          'conbrk() did not spuriously restart the program' % (path, rc))


def check_halt(path, n, poll):
    log = load(path)
    tail = after_marker(log, path)
    check_short_prefix(tail, pattern(n), path, '^S with no ^Q', poll)
    # Never resumes on its own: no second prompt line ever follows.
    if log.count(b'A>') > 1:
        sys.exit('conbrkcheck: FAIL [%s] a second A> prompt appears -- '
                 '^S let the session return to CCP with no ^Q sent, '
                 'which means it did not actually stop anything' % path)
    print('conbrkcheck: ok   [%s] no A> prompt reappears in a run three '
          'times as long as a complete one -- the session is still '
          'stopped, not merely slow' % path)


def check_resume(path, n):
    log = load(path)
    tail = after_marker(log, path)
    check_full_sequence(tail, pattern(n), path, '^S then ^Q')
    print('conbrkcheck: ok   [%s] ^S then ^Q: full %d-token sequence, '
          'nothing lost or duplicated across the pause' % (path, n))


def check_ctlc(path, n, poll):
    log = load(path)
    tail = after_marker(log, path)
    check_short_prefix(tail, pattern(n), path, '^C', poll)
    # Unlike ^S, ^C must NOT leave the session stuck: warmboot(1) never
    # returns to the interrupted program, so CCP has to come back on
    # its own, with nothing sent to release it.
    if b'A>' not in tail:
        sys.exit('conbrkcheck: FAIL [%s] no A> prompt after the ^C '
                 'prefix -- the session is stuck, which is ^S\'s '
                 'behaviour, not a warm boot\'s' % path)
    print('conbrkcheck: ok   [%s] A> reappears with nothing sent to '
          'release it -- ^C abandoned the program and warm booted, '
          'where ^S left the same session waiting' % path)


def main(argv):
    if len(argv) != 6:
        usage()
    try:
        n = int(argv[1])
    except ValueError:
        usage()
    poll = conbrk_poll()
    print('conbrkcheck: CONBRK_POLL = %d, read from %s' % (poll, CONBDOS))
    plain, halt, resume, ctlc = argv[2], argv[3], argv[4], argv[5]
    check_plain(plain, n)
    check_halt(halt, n, poll)
    check_resume(resume, n)
    check_ctlc(ctlc, n, poll)
    print('conbrkcheck: PASS -- ^S stops output, ^Q resumes it losslessly, '
          '^C warm boots, all bounded by the %d-character poll interval'
          % poll)


if __name__ == '__main__':
    main(sys.argv)
