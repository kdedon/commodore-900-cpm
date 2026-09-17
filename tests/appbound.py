#!/usr/bin/env python3
# Copyright (c) 2026 Kevin Dedon.
# SPDX-License-Identifier: MIT

"""appbound.py -- build the malformed inputs tests/appbound.sh feeds to the
src/app/ tools compiled for the host under -fsanitize=address.

Every fixture is named after the defect it reaches, and each one was first
confirmed against the UNFIXED source: the review's P1 #17, #18 and #19 and
the INT.C, IO.C and IEX.C items in its P2 list.  The comments record WHICH
INPUT REACHES WHICH WRITE, because that is the part a later reader cannot
re-derive from the fixture bytes.

Usage: appbound.py <outdir>
"""

import os
import struct
import sys

# ---- the SDB relation file header (src/app/SDBIO.H struct header) --------
#
# 512 bytes: hd_tcnt[2] hd_tmax[2] hd_data[2] hd_size[2] hd_unused[8], then
# NATTRS=31 sixteen-byte attribute entries (at_name[10] at_type at_size
# at_scale at_unused[3]).  Words are big-endian -- src/app/IO.C db_cvword()
# under CPM68K.  TCHAR is 1, TNUM is 2.

RNSIZE = 10
ASIZE = 16
NATTRS = 31
TCHAR, TNUM = 1, 2


def be(n):
    return struct.pack(">H", n & 0xFFFF)


def attr(name, typ, size, scale=0):
    return (name.encode()[:RNSIZE].ljust(RNSIZE, b"\0")
            + bytes([typ & 0xFF, size & 0xFF, scale & 0xFF])
            + bytes(ASIZE - RNSIZE - 3))


def relation(attrs, tcnt, tmax, size, data=512, tuples=b""):
    """A whole .sdb file.  `size' is what the HEADER SAYS each tuple is,
    which is the point of most of these: it need not agree with `attrs'."""
    hdr = be(tcnt) + be(tmax) + be(data) + be(size) + bytes(8)
    for a in attrs:
        hdr += a
    hdr += bytes(ASIZE) * (NATTRS - len(attrs))
    assert len(hdr) == 512, len(hdr)
    return hdr + tuples


EMP = [attr("name", TCHAR, 10), attr("dept", TCHAR, 6), attr("sal", TNUM, 6)]
EMPSIZE = 1 + 10 + 6 + 6          # status byte + the three attributes

ROWS = [("SMITH", "ENG", "1000"), ("JONES", "OPS", "2500"),
        ("CLARKE", "QA", "1750")]


def emptuples(rows):
    """ACTIVE tuples in the EMP shape, laid out as db_rstore() writes them."""
    out = b""
    for name, dept, sal in rows:
        out += bytes([1])                       # ACTIVE
        out += name.encode().ljust(10, b"\0")
        out += dept.encode().ljust(6, b"\0")
        out += sal.encode().rjust(6, b" ")
    return out


# ---- the nested boolean expression that reaches INT.C's stack -----------
#
# src/app/COM.C compiles left-associatively, so `a>1 & a>1 & ...' never
# holds more than three operands at once.  What grows the interpreter stack
# is nesting on the RIGHT, and the cheapest level is a BARE factor with no
# comparison in it -- relat()'s comparison loop is optional, so
# `"x" & ("x" & (...))' costs one push (two code cells) plus one `&' (one
# cell) per level against CODEMAX's hundred cells.  Three cells a level
# reaches depth 33; a level built from a comparison costs six cells and
# tops out at depth 17, below STACKMAX.  THAT is why this fixture is
# written with literals instead of comparisons: with comparisons the defect
# is not reachable at all, because CODEMAX gets there first.

def nest(n):
    return '"x"' + ' & ("x"' * n + ')' * n


def write(path, data):
    with open(path, "wb" if isinstance(data, bytes) else "w") as f:
        f.write(data)


def main():
    d = sys.argv[1]
    os.makedirs(d, exist_ok=True)

    def p(n):
        return os.path.join(d, n)

    # ---- (a) src/app/FROMHEX.C ------------------------------------------
    #
    # noeof.hex: not one semicolon in the file.  FROMHEX.C's
    #     while (getc(infile) != ';') ;
    # never tests EOF, so getc() returns -1 for ever and the program never
    # terminates.  The object of this fixture is TERMINATION; appbound.sh
    # bounds it with timeout and the exit status is the assertion.
    write(p("noeof.hex"), "no semicolon anywhere in this file\n")

    # big.hex: one otherwise well-formed record whose length byte says
    # 0x40, twice the 32-byte block_buff.  Each decoded byte is stored
    # BEFORE the limit is tested, so byte 33 is written one past the array
    # and only then is the overflow announced.
    n = 0x40
    body = bytes(range(n))
    write(p("big.hex"), ";%02X%04X%s%04X\n"
          % (n, 0, "".join("%02X" % b for b in body), (n + sum(body)) & 0xFFFF))

    # good.hex: a 32-byte record, the largest that fits, followed by the
    # zero-length record src/app/TOHEX.C:35 always writes last -- that
    # record, not end of file, is how a well-formed stream says it is over,
    # which is why FROMHEX got away with never testing EOF.  A bound that
    # refuses this fixture is not a fix.
    body = bytes(range(32))
    write(p("good.hex"), ";%02X%04X%s%04X\n;%02X%04X%04X\n"
          % (32, 0, "".join("%02X" % b for b in body),
             (32 + sum(body)) & 0xFFFF, 0, 1, 1))
    write(p("good.bin"), body)

    # ---- (c) src/app/SORTFL.C and (d) src/app/KILLDU.C ------------------
    #
    # long.txt: a 200-character line against the 128-byte line[] in both
    # tools.  many.txt: 1,001 lines against SORTFL's 1,000-entry lines[].
    # sort.txt: ordinary input, which must still sort (and, for KILLDU,
    # still drop the adjacent duplicate).
    write(p("long.txt"), "x" * 200 + "\n")
    write(p("many.txt"), "a\n" * 1001)
    write(p("sort.txt"), "cherry\napple\nbanana\napple\n")

    # ---- (b) src/app/CMD.C get_aname() ---------------------------------
    #
    # long.frm: a form carrying a 36-character attribute name between < and
    # >, against the 11-byte aname[ANSIZE+1] in form().
    write(p("long.frm"), "Report: <abcdefghijklmnopqrstuvwxyz0123456789>\n")
    # noterm.frm: a `<' with no `>' before end of file.  getc() returns -1,
    # which is neither '>' nor a space, so the store runs on past the
    # buffer for as long as the process lives.  Not a hang -- a write.
    write(p("noterm.frm"), "Report: <name")
    # good.frm: names that fit, so the bound is shown not to break forms.
    # The `$' is get_aname()'s own no-padding flag -- without it put_avalue()
    # pads the value out to the attribute width and the substitution is
    # harder to assert on than it is worth.
    write(p("good.frm"), "<$name> in <$dept>\n")

    # ---- (e) src/app/INT.C db_xpush() ----------------------------------
    write(p("deep.rel"), relation(EMP, 3, 20, EMPSIZE,
                                  tuples=emptuples(ROWS)))
    write(p("deep.in"), "print * from deep where %s ;\nexit\n" % nest(30))
    write(p("shallow.in"),
          'print * from deep where deep.sal > "1500" ;\nexit\n')

    # ---- (f) src/app/IO.C rfind() and db_ropen() -----------------------
    #
    # small.rel: the header says each tuple is four bytes; the attribute
    # table in the SAME header describes twenty-three.  db_ropen() mallocs
    # four and db_aget() indexes twenty-three.
    write(p("small.rel"), relation(EMP, 3, 20, 4, tuples=emptuples(ROWS)))
    # wide.rel: honest 23-byte tuples, attributes of 120 bytes each.  The
    # sizes are under 128 on purpose -- at_size is a signed char, so 250
    # reads back as -6 and every copy loop is simply empty, which is a
    # fixture that proves nothing.
    wide = [attr("name", TCHAR, 120), attr("dept", TCHAR, 120),
            attr("sal", TNUM, 120)]
    write(p("wide.rel"), relation(wide, 3, 20, EMPSIZE,
                                  tuples=emptuples(ROWS)))
    # zero.rel: a zero tuple size, so the buffer is malloc(0).
    write(p("zero.rel"), relation(EMP, 3, 20, 0, tuples=emptuples(ROWS)))
    # short.rel: a header block shorter than the 512 bytes rfind() reads.
    write(p("short.rel"), relation(EMP, 3, 20, EMPSIZE)[:200])
    # good.rel: the same relation with an honest header, the control.
    write(p("good.rel"), relation(EMP, 3, 20, EMPSIZE,
                                  tuples=emptuples(ROWS)))
    for r in ("small", "wide", "zero", "short", "good"):
        write(p(r + ".in"), "print * from %s ;\nexit\n" % r)

    # ---- (g) src/app/IEX.C db_import() ---------------------------------
    #
    # nonl.txt: the last value has no newline after it.  IEX.C's
    #     avalue[strlen(avalue)-1] = EOS;
    # removes the last byte whatever it is, so 1000 is imported as 100 --
    # and the command reports success.  The object of this one is the DATA
    # in the relation, not a return code.
    write(p("nonl.txt"), "SMITH\nENG\n1000")
    # nul.txt: a value line whose first byte is NUL.  strlen() is then 0,
    # the subscript is -1, and the store lands one byte BEFORE avalue.
    write(p("nul.txt"), b"SMITH\nENG\n\x001000\n")
    # wide.txt: a 200-character value against fgets()'s 132-byte limit.
    # The tail that would not fit is read as the NEXT attribute's value.
    write(p("wide.txt"), "SMITH\nENG\n" + "9" * 200 + "\n")
    # good.txt: three ordinary values, the control.
    write(p("good.txt"), "SMITH\nENG\n1000\n")
    # imp.rel is an empty EMP relation; appbound.sh copies it to imp<n>.sdb
    # before each import so that every import starts from no tuples.
    write(p("imp.rel"), relation(EMP, 0, 20, EMPSIZE))
    for r in ("nonl", "nul", "wide", "good"):
        write(p("imp" + r + ".in"),
              'import "%s.txt" into imp%s\nprint * from imp%s ;\nexit\n'
              % (r, r, r))

    print("appbound.py: fixtures written to %s" % d)


if __name__ == "__main__":
    main()
