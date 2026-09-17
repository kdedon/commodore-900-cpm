#!/usr/bin/env python3
# Copyright (c) 2026 Kevin Dedon.
# SPDX-License-Identifier: MIT

"""signoncount.py - assert the BDOS sign-on appears exactly once per boot.

Usage: signoncount.py SRC HEADER LOG WARMBOOTS

SRC is src/bdos/bdosmisc.c, HEADER the generated build/obj/cpmver.h that holds
the two banner lines the build supplies, LOG a console transcript of one
cold boot, and WARMBOOTS the number of transient programs the session
ran.

The sign-on is printed by bdosinit(), and ccpif.s's `tsetb sysinit; jr
mi, ccploop' latch means bdosinit() runs on the cold start only: BIOS
function 1 (WBOOT) re-enters the CCP at `ccp' without reloading cpm.sys,
so a warm boot must yield the prompt alone.  A transcript in which the
identification appears again after a transient program is the symptom of
that latch not holding.

The expected text is read out of bdosinit()'s own prt_line() calls
rather than spelled out here, so the check tracks whatever the banner
currently says (version, date, copyright lines) and only ever tests how
many times it was printed.

Two ways to be wrong are both failures: more than one occurrence (the
latch is not holding) and fewer than one (the banner never printed, or
the session never got that far).  A session that ran no transient
program cannot observe a warm boot at all, so WARMBOOTS below 2 is also
a failure -- it would let the count pass vacuously.
"""

import re
import sys

# C escapes bdosinit()'s strings actually use; '\014' is the leading form
# feed, which the ROM console consumes rather than echoing (see
# ../docs -- serial passes it to the terminal, the LR/HR video handler
# homes the cursor), so it can never appear in a transcript line.
ESCAPES = {
    "n": "\n", "r": "\r", "t": "\t", "f": "\f",
    "\\": "\\", '"': '"', "'": "'", "0": "\0",
}


def unescape(s):
    """Decode the C string escapes in a source literal."""
    out = []
    i = 0
    while i < len(s):
        c = s[i]
        if c != "\\":
            out.append(c)
            i += 1
            continue
        i += 1
        if i < len(s) and s[i].isdigit():        # octal, up to 3 digits
            j = i
            while j < len(s) and j < i + 3 and s[j] in "01234567":
                j += 1
            out.append(chr(int(s[i:j], 8)))
            i = j
        else:
            out.append(ESCAPES.get(s[i], s[i]))
            i += 1
    return "".join(out)


def macros(path):
    """#define NAME "literal" from the generated build/obj/cpmver.h.

    Two of bdosinit()'s three lines carry a version or a year and so are
    built rather than written: the Makefile's CPMVER block generates
    cpmver.h with SYS_BANNER and SYS_COPYRIGHT as whole literals.  Reading
    the header here keeps this check covering all three lines -- resolving
    them, rather than skipping what it cannot read, is what stops a macro
    from quietly shrinking the check to the one line still spelled out in
    the source.
    """
    out = {}
    try:
        text = open(path).read()
    except OSError:
        return out
    for name, lit in re.findall(
            r'#define\s+(\w+)\s+"((?:[^"\\]|\\.)*)"', text):
        out[name] = lit
    return out


def signon_lines(path, header):
    """The printable lines bdosinit() sends to the console, in order."""
    src = open(path).read()
    start = src.index("\nbdosinit()")
    end = src.index("\n}", start)
    body = src[start:end]
    defs = macros(header)
    lines = []
    for lit, name in re.findall(
            r'prt_line\(\s*(?:"((?:[^"\\]|\\.)*)"|(\w+))\s*\)', body):
        if not lit:
            if name not in defs:
                sys.exit("signoncount: FAIL -- prt_line(%s) in bdosinit() "
                         "is not defined in %s, so the sign-on it prints "
                         "would go unchecked" % (name, header))
            lit = defs[name]
        text = unescape(lit)
        if text.endswith("$"):                   # BDOS string terminator
            text = text[:-1]
        for piece in re.split(r"[\r\n\f]+", text):
            if piece.strip():
                lines.append(piece.strip())
    return lines


def main():
    if len(sys.argv) != 5:
        sys.exit("usage: signoncount.py SRC HEADER LOG WARMBOOTS")
    src, header, log = sys.argv[1], sys.argv[2], sys.argv[3]
    warmboots = int(sys.argv[4])

    lines = signon_lines(src, header)
    if not lines:
        sys.exit("signoncount: FAIL -- no prt_line() banner found in %s" % src)

    text = open(log, errors="replace").read()
    bad = False
    if warmboots < 2:
        print("signoncount: FAIL -- session ran %d transient programs; "
              "at least 2 warm boots are needed to observe a reprint"
              % warmboots)
        bad = True
    for line in lines:
        n = text.count(line)
        print("signoncount: %d x %r" % (n, line))
        if n != 1:
            why = "never printed" if n == 0 else "reprinted after a warm boot"
            print("signoncount: FAIL -- %r appears %d times (%s)"
                  % (line, n, why))
            bad = True
    if bad:
        sys.exit(1)
    print("signoncount: PASS -- sign-on printed once across %d warm boots"
          % warmboots)


main()
