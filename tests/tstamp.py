#!/usr/bin/env python3
"""Per-line wall-clock timestamper for emulator console transcripts.

Reads stdin byte-wise (the emulator emits console characters unbuffered)
and prefixes every completed line with seconds elapsed since start, so
phase wall times can be read off a scripted session (Makefile `selfhost`
target).  Carriage returns are dropped; a trailing unterminated line is
flushed at EOF.
"""
import sys
import time

t0 = time.time()
buf = bytearray()
fd = sys.stdin.buffer
out = sys.stdout


def flush():
    out.write("[%9.3f] %s\n" % (time.time() - t0, buf.decode("latin1")))
    out.flush()
    buf.clear()


while True:
    b = fd.read(1)
    if not b:
        break
    if b == b"\r":
        continue
    if b == b"\n":
        flush()
    else:
        buf += b
if buf:
    flush()
