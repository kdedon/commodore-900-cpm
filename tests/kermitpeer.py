#!/usr/bin/env python3
# Copyright (c) 2026 Kevin Dedon.
# SPDX-License-Identifier: MIT

"""kermitpeer.py -- the OTHER end of the wire for KERMIT.Z8K (task N2).

WHAT THIS IS, SAID PLAINLY SO NOBODY OVERSELLS IT.  This is a Kermit
protocol implementation written for this test, not an interop test
against somebody else's Kermit.  There is no gkermit, ckermit or kermit
binary on this build machine (`which gkermit kermit ckermit' finds
nothing, and no distribution package supplies one), so the honest test
the brief asked for -- a round trip against a host-side E-Kermit -- could
not be run here.  What IS run is a round trip against this file.

That is weaker evidence and the difference matters.  What it does prove:
the guest's packet framing, its checksum, its sequence numbering, its
control-quoting, its parameter negotiation and its file I/O all work,
and a file survives a trip out of the CP/M disk and back byte for byte.
What it does NOT prove: that this file agrees with the Kermit protocol
where this file and the protocol differ.  A real interop leg against
ckermit or gkermit remains to be run by anyone who has one; the guest
side needs no change for it, only a different program on the socket.

WHAT IT SPEAKS.  Classic Kermit: SOH, LEN, SEQ, TYPE, DATA, CHECK, CR,
all fields tochar()'d; block check type 1 (the six-bit checksum), control
prefixing with '#', no 8-bit prefixing (the line is 8-bit clean and this
peer says so in its negotiation), no repeat counts, no long packets, no
sliding windows.  Every one of those is a value this peer PROPOSES, so
the guest follows it -- Kermit's rule is that a sender uses the
parameters the receiver sent back.

HOW IT RUNS.  Like wirecon.py: it listens on an AF_UNIX socket, starts
the emulator with `--wire=' pointed at it, types one command at console
0, and then is the far end of the spare serial port for the rest of the
boot.  Console 0's transcript is captured the ordinary way.

    kermitpeer.py --emu DIR --disk IMG --mode send --file F --as NAME
    kermitpeer.py --emu DIR --disk IMG --mode recv --out F

`send' pushes a file at a guest running `KERMIT R'; `recv' catches one
from a guest running `KERMIT S'.  Exit status is 0 only if the whole
transfer completed; the caller compares the bytes.
"""

import argparse
import os
import socket
import subprocess
import sys
import tempfile
import time

SOH = 0x01
CR = 0x0d
MAXTRY = 10

def tochar(n):
    return (n + 32) & 0xff

def unchar(c):
    return (c - 32) & 0xff

def ctl(c):
    return c ^ 64


def chk1(data):
    """The type-1 block check over the bytes between SOH and the check."""
    s = sum(data) & 0xffff
    return tochar((((s & 0o300) >> 6) + s) & 0o77)


class Wire:
    """The socket, with a packet reader that resynchronises on SOH."""

    def __init__(self, conn, proc, trace=False, bytedelay=0.0):
        self.conn = conn
        self.proc = proc
        self.buf = bytearray()
        self.trace = trace
        self.bytedelay = bytedelay
        conn.settimeout(0.2)

    def fill(self):
        try:
            b = self.conn.recv(4096)
        except socket.timeout:
            return False
        except OSError:
            return False
        if not b:
            return False
        self.buf += b
        return True

    def send(self, data):
        """Write a packet, one byte at a time if a delay was asked for.

        WHY THE WIRE IS PACED.  A packet written in one call is handed to
        the guest's receiver a byte every 64 emulated instructions --
        very much faster than 38,400 baud, and faster than the guest can
        drain the BIOS's 64-character receive ring (src/bios/bios900.c,
        C8).  The ring then overflows in the middle of a packet, the
        checksum fails and the transfer NAKs forever.  That is the
        emulator's socket granularity showing through, not a property of
        a serial line: at the line's own rate a byte takes 260 us and
        the ring never fills.  So the peer paces, which is what
        wirecon.py does for the same reason (verify-rxov).
        """
        if self.trace:
            print('  > %r' % bytes(data), file=sys.stderr)
        if self.bytedelay > 0:
            for b in data:
                self.conn.sendall(bytes([b]))
                time.sleep(self.bytedelay)
        else:
            self.conn.sendall(bytes(data))

    def readpkt(self, timeout):
        """One packet as (seq, typ, data), or None on timeout.

        A packet whose check does not verify is dropped, exactly as the
        guest drops ours: the retry that follows is the protocol's, not
        this reader's.
        """
        end = time.time() + timeout
        while True:
            while True:
                i = self.buf.find(bytes([SOH]))
                if i < 0:
                    del self.buf[:]
                    break
                if i:
                    del self.buf[:i]
                if len(self.buf) < 2:
                    break
                n = unchar(self.buf[1])
                if n < 3 or n > 94:
                    del self.buf[:1]     # not a length: resynchronise
                    continue
                if len(self.buf) < n + 2:
                    break
                body = bytes(self.buf[1:n + 1])          # LEN..DATA
                got = self.buf[n + 1]
                del self.buf[:n + 2]
                while self.buf[:1] in (b'\r', b'\n'):
                    del self.buf[:1]
                if got != chk1(body):
                    continue                              # bad check: drop
                seq = unchar(body[1])
                typ = chr(body[2])
                data = body[3:]
                if self.trace:
                    print('  < %s seq=%d %r' % (typ, seq, data),
                          file=sys.stderr)
                return (seq, typ, data)
            if time.time() > end:
                return None
            if not self.fill() and self.proc.poll() is not None:
                return None

    def sendpkt(self, seq, typ, data=b''):
        body = bytearray()
        body.append(tochar(len(data) + 3))
        body.append(tochar(seq & 63))
        body.append(ord(typ))
        body += data
        self.send(bytes([SOH]) + bytes(body) + bytes([chk1(body), CR]))


def myparams():
    """The parameters this peer offers, which the guest then uses.

    MAXL 80, TIME 8 seconds, no padding, EOL = CR, control prefix '#',
    NO 8-bit prefixing (the line carries eight bits and pretending
    otherwise would only make the packets longer), block check 1, no
    repeat counts, no capabilities.
    """
    return bytes([tochar(80), tochar(8), tochar(0), ctl(0), tochar(CR),
                  ord('#'), ord('N'), ord('1'), 0x20, tochar(0)])


def encode(data, qctl=ord('#')):
    """Control-quote a run of bytes for a D packet.

    THE QUOTE CHARACTER IS QUOTED BY DOUBLING IT, not by ctl()ing it.
    ctl('#') is 'c', and a receiver un-ctl()s only the bytes whose low
    seven bits fall in 62..95 -- the range control characters and DEL
    map into.  'c' is outside it, so QCTL+ctl(QCTL) arrives as a literal
    'c' and the file is wrong in exactly the places it contained a '#'.
    """
    out = bytearray()
    for c in data:
        low = c & 0x7f
        if c == qctl:
            out.append(qctl)
            out.append(qctl)
        elif low < 32 or low == 127:
            out.append(qctl)
            out.append(ctl(c))
        else:
            out.append(c)
    return bytes(out)


def decode(data, qctl=ord('#')):
    out = bytearray()
    i = 0
    while i < len(data):
        c = data[i]
        i += 1
        if c == qctl and i < len(data):
            c = data[i]
            i += 1
            # The sender's own rule, read backwards: only the bytes a
            # control character can have become are turned back.
            a7 = c & 0x7f
            out.append(ctl(c) if 61 < a7 < 96 else c)
        else:
            out.append(c)
    return bytes(out)


def do_recv(w, out):
    """Catch a file from a guest running KERMIT S."""
    seq = 0
    got = bytearray()
    name = None
    tries = 0
    while True:
        p = w.readpkt(20.0)
        if p is None:
            tries += 1
            if tries > 3:
                return 'timed out waiting for a packet'
            continue
        tries = 0
        rseq, typ, data = p
        if rseq != seq:
            # a repeat of the packet we already took: re-ACK it and wait
            w.sendpkt(rseq, 'Y')
            continue
        if typ == 'S':
            w.sendpkt(seq, 'Y', myparams())
        elif typ == 'F':
            name = data.decode('latin-1')
            w.sendpkt(seq, 'Y')
        elif typ == 'A':
            w.sendpkt(seq, 'Y')
        elif typ == 'D':
            got += decode(data)
            w.sendpkt(seq, 'Y')
        elif typ == 'Z':
            w.sendpkt(seq, 'Y')
        elif typ == 'B':
            w.sendpkt(seq, 'Y')
            open(out, 'wb').write(bytes(got))
            print('kermitpeer: received %s, %d bytes' % (name, len(got)))
            return None
        elif typ == 'E':
            return 'the guest sent an Error packet: %r' % data
        else:
            return 'unexpected packet type %r' % typ
        seq = (seq + 1) & 63


def xchg(w, seq, typ, data):
    """Send one packet and wait for its ACK; the ACK's data is returned."""
    for _ in range(MAXTRY):
        w.sendpkt(seq, typ, data)
        p = w.readpkt(6.0)
        if p is None:
            continue
        rseq, rtyp, rdata = p
        if rtyp == 'E':
            return (None, 'the guest sent an Error packet: %r' % rdata)
        if rtyp == 'N':
            continue                        # NAK: send it again
        if rtyp == 'Y' and rseq == (seq & 63):
            return (rdata, None)
    return (None, 'no ACK for a %s packet after %d tries' % (typ, MAXTRY))


def do_send(w, path, name):
    """Push a file at a guest running KERMIT R."""
    body = open(path, 'rb').read()
    seq = 0
    ack, err = xchg(w, seq, 'S', myparams())
    if err:
        return err
    # The guest's own parameters come back in the ACK; we honour its
    # control prefix and its maximum packet length and nothing else,
    # because nothing else is negotiable in what this peer can do.
    qctl = ack[5] if len(ack) > 5 else ord('#')
    maxl = unchar(ack[0]) if len(ack) > 0 else 80
    room = max(20, min(maxl, 90) - 8)
    seq = (seq + 1) & 63
    _, err = xchg(w, seq, 'F', name.encode('latin-1'))
    if err:
        return err
    i = 0
    while i < len(body):
        # Encode by growing the field until it would not fit: a quoted
        # byte costs two, so a fixed byte count would overrun.
        field = bytearray()
        while i < len(body):
            e = encode(body[i:i + 1], qctl)
            if len(field) + len(e) > room:
                break
            field += e
            i += 1
        seq = (seq + 1) & 63
        _, err = xchg(w, seq, 'D', bytes(field))
        if err:
            return err
    seq = (seq + 1) & 63
    _, err = xchg(w, seq, 'Z', b'')
    if err:
        return err
    seq = (seq + 1) & 63
    _, err = xchg(w, seq, 'B', b'')
    if err:
        return err
    print('kermitpeer: sent %s, %d bytes' % (name, len(body)))
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--emu', required=True)
    ap.add_argument('--disk', required=True)
    ap.add_argument('--mode', required=True, choices=('send', 'recv'))
    ap.add_argument('--file', help='the file to send (--mode send)')
    ap.add_argument('--as', dest='asname', default='TEST.BIN')
    ap.add_argument('--out', help='where to write it (--mode recv)')
    ap.add_argument('--max', default='900000000')
    ap.add_argument('--log', required=True, help='console 0 transcript')
    ap.add_argument('--trace', action='store_true')
    ap.add_argument('--byte-delay', type=float, default=0.0006,
                    dest='bytedelay',
                    help='seconds between the bytes this peer writes; see '
                         'Wire.send().  0 writes each packet in one call, '
                         'which overruns the guest ring.')
    a = ap.parse_args()

    tmpdir = tempfile.mkdtemp(prefix='kermitpeer.')
    sockpath = os.path.join(tmpdir, 's')
    srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    srv.bind(sockpath)
    srv.listen(1)
    srv.settimeout(30.0)

    cmd = 'KERMIT R\r' if a.mode == 'send' else 'KERMIT S %s\r' % a.asname
    argv = ['./c900', '--disk=' + os.path.abspath(a.disk),
            '--max=' + a.max, '--wire=' + sockpath,
            '--input=' + cmd, '--stop-on=idle']

    con0 = open(a.log, 'wb')
    proc = subprocess.Popen(argv, cwd=os.path.join(a.emu, 'bin'),
                            stdout=con0, stderr=subprocess.DEVNULL)
    err = None
    try:
        conn, _ = srv.accept()
    except socket.timeout:
        proc.kill()
        proc.wait()
        con0.close()
        print('kermitpeer: the emulator never connected to the wire',
              file=sys.stderr)
        return 3

    w = Wire(conn, proc, a.trace, a.bytedelay)
    try:
        if a.mode == 'send':
            err = do_send(w, a.file, a.asname)
        else:
            err = do_recv(w, a.out)
    finally:
        # Let the guest finish printing before the machine is stopped, so
        # the transcript shows how the program ended.
        deadline = time.time() + 10.0
        while proc.poll() is None and time.time() < deadline:
            w.fill()
        if proc.poll() is None:
            proc.kill()
        proc.wait()
        conn.close()
        srv.close()
        con0.close()
        os.unlink(sockpath)
        os.rmdir(tmpdir)

    if err:
        print('kermitpeer: ' + err, file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
