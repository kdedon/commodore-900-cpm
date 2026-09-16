#!/usr/bin/env python3
"""wirecon.py -- run the emulator with a SECOND TERMINAL on SCC channel A.

Every other verify target drives one console: the emulator maps SCC channel
B to its own stdin/stdout, `--input=' types at it and the transcript is what
comes back.  A two-console system cannot be tested that way, because the
second console is not a stream the emulator owns -- channel A is an AF_UNIX
socket the emulator CONNECTS to (`--wire=PATH', src/wire.c), and something
has to be listening on the far end.

This is that something.  It listens, starts the emulator, and then is the
person sitting at console 1: it collects everything the guest prints there
and types a scripted string back once a given text has appeared.  Console 0
is captured the ordinary way, from the emulator's stdout.

`--send-after TEXT --send BYTES' may be given more than once, and the pairs
fire in order.  THAT IS PACING, NOT CONVENIENCE.  A CP/M console driver
polls the keyboard while it is PRINTING, to catch ^S and ^C (conbdos.c
conbrk), and a byte that lands in that poll is handled there rather than by
the read the program is in.  So bytes dumped at a console faster than it
consumes them are not queued, they are eaten -- which is why the emulator's
own console feeder waits for quiet and for a prompt before each byte.  A
line typed after the prompt it answers is the same discipline, stated as
the test wants it.

The two transcripts come out as two files, which is the whole point: a test
can then assert that a line went to ONE of them and not the other, which is
the property "the console number selects the device" actually means.

`--rx-overrun' and `--send-delay' are for the one test that has to make the
wire LOSE something (verify-rxov).  The first passes the emulator's own
option through: a byte arriving while the guest has not read the previous
one destroys it, as the chip does, instead of being held at this end.  The
second paces a `--send' one byte at a time, because a burst written in a
single call is handed to the receiver a byte every 64 emulated instructions
-- faster than an interrupt can be taken, serviced and dismissed, so an
unpaced burst measures this file's write granularity and not the driver.
Both default off, and no target but verify-rxov passes either.

Exit status is the emulator's, so a target can treat this as it treats
`./c900' -- and `--send-after' never waits forever: if the text never
appears the bytes are simply never typed, and the run ends on its own
`--max' or `--stop-on'.
"""

import argparse
import os
import socket
import subprocess
import sys
import tempfile
import time


def unescape(s):
    """The \\r that a CP/M command line ends with, spelled portably."""
    return s.replace('\\r', '\r').replace('\\n', '\n').replace('\\t', '\t')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--emu', required=True,
                    help='emulator CHECKOUT directory (bin/c900 lives under it)')
    ap.add_argument('--disk', required=True)
    ap.add_argument('--input', default='', help='typed at console 0')
    ap.add_argument('--max', default='600000000')
    ap.add_argument('--stop-on', default='idle')
    ap.add_argument('--stop-mark', default='',
                    help="the emulator's --stop-mark: end the run the moment "
                         'this text is printed on ANY serial channel, rather '
                         'than waiting out the idle timer.  Empty (the '
                         'default) passes nothing, which is what every target '
                         'written before the mark existed does.')
    ap.add_argument('--input-mark', default='',
                    help="the emulator's --input-mark: hold the \\i byte of "
                         '--input until the guest has PRINTED this text, then '
                         'release it.  How a scripted ^C is aimed at a program '
                         'rather than at whatever happens to be reading.')
    ap.add_argument('--send', action='append', default=[],
                    help='typed at console 1; repeatable')
    ap.add_argument('--send-after', action='append', default=[],
                    help='...once this text has been printed on console 1')
    ap.add_argument('--send-delay', type=float, default=0.0,
                    help='seconds between the BYTES of a --send.  A burst '
                         'sent in one write is one socket write, and the '
                         'emulator hands the whole of it to the receiver a '
                         'byte every 64 instructions -- faster than any '
                         'driver can be expected to keep up with.  A delay '
                         'paces the wire instead, which is what a person or '
                         'a modem does; 0 (the default) is the old '
                         'behaviour and no existing target passes it.')
    ap.add_argument('--rx-overrun', action='store_true',
                    help="pass the emulator's --rx-overrun: a byte arriving "
                         'while the guest has not read the previous one '
                         'DESTROYS it, as the chip does.  Off by default '
                         'there and here.')
    ap.add_argument('--log', required=True, help='console 0 transcript')
    ap.add_argument('--wire-log', required=True, help='console 1 transcript')
    a = ap.parse_args()

    tmpdir = tempfile.mkdtemp(prefix='wirecon.')
    sockpath = os.path.join(tmpdir, 's')

    srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    srv.bind(sockpath)
    srv.listen(1)
    srv.settimeout(30.0)

    argv = ['./c900', '--disk=' + os.path.abspath(a.disk),
            '--max=' + a.max, '--wire=' + sockpath]
    if a.input:
        argv.append('--input=' + unescape(a.input))
    if a.stop_on:
        argv.append('--stop-on=' + a.stop_on)
    if a.stop_mark:
        argv.append('--stop-mark=' + unescape(a.stop_mark))
    if a.input_mark:
        argv.append('--input-mark=' + unescape(a.input_mark))
    if a.rx_overrun:
        argv.append('--rx-overrun')

    con0 = open(a.log, 'wb')
    proc = subprocess.Popen(argv, cwd=os.path.join(a.emu, 'bin'),
                            stdout=con0, stderr=subprocess.DEVNULL)

    wire = b''
    step = 0
    if len(a.send_after) < len(a.send):
        a.send_after += [''] * (len(a.send) - len(a.send_after))
    script = [(unescape(w).encode('latin-1'), unescape(s).encode('latin-1'))
              for w, s in zip(a.send_after, a.send)]
    seen = 0             # how much of the wire the current step may match in
    try:
        conn, _ = srv.accept()
    except socket.timeout:
        proc.kill()
        proc.wait()
        con0.close()
        open(a.wire_log, 'wb').close()
        print('wirecon: the emulator never connected to channel A',
              file=sys.stderr)
        return 3

    conn.settimeout(0.25)
    while True:
        try:
            b = conn.recv(4096)
            if b:
                wire += b
            elif proc.poll() is not None:
                break
        except socket.timeout:
            if proc.poll() is not None:
                break
        while step < len(script):
            want, send = script[step]
            if want and want not in wire[seen:]:
                break
            if want:
                seen += wire[seen:].index(want) + len(want)
            if send:
                if a.send_delay > 0:
                    for byte in send:
                        conn.sendall(bytes([byte]))
                        time.sleep(a.send_delay)
                else:
                    conn.sendall(send)
            step += 1

    rc = proc.wait()
    con0.close()
    conn.close()
    srv.close()
    with open(a.wire_log, 'wb') as f:
        f.write(wire)
    os.unlink(sockpath)
    os.rmdir(tmpdir)
    return rc


if __name__ == '__main__':
    sys.exit(main())
