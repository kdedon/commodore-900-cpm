#!/usr/bin/env python3
"""dri-sfcb-check.py -- check on-disk stamps against DRI's own layout,
read (not copied) from c900oses/cpm8000/ref/incoming/cpm3src/:
  XFCB.LIT: xfcb$type=10h, xf$create=24, xf$update=28 (4-byte stamps);
    dirlabeltype=20h shares this shape (label create/update at 24/28).
  BDOS30.ASM:3314-3327 get$dtba: SFCB (type 21h) is the 4th 32-byte item
    of a 128-byte dir sector (offset 96).  A dir item at group position
    p (0,1,2 -- p==3 IS the SFCB) has create/update at SFCB-relative
    offset 1+10*p+{0,4}.
  DATE.PLM:285-286 base$year=78,base$day=0 -> day count since 31-Dec-77,
    so day 1 = 1-Jan-1978 (LE word, then BCD hour, BCD minute).
Reimplements this off raw disk bytes, independent of mkcpmfs.py/our BDOS
C source, so it can catch a layout that is merely self-consistent.
CPM3-PORT-PLAN.md section 9 row 9.
"""
import datetime
import sys

ENTSIZE, PER_SECTOR, T_SFCB, T_LABEL = 32, 4, 0x21, 0x20
EPOCH = datetime.date(1978, 1, 1)


def decode(b4):
    day = b4[0] | (b4[1] << 8)
    if day == 0:
        return None
    hh = "%d%d" % (b4[2] >> 4, b4[2] & 0xF)
    mm = "%d%d" % (b4[3] >> 4, b4[3] & 0xF)
    d = EPOCH + datetime.timedelta(days=day - 1)
    return "%04d-%02d-%02d %s:%s" % (d.year, d.month, d.day, hh, mm)


def matches(e, n8, e3):
    return (bytes(c & 0x7F for c in e[1:9]) == n8 and
            bytes(c & 0x7F for c in e[9:12]) == e3)


def main():
    data = open(sys.argv[1], "rb").read()
    ok = True
    for spec in sys.argv[2:]:
        name, ext, want_c, want_u = spec.split("|")
        n8, e3, found = name.encode().ljust(8), ext.encode().ljust(3), False
        for i in range(len(data) // ENTSIZE):
            e = data[i * ENTSIZE:(i + 1) * ENTSIZE]
            if e[0] in (0xE5, T_SFCB, T_LABEL) or not matches(e, n8, e3):
                continue
            found = True
            group, p = divmod(i, PER_SECTOR)
            sfcb = data[group * 128 + 96:group * 128 + 128]
            if sfcb[0] != T_SFCB:
                print("FAIL %s.%s: no SFCB at group offset 96 (byte=%#x)" % (name, ext, sfcb[0]))
                ok = False
                continue
            got_c = decode(sfcb[1 + 10 * p:1 + 10 * p + 4])
            got_u = decode(sfcb[1 + 10 * p + 4:1 + 10 * p + 8])
            for lbl, want, got in (("create", want_c, got_c), ("update", want_u, got_u)):
                good = want and got and got.startswith(want)
                print("%s %s.%s %s = %r (want %r) DRI offset 1+10*%d+%s"
                      % ("ok  " if good else "FAIL", name, ext, lbl, got, want,
                         p, "0" if lbl == "create" else "4"))
                ok = ok and good
        if not found:
            print("FAIL %s.%s: no directory entry found" % (name, ext))
            ok = False
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
