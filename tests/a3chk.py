#!/usr/bin/env python3
# Copyright (c) 2026 Kevin Dedon.
# SPDX-License-Identifier: MIT

"""a3chk.py -- SORTFL, KILLDU, TOHEX and FROMHEX did their jobs on the machine.

Usage: a3chk.py [--cpmsys] RUNLOG ARTDIR

RUNLOG is the transcript of the boot that ran, on drive A:,

    TOHEX SDB.Z8K >A3.HEX
    FROMHEX A3.HEX A3.BIN
    SORTFL <README.TXT >A3S.TXT
    KILLDU <A3S.TXT >A3K.TXT

and ARTDIR the directory its cpma partition was extracted into.  Every
answer is checked off the disk against one worked out here:

  A3.HEX   decoded here, record by record, each checksum and sequence
           number checked, must be SDB.Z8K.  SDB.Z8K is 48K, so the text
           TOHEX writes through `>' is 113K: four directory entries,
           which is what exercises a random write crossing an extent.
  A3.BIN   FROMHEX's binary output must be SDB.Z8K byte for byte: CP/M
           files end on a record boundary, and so do both.
  A3S.TXT  README.TXT's lines in strcmp order.
  A3K.TXT  those lines with adjacent duplicates dropped -- README.TXT has
           several blank lines, so there are duplicates to drop.

With --cpmsys the programs are the host-built ones, over src/cmd/cpmsys.c,
and the run went on with

    CPMSYST ARGS A3?.TXT A3.* NOSUCH?.* PLAIN A:SDB.Z?K >B3W.TXT
    SORTFL <README.TXT >B3A.TXT
    KILLDU <A3S.TXT >>B3A.TXT
    KILLDU <A3S.TXT >>B3N.TXT
    CPMSYST ODD 1001 B3O.BIN
    TOHEX B3O.BIN >B3O.HEX
    FROMHEX B3O.HEX B3P.BIN

  B3W.TXT  each wildcard argument as the names matching it on the disk, in
           directory order, and one matching nothing left as it was.
  B3A.TXT  A3S.TXT's lines and then A3K.TXT's: `>>' appended before the ^Z.
  B3N.TXT  A3K.TXT's lines: `>>' made the file.
  B3O.BIN, B3P.BIN
           1001 pattern bytes exactly, as is what B3O.HEX decodes to:
           binary files keep their CP/M 3 last record byte count, and the
           transcript shows lseek to the end and a full read agreeing.

Text files are compared as DRI's C reads them: CR dropped, ^Z the end.
"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '..', 'tools'))
import mkcpmfs                                           # noqa: E402

WILDARGS = ('A3?.TXT', 'A3.*', 'NOSUCH?.*', 'PLAIN', 'A:SDB.Z?K')
ODDLEN = 1001


def text_lines(path):
    with open(path, 'rb') as f:
        data = f.read()
    data = data.split(b'\x1a', 1)[0].replace(b'\r', b'')
    lines = data.split(b'\n')
    if lines and lines[-1] == b'':
        lines.pop()
    return lines


def unhex(path):
    """TOHEX's format: ;LLRRRR<LL data bytes>CCCC per line, the checksum
    the sum of length, both sequence-number bytes and the data; a
    zero-length record ends the file."""
    out = bytearray()
    want = 0
    with open(path, 'rb') as f:
        text = f.read().split(b'\x1a', 1)[0].decode('ascii')
    for n, line in enumerate(text.replace('\r', '').split('\n'), 1):
        if not line:
            continue
        if line[0] != ';':
            raise ValueError("line %d does not start with ';'" % n)
        ln, rec = int(line[1:3], 16), int(line[3:7], 16)
        body = line[7:7 + 2 * ln]
        data = bytes(int(body[i:i + 2], 16) for i in range(0, 2 * ln, 2))
        chk = int(line[7 + 2 * ln:11 + 2 * ln], 16)
        if len(line) != 11 + 2 * ln:
            raise ValueError("line %d is %d characters, not %d"
                             % (n, len(line), 11 + 2 * ln))
        if rec != want:
            raise ValueError("line %d is record %04x, expected %04x"
                             % (n, rec, want))
        if chk != (ln + (rec & 0xff) + (rec >> 8) + sum(data)) & 0xffff:
            raise ValueError("line %d checksum %04x is wrong" % (n, chk))
        if ln == 0:
            return bytes(out)
        out += data
        want += 1
    raise ValueError("no zero-length record at the end")


def dir_names(img, pattern):
    """The user-0 files matching an 8.3 wildcard, in directory order."""
    drive = ''
    if pattern[1:2] == ':':
        drive, pattern = pattern[:2], pattern[2:]
    name, _, ext = pattern.partition('.')

    def field(s, n):                    # `*' fills the rest with `?'
        return s.split('*')[0].ljust(n, '?') if '*' in s else s.ljust(n)
    pat11 = field(name, 8) + field(ext, 3)
    out = []
    for i in range(mkcpmfs.NENT):
        e = img[i * 32:(i + 1) * 32]
        if e[0] != 0:
            continue
        ext_total = ((e[14] & 0x3f) << 5) | (e[12] & 0x1f)
        if ext_total >> 1 != 0:
            continue
        n11 = bytes(c & 0x7f for c in e[1:12]).decode('latin-1')
        if all(p == '?' or p == c for p, c in zip(pat11, n11)):
            base, typ = n11[:8].rstrip(), n11[8:].rstrip()
            out.append(drive + base + ('.' + typ if typ else ''))
    return out


def main():
    args = sys.argv[1:]
    cpmsys = args[:1] == ['--cpmsys']
    if cpmsys:
        args = args[1:]
    if len(args) != 2:
        sys.stderr.write("usage: a3chk.py [--cpmsys] RUNLOG ARTDIR\n")
        return 2
    log, art = args
    fails = []

    def ok(msg):
        print("  ok  " + msg)

    def bad(msg):
        print("FAIL " + msg)
        fails.append(msg)

    with open(log, 'rb') as f:
        transcript = f.read().decode('latin-1')
    for word in ('Usage', 'error', 'Phase', 'Buffer overflow',
                 'out of memory', 'more than'):
        if word in transcript:
            bad("the transcript says %r" % word)

    def path(name):
        p = os.path.join(art, name)
        if not os.path.isfile(p):
            bad("%s is not on the disk" % name)
            return None
        return p

    sdb = path('SDB.Z8K')
    hexf = path('A3.HEX')
    if sdb and hexf:
        with open(sdb, 'rb') as f:
            image = f.read()
        try:
            if unhex(hexf) == image:
                ok("A3.HEX decodes to SDB.Z8K, %d bytes" % len(image))
            else:
                bad("A3.HEX decodes to something other than SDB.Z8K")
        except ValueError as e:
            bad("A3.HEX: %s" % e)
        binf = path('A3.BIN')
        if binf:
            with open(binf, 'rb') as f:
                if f.read() == image:
                    ok("A3.BIN is SDB.Z8K again, byte for byte")
                else:
                    bad("A3.BIN differs from SDB.Z8K")

    readme = path('README.TXT')
    srt = path('A3S.TXT')
    kil = path('A3K.TXT')
    if readme and srt:
        want = sorted(text_lines(readme))
        if text_lines(srt) == want:
            ok("A3S.TXT is README.TXT's %d lines sorted" % len(want))
        else:
            bad("A3S.TXT is not README.TXT sorted")
        if kil:
            uniq = [l for i, l in enumerate(want) if i == 0 or l != want[i - 1]]
            if len(uniq) == len(want):
                bad("README.TXT has no adjacent duplicates once sorted: "
                    "KILLDU would be tested on nothing")
            elif text_lines(kil) == uniq:
                ok("A3K.TXT drops %d adjacent duplicates"
                   % (len(want) - len(uniq)))
            else:
                bad("A3K.TXT is not A3S.TXT with adjacent duplicates dropped")

    if cpmsys:
        with open(os.path.join(art, 'cpma.img'), 'rb') as f:
            img = f.read(mkcpmfs.NENT * 32)
        want = []
        for a in WILDARGS:
            want += (('?' in a or '*' in a) and dir_names(img, a)) or [a]
        w = path('B3W.TXT')
        if w:
            got = [l.decode('latin-1') for l in text_lines(w)]
            if got == want:
                ok("B3W.TXT expands the wildcards: %s" % ' '.join(got))
            else:
                bad("B3W.TXT is %r, not %r" % (got, want))

        if srt and kil:
            a3s, a3k = text_lines(srt), text_lines(kil)
            for name, lines in (('B3A.TXT', a3s + a3k), ('B3N.TXT', a3k)):
                p = path(name)
                if p and text_lines(p) == lines:
                    ok("%s is %d lines: `>>' appended" % (name, len(lines)))
                elif p:
                    bad("%s is not the %d lines `>>' should have left"
                        % (name, len(lines)))

        pattern = bytes((i * 7 + 3) & 0xff for i in range(ODDLEN))
        for name in ('B3O.BIN', 'B3P.BIN'):
            p = path(name)
            if p:
                with open(p, 'rb') as f:
                    data = f.read()
                if data == pattern:
                    ok("%s is the %d pattern bytes exactly" % (name, ODDLEN))
                else:
                    bad("%s is %d bytes, not the %d pattern bytes"
                        % (name, len(data), ODDLEN))
        p = path('B3O.HEX')
        if p:
            try:
                data = unhex(p)
                if data == pattern:
                    ok("B3O.HEX decodes to the %d bytes" % ODDLEN)
                else:
                    bad("B3O.HEX decodes to %d bytes, not the %d pattern "
                        "bytes" % (len(data), ODDLEN))
            except ValueError as e:
                bad("B3O.HEX: %s" % e)
        line = "ODD B3O.BIN lseek %d read %d" % (ODDLEN, ODDLEN)
        if line in transcript:
            ok("the transcript says %r" % line)
        else:
            bad("the transcript does not say %r" % line)

    if fails:
        print("a3chk: FAIL -- %d assertion(s)" % len(fails))
        return 1
    print("a3chk: PASS -- TOHEX, FROMHEX, SORTFL and KILLDU gave the right "
          "answers")
    return 0


if __name__ == '__main__':
    sys.exit(main())
