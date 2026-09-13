#!/usr/bin/env python3
"""mkcpmdisk.py - build a CP/M-ONLY bootable hard-disk image for the C900.

Usage: mkcpmdisk.py [--kboot=FILE] [--mutate=KIND] [--cpmb-base=BLK]

The medium carries only what CP/M-8000 needs:

    block        blocks  what
    0            136     boot     Coherent FS: kboot (as `coherent') + kboot.cfg
    38144        20480   cpma     drive A:, raw
    58624        512     cpmboot  Coherent FS: cpm.sys
    59136        16384   cpmb     drive B:, raw
    (total 83776 = 704 cyl x 7 heads x 17 spt, the ROM's drive type 3)

No Coherent root, /usr, /usr/man, /tmp or swap: nothing in CP/M reads them.
Blocks 136..38143 and 75520..83775 are unallocated, and the image is written
sparse, so the file costs about what its partitions hold.

WHY THE OFFSETS ARE THE DUAL-BOOT ONES.  They no longer have to be: the CP/M
BIOS reads its drive table out of kboot's handoff block, so the cfg below is
what says where A: and B: are, and --cpmb-base= moves B: to prove it (that is
verify-bipart).  They are kept anyway, as a default, because the same drive A:
and B: bytes then sit at the same LBAs as on the dual-boot hd42-cpm.media
medium and one BIOS boots off either.  What changed is that this is now a
choice the medium records rather than a constant in C source.

kboot is an INPUT, not something this builder owns: it is its own repository
(commodore-900-kboot), because it belongs to no one operating system -- it
boots COHERENT and CP/M-8000 from the same menu.  We consume the built binary
and write our own kboot.cfg, which is a description of THIS medium: one `os'
entry, CP/M, asking for a partition table, and the `part' lines that fill it.

THE SLOT CONVENTION, which is a fixed interface and not this file's choice:
slots 8..14 are CP/M drives A: through G: in letter order, an absent letter is
a slot with 0 blocks, and slot 15 is the whole disk as it is on every C900
medium.  Slots 0..7 are COHERENT's wd(4) pseudo-drives and stay empty here.

--no-bootinfo writes the kboot.cfg this builder wrote BEFORE the drive table
came out of the medium: no `part' lines and no `part' keyword, so nothing is
handed over and cpm.sys falls back to its compiled A: and B:.  It is not a
mutation -- it is what a medium built for an older kboot, or for a kboot that
does not fill bi_part[], actually looks like, and verify-bifallback boots one
to show that such a medium still comes up exactly as it did.

--mutate=KIND breaks the boot chain on purpose, for verify-bootgate.  It exists
so the gate that says "the loader broke the OS" can be shown to fail:
    cfg     kboot.cfg replaced with garbage        (loader finds no config)
    kernel  cpm.sys stored under another name      (loader cannot find it)
    base    the os line names the wrong base block (loader reads no filesystem)
"""

import os
import sys

# The boot and cpmboot partitions are COHERENT filesystems because that is the
# format the ROM and kboot read -- an on-disk interface, not a dependency on
# COHERENT the operating system.  tools/cohfs.py writes it, in this repository,
# so that building a medium to test CP/M on needs no other source tree.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cohfs                                            # noqa: E402
from sparse import write_sparse                         # noqa: E402

BS = cohfs.BS

# Geometry: the ROM's dparam[3] (drive type 3), the same one hd42-cpm.media
# declares.  kboot programs it into the WD2010 from the `geom' line below
# before it loads anything.
GEOM = (704, 7, 17, 352)                    # cyl, heads, spt, precomp bytes
TOTAL = GEOM[0] * GEOM[1] * GEOM[2]         # 83776 blocks

BOOT_BASE, BOOT_BLKS, BOOT_ISIZE = 0, 136, 4
CPMA_BASE, CPMA_BLKS = 38144, 20480
CPMBOOT_BASE, CPMBOOT_BLKS, CPMBOOT_ISIZE = 58624, 512, 4
CPMB_BASE, CPMB_BLKS = 59136, 16384

KERNEL = 'cpm.sys'          # the file kboot loads out of cpmboot
LOADER = 'coherent'         # the file the ROM loads out of the boot partition
LABEL = 'CPM-8000'          # the kboot menu label

MUTATIONS = ('cfg', 'kernel', 'base')


# The whole disk in memory, written out sparse at the end.  cohfs.build_fs()
# writes its filesystems through this same block interface.
Img = cohfs.Img


    """kboot.cfg for a CP/M-only medium.

    Two of the things a dual-boot kboot.cfg carries do not apply here: there
    are no Coherent partitions to hand a kernel a wd(4) table for, and no
    second OS to choose between.  What is left is the geometry -- a fact about
    the drive, which kboot programs for every OS -- the single `os' line, and
    the CP/M drive slots.

    The trailing `part' on the `os' line is what ASKS for the handoff, and it
    is load-bearing in both directions: without it kboot starts the kernel in
    silence and cpm.sys falls back to its compiled A:/B:, and with it kboot
    REFUSES the entry unless there are `part' lines to fill the table from.
    So the keyword and the slots below are one thing, and neither is optional
    once the other is there.

    Slot 15 is the whole disk, as it is on every C900 medium: not a CP/M drive
    letter, and named here because a medium that describes its drives should
    describe the device they are on.  Nothing reads it yet.

    With one entry kboot prints no menu and boots it directly, so a scripted
    session for this medium sends no menu-selection character (Makefile
    OSSEL).
    """
    if not bootinfo:
        # The pre-K1 cfg, kept exactly: no table offered, so kboot starts the
        # kernel in silence and the kernel uses what it was built with.
        return ("\n".join([
            "# kboot.cfg -- generated by tools/mkcpmdisk.py; do not edit by hand.",
            "geom %d %d %d %d" % GEOM,
            "# No `part': this entry asks for no handoff, so it is loaded and",
            "# started with nothing written into it and nothing said about it.",
            "os %s %d %s" % (LABEL, base, KERNEL),
    return ("\n".join([
        "# kboot.cfg -- generated by tools/mkcpmdisk.py; do not edit by hand.",
        "# geom <cyl> <heads> <spt> <precomp>: WD2010 set-drive-parameters",
        "geom %d %d %d %d" % GEOM,
        "# part <slot> <start> <blocks>.  Slots 8..14 are CP/M drives A:..G:",
        "# in letter order; a letter that is not there is a slot that is not",
        "# named.  Slot 15 is the whole device.",
        "part 8 %d %d" % (CPMA_BASE, CPMA_BLKS),
        "part 9 %d %d" % (cpmb_base, CPMB_BLKS),
        "part 15 0 %d" % TOTAL,
        "# os <label> <base-block> <kernel-file> [part] -- `part' asks for the",
        "# table above; kboot refuses an entry that asks and cannot be given.",
        "os %s %d %s part" % (LABEL, base, KERNEL),


def blit(img, base, blks, path, what):
    data = open(path, 'rb').read()
    if len(data) > blks * BS:
        sys.exit("mkcpmdisk: %s image is %d bytes > %d blocks"
                 % (what, len(data), blks))
    img.d[base*BS : base*BS + len(data)] = data
    return len(data)


def main():
    kboot = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                         '..', '..', 'commodore-900-kboot', 'build', 'kboot')
    mutate = None
    bootinfo = True
    cpmb_base = CPMB_BASE
    args = []
    for a in sys.argv[1:]:
        if a.startswith('--kboot='):
            kboot = a.split('=', 1)[1]
        elif a == '--no-bootinfo':
            bootinfo = False
        elif a.startswith('--cpmb-base='):
            # Where drive B: is.  It goes into the cfg AND into where the
            # bytes are blitted, because those are the same fact: the medium
            # says where its drives are and the BIOS believes it.
            cpmb_base = int(a.split('=', 1)[1])
        elif a.startswith('--mutate='):
            mutate = a.split('=', 1)[1]
            if mutate not in MUTATIONS:
                sys.exit("mkcpmdisk: --mutate must be one of %s"
                         % ", ".join(MUTATIONS))
        elif a.startswith('-'):
            sys.exit(__doc__)
        else:
            args.append(a)
    if len(args) not in (3, 4):
        sys.exit(__doc__)
    out, cpmsys, cpmaimg = args[:3]
    cpmbimg = args[3] if len(args) == 4 else None

    if not os.path.exists(kboot):
        sys.exit("mkcpmdisk: no loader at %s\n"
                 "  kboot is an input to this build, not part of it.  Build it:\n"
                 "      git clone <...>/commodore-900-kboot\n"
                 "      make -C commodore-900-kboot\n"
                 "  or point --kboot= (make KBOOT=) at an already-built copy."
                 % kboot)

    # A moved drive B: is still a partition of THIS device: overlapping the
    # boot partition, drive A: or cpmboot would produce a medium whose two
    # halves quietly overwrite each other, and the CP/M side cannot detect it
    # -- the BIOS believes what the cfg says.  So it is checked here, where
    # the layout is known.
    for lo, blks, what in ((BOOT_BASE, BOOT_BLKS, 'boot'),
                           (CPMA_BASE, CPMA_BLKS, 'cpma'),
                           (CPMBOOT_BASE, CPMBOOT_BLKS, 'cpmboot')):
        if cpmb_base < lo + blks and lo < cpmb_base + CPMB_BLKS:
            sys.exit("mkcpmdisk: drive B: at %d..%d overlaps %s (%d..%d)"
                     % (cpmb_base, cpmb_base + CPMB_BLKS - 1, what,
                        lo, lo + blks - 1))
    if cpmb_base < 0 or cpmb_base + CPMB_BLKS > TOTAL:
        sys.exit("mkcpmdisk: drive B: at %d..%d is off the %d-block device"
                 % (cpmb_base, cpmb_base + CPMB_BLKS - 1, TOTAL))

    img = Img(TOTAL)

    # boot: the loader the ROM runs, plus this medium's description of itself.
    base = CPMBOOT_BASE
    if mutate == 'base':
        base = cpmb_base                # drive B:, not the cpmboot filesystem
    if mutate == 'cfg':
        # Not an empty file: an unparseable one, which is the realistic
        # failure (a truncated write, a hand edit).  kboot finds no `os' line
        # and falls back to its compiled Coherent default -- which on a
        # CP/M-only medium points at nothing.
        cfg = b'\x00\xffgeom garbage not a config\n' * 4
    if len(cfg) > cohfs.CFGBLK * BS:
        sys.exit("mkcpmdisk: kboot.cfg is %d bytes > %d"
                 % (len(cfg), cohfs.CFGBLK * BS))
    used = cohfs.build_fs(img, BOOT_BASE, BOOT_BLKS, BOOT_ISIZE,
                          {LOADER: open(kboot, 'rb').read(), 'kboot.cfg': cfg})
    # The ROM reads the boot partition under whichever dparam[] geometry its
    # drive type selects, so only the blocks whose mapping is the same under
    # all of them are reachable: LBA 0..(ROM_MIN_HEADS*17 - 1).  cohfs.py has
    # the derivation and the ROM evidence for the head count.
    safe = cohfs.ROM_MIN_HEADS * GEOM[2]
    if used > safe:
        sys.exit("mkcpmdisk: boot partition uses %d blocks > ROM-safe span (%d),\n"
                 "  with %s at %d bytes.  Past the span the ROM cannot reliably\n"
                 "  read the loader -- nor the root directory that NAMES it, which\n"
                 "  it reads first -- so the machine does not boot.  Shrink the\n"
                 "  loader (kboot's own `make' reports the same budget)."
                 % (used, safe, os.path.basename(kboot), os.path.getsize(kboot)))

    # cpmboot: cpm.sys, the file the `os' line names.
    kname = KERNEL if mutate != 'kernel' else 'cpm.sy_'
    cohfs.build_fs(img, CPMBOOT_BASE, CPMBOOT_BLKS, CPMBOOT_ISIZE,
                   {kname: open(cpmsys, 'rb').read()})

    na = blit(img, CPMA_BASE, CPMA_BLKS, cpmaimg, 'cpma')
    nb = 0
    if cpmbimg is not None:
        nb = blit(img, cpmb_base, CPMB_BLKS, cpmbimg, 'cpmb')

    write_sparse(out, img.d)
    print("mkcpmdisk: %s (%d blocks, %d cyl x %d heads x %d spt)"
          % (out, TOTAL, GEOM[0], GEOM[1], GEOM[2]))
    # The span figure is printed on every build, not only when it is breached:
    # a medium landing at 33 of 34 blocks looked exactly like one landing at
    # 20, so the commit that consumed the last block could not know it had.
    print("  boot@%-6d %s (%d B) + kboot.cfg (%d B), %d of %d ROM-safe blocks"
          " used (%d spare)"
          % (BOOT_BASE, os.path.basename(kboot), os.path.getsize(kboot),
             len(cfg), used, safe, safe - used))
    print("  cpma@%-6d %d B   cpmboot@%-6d %s (%d B)   cpmb@%-6d %d B"
          % (CPMA_BASE, na, CPMBOOT_BASE, kname, os.path.getsize(cpmsys),
             cpmb_base, nb))
    if bootinfo:
        print("  os %s %d %s part" % (LABEL, base, KERNEL))
        print("  part 8 %d %d (A:)   part 9 %d %d (B:)   part 15 0 %d"
              % (CPMA_BASE, CPMA_BLKS, cpmb_base, CPMB_BLKS, TOTAL))
    else:
        print("  os %s %d %s   (--no-bootinfo: no table handed over, so the"
              " kernel uses its own)" % (LABEL, base, KERNEL))
    if mutate:
        print("  *** MUTATED (--mutate=%s): this medium is BROKEN on purpose"
              % mutate)


if __name__ == '__main__':
    main()
