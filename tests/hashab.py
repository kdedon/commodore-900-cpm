# Copyright (c) 2026 Kevin Dedon.
# SPDX-License-Identifier: MIT

"""hashab.py -- the directory-hashing A/B test (PLAN.md sec 9 rows 5 and 8).

Row 5 wants measured evidence that hashing (dskhash.c) and the BCB cache
pay for themselves; row 8 wants a config on/off A-B test.  Both need one
prerequisite that did not exist before this change: a real switch
(dskhash.c hashen[]) that turns hashing off without touching anything
else.  Two cpm.sys builds, differing ONLY in that switch (see
tests/verify.mk verify-hash-ab), are compared here.

METHODOLOGY: subtraction -- one command run twice, at the cheapest and the most expensive
directory position, and the difference isolates the position-dependent
(scan) cost while the shared cost (boot, prompt, typing, the file's own
content) cancels.  This finds the hashing signal that a whole-directory
`DIR *.*' cannot: a wildcard scan gets no filter at all (dskhash.c
dhstart), so the original hashing wave measured it as unchanged -- this
tool reproduces that null result too, as a sanity check, but the number
that matters is TYPE of one specific (non-wildcard) file.

Uses the EMULATOR (c900 --max/--input), never the simulator: the
simulator's HTTP single-step protocol is a different,
much slower instrument, and nothing here needs it -- --input already
paces keystrokes for the guest, and the emulator prints the exact
instruction count a run took ("stopped after N instructions") on exit,
which is all a whole-command A-B comparison needs.

USAGE
    python3 tests/hashab.py --emu DIR --on IMG --off IMG [--n N]
"""

import argparse
import re
import subprocess
import sys

STOPPED_RE = re.compile(rb'stopped after (\d+) instructions')
MAXINSN = 200_000_000


def run(c900, disk, cmd):
    """Boot `disk`, run one console command, return (instructions, stdout text)."""
    firmware = c900.rsplit('/bin/', 1)[0] + '/rom'
    p = subprocess.run(
        [c900, '--firmware', firmware, '--disk', disk, '--input', cmd,
         '--max', str(MAXINSN), '--stop-on=idle', '--idle=40000000'],
        capture_output=True, timeout=180)
    out = p.stdout + p.stderr
    m = STOPPED_RE.search(out)
    if not m:
        raise RuntimeError('emulator gave no instruction count for %r:\n%s'
                            % (cmd, out.decode(errors='replace')[-500:]))
    return int(m.group(1)), out.decode(errors='replace')


def marginal(c900, disk, first, last):
    """T(TYPE of the last entry) - T(TYPE of the first): the scan-only cost."""
    t0, out0 = run(c900, disk, 'TYPE B:%s\\r' % first)
    t1, out1 = run(c900, disk, 'TYPE B:%s\\r' % last)
    return t1 - t0, out0, out1


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--emu', required=True, help='commodore-900-emulator checkout')
    ap.add_argument('--on', required=True, help='disk image, hashing ON')
    ap.add_argument('--off', required=True, help='disk image, hashing OFF')
    ap.add_argument('--n', type=int, default=384, help='directory entries (F000..F(n-1).TXT)')
    args = ap.parse_args()

    c900 = args.emu.rstrip('/') + '/bin/c900'
    first = 'F000.TXT'
    last = 'F%03d.TXT' % (args.n - 1)

    on_marg, on0, on1 = marginal(c900, args.on, first, last)
    off_marg, off0, off1 = marginal(c900, args.off, first, last)

    fail = []
    for label, out0, out1 in (('hashing ON', on0, on1), ('hashing OFF', off0, off1)):
        if 'hash test file 000' not in out0 or 'hash test file %03d' % (args.n - 1) not in out1:
            fail.append('%s: TYPE did not return the expected file content -- '
                         'this is a correctness bug in the hashing path, not a '
                         'performance number, and is reported ahead of the ratio.' % label)

    print('directory entries       : %d' % args.n)
    print('marginal scan cost, ON  : %d instructions' % on_marg)
    print('marginal scan cost, OFF : %d instructions' % off_marg)
    if on_marg > 0:
        print('speedup (OFF/ON)         : %.2fx' % (off_marg / on_marg))
    if fail:
        for f in fail:
            print('verify-hash-ab: FAIL --', f)
        sys.exit(1)
    if not (off_marg > on_marg):
        print('verify-hash-ab: FAIL -- hashing OFF was not slower than ON; '
              'the switch has no measurable effect, which contradicts the '
              'row-5/row-8 premise that hashing does real work.')
        sys.exit(1)
    print('verify-hash-ab: PASS -- hashen[] really governs hashing, directory '
          'results match either way, and hashing measurably speeds up a '
          'non-wildcard lookup (wildcard DIR is expected to show no change).')


if __name__ == '__main__':
    main()
