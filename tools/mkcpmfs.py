#!/usr/bin/env python3
# Copyright (c) 2026 Kevin Dedon.
# SPDX-License-Identifier: MIT

"""mkcpmfs.py - CP/M filesystem packer/reader for the C900 drive A: image.

Pack:    mkcpmfs.py [opts] <img> <blocks> <srcdir>  (blocks = 512-byte; 20480 = 10 MB)
List:    mkcpmfs.py --list <img>
Entries: mkcpmfs.py --entries <img>                 (raw directory entries, decoded)
Extract: mkcpmfs.py --extract <img> <destdir>
Edit:    mkcpmfs.py [opts] <img>                    (apply --initdir/--label in place)

Options (pack and edit):
  --initdir             reserve every 4th directory entry as an SFCB (type 21h),
                        the INITDIR analog: the BDOS never creates these itself
  --label NAME          create or update the type-20h directory label
  --label-mode M[,M...] label stamping/password bits: create, update, access,
                        password, none (default: create,update)
  --stamp-date S        ISO 'YYYY-MM-DD' or 'YYYY-MM-DDTHH:MM' for the label's
                        own stamps (default: zero, so output stays deterministic)
  --xfcb N[:MODE[:PW]]  put a type-1xh password XFCB on the disk for file N,
                        and copy MODE into that file's SFCB password-mode byte
                        the way INITDIR does.  Nothing here (or in the BDOS)
                        enforces a password: this exists so the preservation
                        and high-water-mark rules for XFCBs can be tested with
                        an XFCB actually on the disk.

Geometry (must match the C900 CP/M BIOS DPB):
  128-byte logical records, 4 per 512-byte physical sector; BLS=4096
  (BSH=5 BLM=31 EXM=1), DRM=511 (directory = first 4 allocation blocks =
  16 KB), skew=0 (SECTRAN identity), CKS=0.  DSM comes from the image size
  (A: is 20480 blocks -> DSM 2559, B: is 16384 -> DSM 2047).  OFF/trk_off is
  where the drive sits on the DEVICE and so never appears in an image: the
  caller carves the region out of the disk before handing it here.

On-disk format is CP/M 2.2 as interpreted by the CP/M-8000 may83 BDOS
(c900oses/cpm8000/ref/may83/bdos/dskutil.c, fileio.c, bdosrw.c), which is the consumer:
  - 32-byte directory entries: UU F1-8 T1-3 EX S1 S2 RC AL[16 bytes].
  - DSM>255, so AL is EIGHT 16-bit block numbers, LITTLE-endian on disk
    (fileio.c alloc()/bdosrw.c blknum() apply swap() before use).
  - One entry maps 8 blocks = 32 KB = 2 logical extents (EXM=1).
    ext_total = ((S2&0x3f)<<5)|(EX&0x1f); entry k has ext_total 2k or 2k+1.
  - RC = 128-byte records in the LAST logical extent covered by the entry.
    A 16 KB-aligned final chunk gets EX=2k, RC=0x80 (NOT EX=2k+1, RC=0):
    bdosrw.c calcext() derives the sub-extent from the last non-zero AL
    slot (4 blocks -> +0), and get_rc() returns 0 for extents past
    calcext -- EX=2k/RC=0x80 is what the BDOS itself writes and the only
    encoding its EOF logic accepts.
  - Free entries are 0xE5-filled; user byte 0xE5 = free, >=0x10 skipped
    (XFCBs, fileio.c alloc()).

CP/M 3 directory extensions (type bytes 10h XFCB, 20h label, 21h SFCB) are
carried per the format both this packer and the BDOS agree on.  They are
invisible to the 2.2-era BDOS, which allocates no blocks from them
(fileio.c:71) and claims only entries whose type byte is exactly 0xE5
(fileio.c:401), so a stamped image still runs on the unmodified system.
Every code path here preserves entries whose type byte is neither a user
number (0x00-0x0F) nor 0xE5, including types this file does not understand.

Deterministic output: no file timestamps, files packed in sorted 8.3-name order,
blocks allocated sequentially from block 4 (first data block after the
directory), all free space zeroed.

EOF padding: CP/M sizes files in 128-byte records, so the packer must pad
the final partial record.  TEXT files (by extension -- see TEXT_EXTS) get
0x1A (^Z) fill, the classic CP/M text-EOF convention (TYPE/ED/PIP stop at
^Z instead of printing trailing NULs).  Everything else (binaries) stays
zero-filled.  The heuristic is extension-based rather than a per-file
flag so packing stays a pure function of the staging directory.

The reader (--list/--extract) is an independent code path that walks the
directory and extents; it shares only the geometry constants.
"""

import datetime
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sparse import write_sparse                          # noqa: E402

# ---- fixed geometry (BIOS DPB contract) -------------------------------------

RECLEN = 128            # CP/M logical record
PSECLEN = 512           # physical sector
BLS = 4096              # allocation block size
BSH = 5                 # log2(BLS/RECLEN)
BLM = 31                # BLS/RECLEN - 1
EXM = 1                 # extent mask (BLS=4096, DSM>255)
DRM = 511               # highest directory entry number
DIRBLKS = (DRM + 1) * 32 // BLS     # = 4 directory blocks
ENTRY_DATA = 8 * BLS                # bytes mapped by one dir entry (32 KB)
LOGEXT = 16 * 1024                  # logical extent size
RECS_PER_LOGEXT = LOGEXT // RECLEN  # 128

# DSM (highest block number) is the ONE geometry field that differs between
# our drive letters -- A: is 10 MB (DSM 2559) and B: is 8 MB (DSM 2047), see
# src/bios/bios900.c -- and it is exactly the image size expressed in allocation
# blocks, so it is derived rather than declared.  set_dsm() is called from
# every entry point that knows an image size; the initial value is drive A:'s
# so that a caller that forgets still gets the historical behaviour.
DSM = 2559


def set_dsm(imgbytes):
    """Adopt the DPB DSM implied by an image of imgbytes bytes."""
    global DSM
    if imgbytes % BLS:
        sys.stderr.write("mkcpmfs: warning: %d bytes is not a whole number "
                         "of %d-byte allocation blocks\n" % (imgbytes, BLS))
    DSM = imgbytes // BLS - 1
    if DSM < DIRBLKS:
        die("image of %d bytes cannot even hold the directory" % imgbytes)
    return DSM

ENTSIZE = 32
NENT = DRM + 1                      # 512 directory entries

# directory entry type byte (entry[0])
T_FREE = 0xe5                       # free slot
T_XFCB = 0x10                       # 0x10..0x1f: XFCB (password) for a user
T_LABEL = 0x20                      # directory label, at most one per drive
T_SFCB = 0x21                       # stamps for the 3 preceding entries

# directory label mode bits (entry[12]); xfcb.lit dl$* / dirlbl.asm:47-53
DL_PASSWORD = 0x80
DL_ACCESS = 0x40                    # stamp field 1 holds the ACCESS time
DL_UPDATE = 0x20
DL_CREATE = 0x10                    # stamp field 1 holds the CREATE time
DL_EXISTS = 0x01

LABEL_MODES = {
    'create': DL_CREATE,
    'update': DL_UPDATE,
    'access': DL_ACCESS,
    'password': DL_PASSWORD,
    'none': 0,
}

# XFCB password mode bits (entry[12]); xfcb.lit:4
XP_READ = 0x80
XP_WRITE = 0x40
XP_DELETE = 0x20

XFCB_MODE = XP_READ | XP_WRITE | XP_DELETE
XFCB_PASSWORD = 'PASSWORD'

CPM_EPOCH = datetime.date(1977, 12, 31)     # day 1 = 1978-01-01

BADCHARS = set('<>.,;:=?*[] ' + '"')

# Extensions treated as CP/M text: final partial record padded with ^Z
# (0x1A), the CP/M text-EOF byte.  All other extensions are binary and
# stay zero-padded.
TEXT_EXTS = {'TXT', 'C', 'H', 'SUB', 'PD', '8KN', 'S', 'ASM', 'DOC', 'MAN'}


def die(msg):
    sys.stderr.write("mkcpmfs: %s\n" % msg)
    sys.exit(1)


# ---- 8.3 name handling -------------------------------------------------------

def encode_name(hostname):
    """Host filename -> 11-byte upper-case space-padded F1-8/T1-3, or die."""
    up = hostname.upper()
    if '.' in up:
        base, _, ext = up.partition('.')
    else:
        base, ext = up, ''
    if not base or len(base) > 8 or len(ext) > 3:
        die("filename %r does not fit 8.3" % hostname)
    for part in (base, ext):
        for c in part:
            if ord(c) < 0x21 or ord(c) > 0x7e or c in BADCHARS:
                die("filename %r has a character invalid in CP/M" % hostname)
    return (base.ljust(8) + ext.ljust(3)).encode('ascii')


def is_text(hostname):
    """CP/M text file (^Z-padded) vs binary (zero-padded), by extension."""
    _, _, ext = hostname.upper().rpartition('.')
    return ext in TEXT_EXTS


def decode_name(name11):
    """11 bytes (attribute bits stripped) -> host name BASE.EXT / BASE."""
    b = bytes(c & 0x7f for c in name11)
    base = b[:8].decode('ascii', 'replace').rstrip(' ')
    ext = b[8:].decode('ascii', 'replace').rstrip(' ')
    return base + '.' + ext if ext else base


# ---- CP/M 3 directory extensions --------------------------------------------

def is_file_entry(t):
    """Type byte t names a file FCB (user 0..15)?"""
    return t < 0x10


def is_free(t):
    """Type byte t names a free slot?  Only 0xE5 exactly."""
    return t == T_FREE


def is_extension(t):
    """Type byte t names a CP/M 3 extension entry that must be preserved?"""
    return not is_file_entry(t) and not is_free(t)


def sfcb_index(i):
    """Directory index of the SFCB covering entry i (the 4th of i's group)."""
    return i | 3


def sfcb_slot(i):
    """Sub-record 0..2 of entry i within its SFCB, or None if i IS the SFCB."""
    s = i & 3
    return None if s == 3 else s


def sfcb_off(i):
    """Byte offset of entry i's 10-byte sub-record inside its SFCB entry."""
    return 1 + 10 * sfcb_slot(i)


def has_sfcb(dirbuf, i):
    """Is a type-21h SFCB present for entry i?"""
    s = sfcb_slot(i)
    return s is not None and dirbuf[sfcb_index(i) * ENTSIZE] == T_SFCB


def get_sfcb(dirbuf, i):
    """Entry i's 10-byte SFCB sub-record, or None when there is no SFCB."""
    if not has_sfcb(dirbuf, i):
        return None
    base = sfcb_index(i) * ENTSIZE + sfcb_off(i)
    return bytes(dirbuf[base:base + 10])


def put_sfcb(dirbuf, i, sub):
    """Store a 10-byte SFCB sub-record for entry i.  Ignored with no SFCB."""
    if not has_sfcb(dirbuf, i):
        return False
    base = sfcb_index(i) * ENTSIZE + sfcb_off(i)
    dirbuf[base:base + 10] = sub
    return True


def clear_sfcb(dirbuf, i):
    """Zero entry i's SFCB sub-record (called when its FCB is freed)."""
    return put_sfcb(dirbuf, i, b'\0' * 10)


def encode_stamp(when):
    """datetime -> 4-byte stamp: LE day number since 1977-12-31, BCD hh, mm.

    None (or a date before the epoch) encodes as the all-zero "no stamp".
    """
    if when is None:
        return b'\0\0\0\0'
    days = (when.date() - CPM_EPOCH).days
    if days < 1 or days > 0xffff:
        die("stamp %s is outside the CP/M date range" % when)
    bcd = lambda v: ((v // 10) << 4) | (v % 10)
    return bytes([days & 0xff, (days >> 8) & 0xff,
                  bcd(when.hour), bcd(when.minute)])


def decode_stamp(s4):
    """4-byte stamp -> 'YYYY-MM-DD HH:MM', or None when unstamped."""
    days = s4[0] | (s4[1] << 8)
    if days == 0:
        return None
    unbcd = lambda v: (v >> 4) * 10 + (v & 0x0f)
    d = CPM_EPOCH + datetime.timedelta(days=days)
    return "%s %02d:%02d" % (d.isoformat(), unbcd(s4[2]), unbcd(s4[3]))


def show_stamps(sub):
    """SFCB sub-record -> '<stamp1> <stamp2>' display text, '' when blank."""
    if sub is None:
        return ''
    c, u = decode_stamp(sub[0:4]), decode_stamp(sub[4:8])
    if c is None and u is None:
        return ''
    return "%-16s  %-16s" % (c or '-', u or '-')


def parse_stamp_date(s):
    """'YYYY-MM-DD' or 'YYYY-MM-DDTHH:MM' -> datetime."""
    for fmt in ('%Y-%m-%dT%H:%M', '%Y-%m-%d %H:%M', '%Y-%m-%d'):
        try:
            return datetime.datetime.strptime(s, fmt)
        except ValueError:
            pass
    die("--stamp-date %r is not YYYY-MM-DD[THH:MM]" % s)


def parse_label_mode(s):
    """Comma list of create/update/access/password/none -> mode bits."""
    bits = DL_EXISTS
    for name in s.split(','):
        name = name.strip().lower()
        if name not in LABEL_MODES:
            die("unknown --label-mode %r (want %s)"
                % (name, '/'.join(sorted(LABEL_MODES))))
        bits |= LABEL_MODES[name]
    if (bits & DL_ACCESS) and (bits & DL_CREATE):
        die("--label-mode: access and create share one stamp field, "
            "pick one")
    return bits


def find_label(dirbuf):
    """Directory index of the type-20h label, or None."""
    for i in range(NENT):
        if dirbuf[i * ENTSIZE] == T_LABEL:
            return i
    return None


def free_slot(dirbuf, stamped):
    """Lowest slot usable for a new non-SFCB entry, or None when full."""
    for i in range(NENT):
        if stamped and sfcb_slot(i) is None:
            continue
        if is_free(dirbuf[i * ENTSIZE]):
            return i
    return None


def dir_is_stamped(dirbuf):
    """Does this directory carry SFCBs?  True when any 4th slot holds 21h."""
    for i in range(3, NENT, 4):
        if dirbuf[i * ENTSIZE] == T_SFCB:
            return True
    return False


def do_initdir(dirbuf):
    """Reserve every 4th entry as an SFCB, relocating whatever sits there.

    The INITDIR analog.  File FCBs (and any extension entry) found in a 4th
    slot are moved to a free non-4th slot: directory entries carry no
    positional meaning, so a move is transparent to every reader.  A moved
    entry brings no stamps with it (a 4th slot is the SFCB's own slot, so
    nothing there was ever stamped) and any stale stamps at the destination
    are cleared.  Existing SFCBs keep their contents, so the operation is
    idempotent.  Returns (created, moved).
    """
    created = moved = 0
    for i in range(3, NENT, 4):
        t = dirbuf[i * ENTSIZE]
        if t == T_SFCB:
            continue
        if not is_free(t):
            dst = free_slot(dirbuf, True)
            if dst is None:
                die("--initdir: no free slot to relocate directory entry %d "
                    "(directory too full to stamp)" % i)
            dirbuf[dst * ENTSIZE:(dst + 1) * ENTSIZE] = \
                dirbuf[i * ENTSIZE:(i + 1) * ENTSIZE]
            clear_sfcb(dirbuf, dst)
            moved += 1
        dirbuf[i * ENTSIZE:(i + 1) * ENTSIZE] = bytes([T_SFCB]) + b'\0' * 31
        created += 1
    return created, moved


def do_label(dirbuf, name, mode, when):
    """Create or update the type-20h directory label.  Returns its index.

    A new label gets both of its own stamps; an update refreshes only the
    update stamp, matching dirlbl.asm:120-137.  Password bytes (13, 16..23)
    and the password-enable bit of an existing label are left alone -- we
    never write passwords, and clearing them would lock the owner out of
    their own disk.
    """
    name11 = encode_name(name)
    i = find_label(dirbuf)
    stamp = encode_stamp(when)
    if i is None:
        i = free_slot(dirbuf, dir_is_stamped(dirbuf))
        if i is None:
            die("--label: directory is full, no slot for the label")
        e = bytearray(32)
        e[0] = T_LABEL
        e[1:12] = name11
        e[12] = mode
        e[24:28] = stamp
        e[28:32] = stamp
        dirbuf[i * ENTSIZE:(i + 1) * ENTSIZE] = e
    else:
        base = i * ENTSIZE
        dirbuf[base + 1:base + 12] = name11
        dirbuf[base + 12] = mode | (dirbuf[base + 12] & DL_PASSWORD)
        dirbuf[base + 28:base + 32] = stamp
    return i


def parse_xfcb(s):
    """'NAME[:MODE[:PASSWORD]]' -> (name, mode, password)."""
    parts = s.split(':')
    if len(parts) > 3:
        die("--xfcb wants NAME[:MODE[:PASSWORD]], got %r" % s)
    name = parts[0]
    mode = XFCB_MODE
    if len(parts) > 1 and parts[1] != '':
        try:
            mode = int(parts[1], 0) & 0xff
        except ValueError:
            die("--xfcb mode %r is not a number" % parts[1])
    pw = parts[2] if len(parts) > 2 else XFCB_PASSWORD
    if len(pw) > 8:
        die("--xfcb password %r is longer than 8 characters" % pw)
    return name, mode, "%-8s" % pw


def do_xfcb(dirbuf, name, mode, password):
    """Write a password XFCB for file `name`.  Returns its directory index.

    Layout is per c900oses/cpm8000/ref/cpm3/xfcb.lit:2-8 (INITDIR.PLI
    getpass ~838): byte 13 is the XOR key, the sum of the eight password
    characters, and bytes 16..23 hold the password REVERSED, each byte XORed
    with that key.  INITDIR also copies the mode byte into the protected
    file's SFCB sub-record (+8), which is where the BDOS's function 102 reads
    it from, so that is done here too.
    """
    name11 = encode_name(name)
    for i in range(NENT):
        if dirbuf[i * ENTSIZE] == T_XFCB and \
           bytes(c & 0x7f for c in dirbuf[i * ENTSIZE + 1:i * ENTSIZE + 12]) \
           == name11:
            die("--xfcb: %s already has an XFCB at entry %d" % (name, i))
    i = free_slot(dirbuf, dir_is_stamped(dirbuf))
    if i is None:
        die("--xfcb: directory is full, no slot for an XFCB")
    key = sum(ord(c) for c in password) & 0xff
    e = bytearray(32)
    e[0] = T_XFCB                       # user 0
    e[1:12] = name11
    e[12] = mode
    e[13] = key
    e[16:24] = bytes((ord(password[7 - j]) ^ key) for j in range(8))
    dirbuf[i * ENTSIZE:(i + 1) * ENTSIZE] = e

    for j in range(NENT):               # the file's own SFCB records the mode
        b = dirbuf[j * ENTSIZE]
        if is_file_entry(b) and not is_free(b) \
           and bytes(c & 0x7f for c in dirbuf[j * ENTSIZE + 1:j * ENTSIZE + 12]) \
               == name11 \
           and (((dirbuf[j * ENTSIZE + 14] & 0x3f) << 5)
                | (dirbuf[j * ENTSIZE + 12] & 0x1f)) == 0:
            sub = get_sfcb(dirbuf, j)
            if sub is not None:
                put_sfcb(dirbuf, j, sub[0:8] + bytes([mode]) + sub[9:10])
    return i


# ---- packer ------------------------------------------------------------------

def build_entries(name11, size, first_block):
    """Yield (entrybytes, nblocks) 32-byte directory entries for one file.

    Blocks are assumed allocated contiguously starting at first_block.
    """
    entries = []
    nchunks = max(1, (size + ENTRY_DATA - 1) // ENTRY_DATA)  # >=1 even if empty
    blk = first_block
    for k in range(nchunks):
        chunk = min(size - k * ENTRY_DATA, ENTRY_DATA)
        nblk = (chunk + BLS - 1) // BLS
        recs = (chunk + RECLEN - 1) // RECLEN
        # logical extents used by this entry: 1 or 2 (0-byte file: 1, RC=0)
        if recs > RECS_PER_LOGEXT:
            ext_total = 2 * k + 1
            rc = recs - RECS_PER_LOGEXT
        else:
            ext_total = 2 * k
            rc = recs
        e = bytearray(32)
        e[0] = 0                       # user 0
        e[1:12] = name11               # attribute bits clear
        e[12] = ext_total & 0x1f       # EX
        e[13] = 0                      # S1
        e[14] = (ext_total >> 5) & 0x3f  # S2 (module; write flag bit7 clear)
        e[15] = rc                     # RC (0x80 = full logical extent)
        for j in range(nblk):
            b = blk + j
            e[16 + 2 * j] = b & 0xff       # AL: 16-bit LITTLE-endian
            e[17 + 2 * j] = (b >> 8) & 0xff
        entries.append((bytes(e), nblk))
        blk += nblk
    return entries


def pack(imgpath, blocks, srcdir, opts):
    imgsize = blocks * PSECLEN
    set_dsm(imgsize)

    names = []
    for fn in sorted(os.listdir(srcdir)):
        path = os.path.join(srcdir, fn)
        if not os.path.isfile(path):
            die("%s is not a regular file" % path)
        names.append((encode_name(fn), path))
    names.sort(key=lambda t: t[0])
    seen = set()
    for n11, path in names:
        if n11 in seen:
            die("duplicate CP/M name %s (from %s)" % (decode_name(n11), path))
        seen.add(n11)

    img = bytearray(imgsize)
    # free directory entries are 0xE5 fill
    img[0:DIRBLKS * BLS] = b'\xe5' * (DIRBLKS * BLS)

    direntries = []
    nextblk = DIRBLKS                  # first data block after the directory
    for n11, path in names:
        with open(path, 'rb') as f:
            data = f.read()
        entries = build_entries(n11, len(data), nextblk)
        for e, nblk in entries:
            direntries.append(e)
        # lay the data down; ^Z-pad the final partial record of text files
        off = nextblk * BLS
        img[off:off + len(data)] = data
        pad = -len(data) % RECLEN
        if pad and is_text(os.path.basename(path)):
            img[off + len(data):off + len(data) + pad] = b'\x1a' * pad
        nextblk += sum(nblk for _, nblk in entries)
        if nextblk > DSM + 1:
            die("out of space: %s overflows the data area" % path)

    # Lay out the directory: SFCB slots first (so they are never claimed by a
    # file), then the file entries in the remaining slots, then the label.
    dirbuf = bytearray(img[0:DIRBLKS * BLS])
    if opts.initdir:
        do_initdir(dirbuf)
    for e in direntries:
        i = free_slot(dirbuf, opts.initdir)
        if i is None:
            die("too many directory entries: %d files need more than the "
                "%d usable slots" % (len(names), usable_slots(opts.initdir)))
        dirbuf[i * ENTSIZE:(i + 1) * ENTSIZE] = e
    if opts.label is not None:
        do_label(dirbuf, opts.label, opts.label_mode, opts.stamp_date)
    for name, mode, pw in opts.xfcbs:
        do_xfcb(dirbuf, name, mode, pw)
    img[0:DIRBLKS * BLS] = dirbuf

    write_sparse(imgpath, img)
    print("packed %d files, %d dir entries, %d/%d data blocks used%s"
          % (len(names), len(direntries), nextblk - DIRBLKS, DSM + 1 - DIRBLKS,
             ", stamped" if opts.initdir else ""))


def usable_slots(stamped):
    """Directory slots available to files/labels (SFCBs take every 4th)."""
    return NENT - NENT // 4 if stamped else NENT


def edit_image(imgpath, opts):
    """Apply --initdir/--label/--xfcb to an existing image, in place."""
    with open(imgpath, 'r+b') as f:
        dirbuf = bytearray(f.read(DIRBLKS * BLS))
        if len(dirbuf) < DIRBLKS * BLS:
            die("%s is too small to hold a directory" % imgpath)
        if opts.initdir:
            created, moved = do_initdir(dirbuf)
            print("initdir: %d SFCB entries, %d entries relocated, "
                  "%d file slots usable" % (created, moved, usable_slots(True)))
        if opts.label is not None:
            i = do_label(dirbuf, opts.label, opts.label_mode, opts.stamp_date)
            print("label: %s in entry %d, mode 0x%02x"
                  % (opts.label.upper(), i, dirbuf[i * ENTSIZE + 12]))
        for name, mode, pw in opts.xfcbs:
            i = do_xfcb(dirbuf, name, mode, pw)
            print("xfcb: %s in entry %d, mode 0x%02x"
                  % (name.upper(), i, mode))
        f.seek(0)
        f.write(dirbuf)


# ---- reader (independent path) ----------------------------------------------

def read_dir(imgpath):
    """Parse the directory.

    Returns {(user, name11): {entry_k: (recs, blocklist)}}, where blocklist is
    a list of (slot, block) pairs -- slot 0..7 within the entry's allocation
    list -- carrying only the allocated slots.
    """
    with open(imgpath, 'rb') as f:
        img = f.read()
    if len(img) < DIRBLKS * BLS:
        die("image too small to hold a directory")
    set_dsm(len(img))
    files = {}
    for i in range(DRM + 1):
        e = img[i * 32:(i + 1) * 32]
        user = e[0]
        if not is_file_entry(user) or is_free(user):
            continue                    # free, or a CP/M 3 extension entry
        name11 = bytes(c & 0x7f for c in e[1:12])
        ex, s2, rc = e[12] & 0x1f, e[14] & 0x3f, e[15]
        ext_total = (s2 << 5) | ex
        entry_k = ext_total >> 1        # EXM=1: 2 logical extents per entry
        recs = (ext_total & 1) * RECS_PER_LOGEXT + rc
        # A zero AL slot is a HOLE, not an absence: the file has no block
        # there (a sparse file, which random-record writes produce), and the
        # blocks AFTER it still belong at the offsets their own slots name.
        # So each block is carried with its slot number j; dropping the zeros
        # and renumbering would slide every later block toward the start.
        blocklist = []
        for j in range(8):
            b = e[16 + 2 * j] | (e[17 + 2 * j] << 8)   # little-endian
            if b:
                blocklist.append((j, b))
        files.setdefault((user, name11), {})[entry_k] = (recs, blocklist)
    return img, files


def read_extensions(dirbuf):
    """Index the CP/M 3 extension entries: (label_index, xfcbs, nsfcb, other).

    xfcbs is a list of (index, user, name11, passmode); other is a list of
    (index, typebyte) for entries this tool does not understand but must
    still leave alone.
    """
    label = None
    xfcbs = []
    nsfcb = 0
    other = []
    for i in range(NENT):
        t = dirbuf[i * ENTSIZE]
        if not is_extension(t):
            continue
        e = dirbuf[i * ENTSIZE:(i + 1) * ENTSIZE]
        if t == T_LABEL:
            label = i
        elif t == T_SFCB:
            nsfcb += 1
        elif T_XFCB <= t <= T_XFCB + 0x0f:
            xfcbs.append((i, t - T_XFCB, bytes(c & 0x7f for c in e[1:12]),
                          e[12]))
        else:
            other.append((i, t))
    return label, xfcbs, nsfcb, other


def stamp_of(dirbuf, files, key):
    """The SFCB sub-record stamping a file's extent-0 entry, or None.

    Only the first directory FCB of a file is stamped (bdos30.asm qdirfcb1),
    so the stamp is looked up on that entry.  Which entry that is must be
    decided on the DIRECTORY entry number, not on the raw extent: with EXM=1
    one entry maps two logical extents, and RC/EX record the LAST logical
    extent the entry holds, so the first entry of a file over 16 KiB carries
    raw extent 1.  Shifting by EXM is what read_dir does for the same reason.
    """
    user, name11 = key
    for i in range(NENT):
        e = dirbuf[i * ENTSIZE:(i + 1) * ENTSIZE]
        if e[0] != user or is_free(e[0]):
            continue
        if bytes(c & 0x7f for c in e[1:12]) != name11:
            continue
        ext_total = ((e[14] & 0x3f) << 5) | (e[12] & 0x1f)
        if ext_total >> 1 == 0:         # EXM=1: entries 0 and 1 are one FCB
            return get_sfcb(dirbuf, i)
    return None


def file_size(extmap):
    """Byte size implied by a file's entries (max record seen)."""
    size = 0
    for k, (recs, _) in extmap.items():
        size = max(size, k * ENTRY_DATA + recs * RECLEN)
    return size


def cmd_list(imgpath):
    img, files = read_dir(imgpath)
    dirbuf = img[0:DIRBLKS * BLS]
    label, xfcbs, nsfcb, other = read_extensions(dirbuf)

    if label is not None:
        e = dirbuf[label * ENTSIZE:(label + 1) * ENTSIZE]
        print("label     %-12s mode 0x%02x [%s]  created %s  updated %s"
              % (decode_name(bytes(c & 0x7f for c in e[1:12])), e[12],
                 label_modes(e[12]),
                 decode_stamp(e[24:28]) or '-', decode_stamp(e[28:32]) or '-'))
    for i, user, name11, pmode in xfcbs:
        print("password  %2d %-12s mode 0x%02x (preserved, not interpreted)"
              % (user, decode_name(name11), pmode))
    for i, t in other:
        print("unknown   entry %d type 0x%02x (preserved verbatim)" % (i, t))
    if nsfcb:
        print("stamps    %d SFCB entries, %d file slots usable"
              % (nsfcb, usable_slots(True)))

    total = 0
    for key in sorted(files):
        user, name11 = key
        extmap = files[key]
        sz = file_size(extmap)
        nblk = sum(len(bl) for _, bl in extmap.values())
        total += sz
        print("%2d %-12s %8d bytes  %3d blocks  %d entr%-3s %s"
              % (user, decode_name(name11), sz, nblk, len(extmap),
                 'y' if len(extmap) == 1 else 'ies',
                 show_stamps(stamp_of(dirbuf, files, key))))
    print("%d files, %d bytes" % (len(files), total))


def cmd_entries(imgpath):
    """Every non-free directory entry, decoded field by field.

    This is the byte-level view: it never reconstructs a file, so what it
    prints is what is on the disk.  Everything multi-byte is decoded in the
    8080 order the format uses -- the AL block numbers and the stamp date
    words are LITTLE-endian -- which makes this the check that a stamp or
    an extent was written in the right order rather than merely read back
    consistently by the writer.
    """
    with open(imgpath, 'rb') as f:
        dirbuf = f.read(DIRBLKS * BLS)
    if len(dirbuf) < DIRBLKS * BLS:
        die("image too small to hold a directory")
    nfree = 0
    for i in range(NENT):
        e = dirbuf[i * ENTSIZE:(i + 1) * ENTSIZE]
        t = e[0]
        if is_free(t):
            nfree += 1
            continue
        name = "%-8s.%-3s" % (bytes(c & 0x7f for c in e[1:9]).decode('latin-1'),
                              bytes(c & 0x7f for c in e[9:12]).decode('latin-1'))
        if is_file_entry(t):
            al = [e[16 + 2 * j] | (e[17 + 2 * j] << 8) for j in range(8)]
            nz = [b for b in al if b]
            print("entry %3d type 0x%02x file  %s user %2d "
                  "ex 0x%02x s1 0x%02x s2 0x%02x rc 0x%02x nblk %d al %s"
                  % (i, t, name, t, e[12], e[13], e[14], e[15], len(nz),
                     ','.join(str(b) for b in nz) or '-'))
        elif t == T_LABEL:
            print("entry %3d type 0x%02x label %s mode 0x%02x [%s] "
                  "created %s updated %s"
                  % (i, t, name, e[12], label_modes(e[12]),
                     decode_stamp(e[24:28]) or '-',
                     decode_stamp(e[28:32]) or '-'))
        elif T_XFCB <= t <= T_XFCB + 0x0f:
            print("entry %3d type 0x%02x xfcb  %s user %2d mode 0x%02x "
                  "key 0x%02x pw %s"
                  % (i, t, name, t - T_XFCB, e[12], e[13],
                     ''.join("%02x" % b for b in e[16:24])))
        elif t == T_SFCB:
            for s in range(3):
                sub = e[1 + 10 * s:11 + 10 * s]
                if not any(sub):        # an unstamped slot says nothing
                    continue
                print("entry %3d type 0x%02x sfcb  sub %d for entry %3d "
                      "create %-16s update %-16s pwmode 0x%02x"
                      % (i, t, s, (i & ~3) + s,
                         decode_stamp(sub[0:4]) or '-',
                         decode_stamp(sub[4:8]) or '-', sub[8]))
        else:
            print("entry %3d type 0x%02x unknown (preserved verbatim)" % (i, t))
    print("%d entries in use, %d free" % (NENT - nfree, nfree))


def label_modes(mode):
    """Label mode byte -> readable flag list."""
    names = []
    for bit, name in ((DL_PASSWORD, 'password'), (DL_ACCESS, 'access'),
                      (DL_UPDATE, 'update'), (DL_CREATE, 'create'),
                      (DL_EXISTS, 'exists')):
        if mode & bit:
            names.append(name)
    return ','.join(names) if names else 'none'


def extract_path(destdir, user, name11):
    """Host path for one extracted file, or die().

    THE ELEVEN NAME BYTES ARE UNTRUSTED INPUT.  They are whatever is on the
    medium -- a directory entry can say `../OUT.TXT' or `/OUT.TXT' as easily
    as `PIP.COM' -- and nothing in CP/M stops it, so the decoded name may not
    be handed to os.path.join as a path.  Two separate checks, because either
    alone can be fooled: the decoded leaf must BE a leaf, and the path it
    resolves to must lie under the destination (which also catches a symlink
    planted in the destination directory).
    """
    leaf = decode_name(name11)
    if user != 0:
        leaf += ".u%d" % user
    bad = None
    if leaf in ('', '.', '..'):
        bad = "is not a filename"
    elif '/' in leaf or '\\' in leaf or os.sep in leaf or (
            os.altsep and os.altsep in leaf):
        bad = "contains a path separator"
    elif os.path.isabs(leaf) or os.path.splitdrive(leaf)[0]:
        bad = "is an absolute path"
    elif any(c < ' ' or c == '\x7f' for c in leaf):
        bad = "contains a control character"
    if bad is None:
        dest = os.path.realpath(destdir)
        out = os.path.realpath(os.path.join(dest, leaf))
        if out != os.path.join(dest, leaf):
            bad = "resolves outside %s" % dest
    if bad is not None:
        die("refusing to extract user %d entry %r: the decoded name %r %s"
            % (user, bytes(name11), leaf, bad))
    return os.path.join(destdir, leaf)


def cmd_extract(imgpath, destdir):
    img, files = read_dir(imgpath)
    os.makedirs(destdir, exist_ok=True)
    # Every name is validated BEFORE the first byte is written, so a directory
    # holding one hostile entry does not get half-extracted first.
    outpath = dict((key, extract_path(destdir, key[0], key[1]))
                   for key in sorted(files))
    for (user, name11) in sorted(files):
        extmap = files[(user, name11)]
        sz = file_size(extmap)
        buf = bytearray(sz)             # unallocated (sparse) ranges read as 0
        for k, (recs, blocklist) in extmap.items():
            for j, b in blocklist:
                if b > DSM:
                    die("%s: block %d out of range" % (decode_name(name11), b))
                src = img[b * BLS:(b + 1) * BLS]
                dst = k * ENTRY_DATA + j * BLS
                n = min(BLS, sz - dst)
                if n > 0:
                    buf[dst:dst + n] = src[:n]
        with open(outpath[(user, name11)], 'wb') as f:
            f.write(buf)
    print("extracted %d files to %s" % (len(files), destdir))


# ---- main --------------------------------------------------------------------

class Options(object):
    """Directory-format options shared by pack and in-place edit."""

    def __init__(self):
        self.initdir = False
        self.label = None
        self.label_mode = DL_EXISTS | DL_CREATE | DL_UPDATE
        self.stamp_date = None          # None = zero stamps (deterministic)
        self.xfcbs = []                 # (name, mode, password) to place


USAGE = ("usage: mkcpmfs.py [--initdir] [--label NAME] [--label-mode M,...] "
         "[--stamp-date S] [--xfcb N[:MODE[:PW]]] <img> [<blocks> <srcdir>]\n"
         "       mkcpmfs.py --list <img> | --entries <img> "
         "| --extract <img> <destdir>")


def main(argv):
    if len(argv) >= 2 and argv[1] == '--list':
        if len(argv) != 3:
            die("usage: mkcpmfs.py --list <img>")
        cmd_list(argv[2])
        return
    if len(argv) >= 2 and argv[1] == '--entries':
        if len(argv) != 3:
            die("usage: mkcpmfs.py --entries <img>")
        cmd_entries(argv[2])
        return
    if len(argv) >= 2 and argv[1] == '--extract':
        if len(argv) != 4:
            die("usage: mkcpmfs.py --extract <img> <destdir>")
        cmd_extract(argv[2], argv[3])
        return

    opts = Options()
    args = []
    i = 1
    while i < len(argv):
        a = argv[i]
        if a == '--initdir':
            opts.initdir = True
        elif a in ('--label', '--label-mode', '--stamp-date', '--xfcb'):
            if i + 1 >= len(argv):
                die("%s needs an argument\n%s" % (a, USAGE))
            i += 1
            if a == '--label':
                opts.label = argv[i]
            elif a == '--label-mode':
                opts.label_mode = parse_label_mode(argv[i])
            elif a == '--xfcb':
                opts.xfcbs.append(parse_xfcb(argv[i]))
            else:
                opts.stamp_date = parse_stamp_date(argv[i])
        elif a.startswith('--'):
            die("unknown option %s\n%s" % (a, USAGE))
        else:
            args.append(a)
        i += 1

    if len(args) == 3:
        try:
            blocks = int(args[1])
        except ValueError:
            die("blocks must be an integer")
        pack(args[0], blocks, args[2], opts)
    elif len(args) == 1:
        if not opts.initdir and opts.label is None and not opts.xfcbs:
            die("nothing to do: give --initdir, --label and/or --xfcb\n"
                + USAGE)
        edit_image(args[0], opts)
    else:
        die(USAGE)


if __name__ == '__main__':
    main(sys.argv)
