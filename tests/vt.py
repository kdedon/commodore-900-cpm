#!/usr/bin/env python3
"""vt.py -- render a console transcript as a screen, and assert on cells.

The serial console's "screen" is the terminal on the far end, so the only
honest way to check that a character landed at a given row and column is to
be that terminal.  This is a small ANSI/VT100 model: it consumes the byte
stream the emulator wrote and produces an 80x25 grid, which the verify
targets then assert against by coordinate.  Asserting on the byte stream
alone would only prove that some bytes were emitted, not that anything was
positioned.

Understood: printable ASCII, BS, CR, LF, TAB, FF (form feed = home, which the
BIOS console layer passes through unchanged rather than translating -- the
C900 video ROM's own FF handler already homes the cursor, on both the LR and
HR consoles), and the CSI sequences the BIOS escape layer emits -- CUP (H),
CUU/CUD/CUF/CUB (A/B/C/D), ED (J, and 2J) and EL (K).  Anything else is
skipped, not drawn.

Usage:
    vt.py LOG --dump
    vt.py LOG --cell ROW COL TEXT ...   text must appear at exactly (ROW,COL)
    vt.py LOG --blank ROW COL N ...     N cells from (ROW,COL) must be blank
Exit status is 1 on the first failed assertion, and every assertion prints
the row it was checking so a failure says what the screen actually held.
"""

import sys

ROWS, COLS = 25, 80


class Screen:
    def __init__(self):
        self.g = [[' '] * COLS for _ in range(ROWS)]
        self.r = self.c = 0

    def scroll(self):
        self.g.pop(0)
        self.g.append([' '] * COLS)

    def put(self, ch):
        if self.c >= COLS:
            self.c = 0
            self.down()
        self.g[self.r][self.c] = ch
        self.c += 1

    def down(self):
        if self.r < ROWS - 1:
            self.r += 1
        else:
            self.scroll()

    def erase(self, r0, c0, n):
        r, c = r0, c0
        while n > 0 and r < ROWS:
            self.g[r][c] = ' '
            c += 1
            if c >= COLS:
                c = 0
                r += 1
            n -= 1

    def feed(self, data):
        i, n = 0, len(data)
        while i < n:
            b = data[i]
            i += 1
            if b == 0x1b:
                if i < n and data[i] == ord('['):
                    i += 1
                    p = ''
                    while i < n and chr(data[i]) in '0123456789;?':
                        p += chr(data[i])
                        i += 1
                    if i >= n:
                        break
                    self.csi(chr(data[i]), p)
                    i += 1
                else:
                    # ESC not followed by '[': one more byte belongs to it
                    i += 1
                continue
            if b == 0x0d:
                self.c = 0
            elif b == 0x0a:
                self.down()
            elif b == 0x08:
                if self.c > 0:
                    self.c -= 1
            elif b == 0x09:
                self.c = min(COLS - 1, (self.c // 8 + 1) * 8)
            elif b == 0x0c:
                self.r = self.c = 0
            elif 0x20 <= b < 0x7f:
                self.put(chr(b))

    def csi(self, final, p):
        args = [int(x) for x in p.split(';') if x.isdigit()]

        def a(i, d):
            return args[i] if len(args) > i and args[i] else d

        if final == 'H' or final == 'f':
            self.r = max(0, min(ROWS - 1, a(0, 1) - 1))
            self.c = max(0, min(COLS - 1, a(1, 1) - 1))
        elif final == 'A':
            self.r = max(0, self.r - a(0, 1))
        elif final == 'B':
            self.r = min(ROWS - 1, self.r + a(0, 1))
        elif final == 'C':
            self.c = min(COLS - 1, self.c + a(0, 1))
        elif final == 'D':
            self.c = max(0, self.c - a(0, 1))
        elif final == 'J':
            if args and args[0] == 2:
                self.g = [[' '] * COLS for _ in range(ROWS)]
            else:
                self.erase(self.r, self.c, (ROWS - self.r) * COLS - self.c)
        elif final == 'K':
            self.erase(self.r, self.c, COLS - self.c)

    def line(self, r):
        return ''.join(self.g[r]).rstrip()


def main(argv):
    if len(argv) < 3:
        sys.exit(__doc__)
    scr = Screen()
    scr.feed(open(argv[1], 'rb').read())
    args = argv[2:]
    bad = 0
    i = 0
    while i < len(args):
        op = args[i]
        if op == '--dump':
            for r in range(ROWS):
                print('%2d|%s' % (r, scr.line(r)))
            i += 1
            continue
        if op == '--cell':
            r, c, text = int(args[i + 1]), int(args[i + 2]), args[i + 3]
            got = ''.join(scr.g[r][c:c + len(text)])
            if got != text:
                print('vt: FAIL row %d col %d: want %r, got %r' % (r, c, text, got))
                print('vt:   row %d = %r' % (r, scr.line(r)))
                bad = 1
            else:
                print('vt: ok   row %d col %d = %r' % (r, c, text))
            i += 4
            continue
        if op == '--blank':
            r, c, cnt = int(args[i + 1]), int(args[i + 2]), int(args[i + 3])
            got = ''.join(scr.g[r][c:c + cnt])
            if got != ' ' * cnt:
                print('vt: FAIL row %d col %d: want %d blanks, got %r'
                      % (r, c, cnt, got))
                print('vt:   row %d = %r' % (r, scr.line(r)))
                bad = 1
            else:
                print('vt: ok   row %d col %d: %d blanks' % (r, c, cnt))
            i += 4
            continue
        sys.exit('vt: unknown option %s' % op)
    sys.exit(bad)


if __name__ == '__main__':
    main(sys.argv)
