/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/* Shared C/assembly memory layout (crt.s is preprocessed).
 * Transients link at TPABASE without relocation records, so TPASEG must
 * remain fixed. pgalloc.c swaps the physical backing page instead.
 * Changing TPASEG requires updating the build's UBASE and rebuilding all
 * programs. CPM.SYS text must fit one 64 KB segment to leave 0x32 free. */
#define	TPASEG		0x32
#define	TPABASE		0x32000000L	/* (long)TPASEG<<24 -- keep in sync */
#define	TPAPHYSPAGE	0x0a		/* phys 0x0A0000, high descriptor byte */

/*
 * Split-I/D loader shim (Option 6, 0xEE0B programs).  Code runs natively
 * in the TPA (code bank), data lives in a second 64K bank, and the
 * load-time scanner's side table in a third.  Both are mapped at load
 * time (splitld.c spload), SYS-only: the program itself never addresses
 * them -- every access goes through the SC #255 trap handler (splitsc.c)
 * or the BDOS gate's split-aware pointer mapping (bdosglue.s).  Neither
 * segment is in the MRT, so the BDOS never offers them as TPA.
 */
#define	SPLITDSEG	0x35		/* data bank */
#define	SPLITDBASE	0x35000000L
#define	SPLITDPAGE	0x0b00		/* phys 0x0B0000 (mapseg base page) */
#define	SPLITTSEG	0x36		/* side table + scanner scratch */
#define	SPLITTBASE	0x36000000L
#define	SPLITTPAGE	0x0e00		/* phys 0x0E0000, above the C stack */
/* Split-I/D slow-path module: cmain.c copies its linked image from resident
 * data to SPLITMSEG at cold boot. The descriptor limit excludes ROM state
 * at physical 0x0FE400 on a 512 KB machine.
 * The fixed interface offsets are defined by splitent.s and checked by
 * mkblob.py; implementation entry addresses remain internal to the module. */
#define	SPLITMSEG	0x37		/* relocated shim slow path */
#define	SPLITMBASE	0x37000000L
#define	SPLITMPAGE	0x0f00		/* phys 0x0F0000 (mapseg base page) */
#define	SPLITMPHYSPAGE	0x0f		/* the same, high descriptor byte */
#define	SPLITMLIM	0xE3		/* last valid page: 0x0FE3FF, one
					 * short of the ROM's seg-1 cells */
#define	SPM_MAGIC	0x5350		/* 'SP' at module offset 0 */
#define	SPM_VERS	1		/* interface version, offset 2 */
#define	SPM_EMU		0x37000004	/* jp spemu_  (SC #255 slow path) */
#define	SPM_SCAN	0x3700000A	/* jp spscan_ (load-time scan) */
#define	SPM_TOP		0x37000010	/* zw sptop */
#define	SPM_USP		0x37000012	/* zw spusp */
#define	SPM_UFP		0x37000014	/* zw spufp */

/* System-only disk cache segment. Buffers occupy NBCB*512 bytes starting
 * at offset zero; the directory signature table starts at 0x4000 and
 * uses DHMAX entries of five bytes each. WD DMA uses physical addresses.
 * Keep NBCB, DHBASE, and the segment capacity consistent. */
#define	BUFSEG		0x33
#define	BUFBASE		0x33000000L
#define	BUFPHYS		0x000C0000L	/* phys 0x0C0000 (DMA target) */
#define	NBCB		32		/* sector buffers in the pool */
#define	DHBASE		0x33004000L	/* NBCB*512 = 0x4000 */
#define	DHPHYS		0x000C4000L
#define	DHMAX		512		/* directory entries the table covers */
					/* per-drive on/off: dskhash.c hashen[] */

#define	SPLITSCW	0x7FFF		/* the patch word: SC #255 */
#define	SPLITMAXT	0xE000		/* max text bytes: the side table's
					 * scan scratch begins at this offset
					 * in the SPLITTSEG segment */

/* Allocator slots pair logical segments with physical 64 KB pages.
 * PGPGLO follows the resident physical layout. pginit() limits capacity
 * using the ROM RAM report and reserves its top page for ROM state.
 *
 * THE POOL MUST NOT OVERLAP THE ROM'S VIDEO DESCRIPTORS.  It used to run
 * 0x38..0x3E, which INCLUDES logical segments 0x3A and 0x3B -- the ROM's
 * two display planes (crsr.c writes character cells in 0x3a and homes the
 * HR bitmap through 0x3b; rom_source/display_re.c:46,47 names them
 * VRAM_A_CHAR and VRAM_A_ATTR).  pgalloc() mapseg()s the segment it hands
 * out, so the third allocation from an empty pool reprogrammed the display
 * descriptor onto a pool page: console writes went into process memory and
 * pgfree() never put the video page back.  No verify target saw it because
 * the emulator's console is serial (crsr.c CK_SER), but on a machine with
 * an LR or HR console the display died at the third allocation and stayed
 * dead.  The pool is therefore moved wholesale, keeping all seven slots --
 * shrinking it would cost the large-model CP/M-86 case, which holds six at
 * once (tests/verify.mk verify-i86).
 *
 * 0x28..0x2E is free: the ROM identity-maps all 64 descriptors at reset
 * (rom_source/reset_re.c:33) and thereafter only ever addresses segments 0,
 * 1, 0x3A, 0x3B and 0x3F; this port programs 0x32, 0x33, 0x35..0x37 and
 * 0x3F (crt.s), and nothing in the tree names a segment between 0x02 and
 * 0x31.  Only pgalloc.c uses PGSEGLO, so moving it is a config change.
 *
 * AS MANY SLOTS AS RAM CAN BACK.  pginit() used to stop at seven whatever
 * the ROM reported, which on a 2560 KB machine left 1536 KB idle.  Slot i
 * needs physical page PGPGLO+i, and RAM cannot reach the LR card's
 * character RAM at 0x37; pginit() keeps the top RAM page for the ROM, so
 * the largest machine backs pages 0x10..0x35 -- 38 slots.  Their segments:
 * slots 0..7 are 0x28..0x2F, exactly the numbers the first seven always
 * were (verify-i86 names them), and slot 8 onwards counts DOWN from 0x27,
 * which ends at 0x0A.  0x02..0x27 is otherwise unused, for the same reason
 * given above.  pgsize() in pgalloc.c does the sizing; tests/pgtest.c runs
 * it for 512, 1024 and 2560 KB, which the emulator (1 MB, fixed) cannot.
 */
#define	SYSPHYSPAGE	0x08		/* phys 0x080000: CPM.SYS's text */
/*
 * The resident system's own two segments, as crt.s maps them.  The kernel
 * is compiled NON-SEGMENTED, so a C pointer to one of its objects is a
 * bare 16-bit offset; map_adr's system space codes (2 = system data, 3 =
 * system program) are how a caller turns such an offset into a far
 * pointer it can hand to mem_cpy or dereference from another segment.
 */
#define	SYSTSEG		0x30		/* CPM.SYS text			 */
#define	SYSDSEG		0x31		/* CPM.SYS data + bss		 */
#define	PGSEGLO		0x28		/* slot 0's logical segment	 */
#define	PGNUP		8		/* slots 0..7 ascend, 0x28..0x2F */
#define	PGNSLOT		38		/* 0x28..0x2F, then 0x27..0x0A --
					 * NOT 0x38..0x3E: 0x3A/0x3B are
					 * the video planes */
#define	PGSEG(i)	((i) < PGNUP ? PGSEGLO + (i) \
				     : PGSEGLO - 1 - ((i) - PGNUP))
#define	PGPGLO		0x10		/* SPLITMPHYSPAGE + 1		 */
/*
 * The video-card probe's scratch segment (crsr.c vprobe, D8).  COHERENT
 * probes through its transient segment ES, 0x3D (md.s:27, 1146-1150); in
 * this BIOS 0x3D is unused too, but 0x2F is chosen because it sits in the
 * range the paragraph above establishes as free (nothing names 0x02..0x31
 * except the pool, 0x28..0x2E), so it cannot collide with a ROM or crt.s
 * descriptor.  crsinit() maps it onto each framebuffer's physical base in
 * turn and puts it back on its identity page afterwards.
 */
#define	VPROBESEG	0x2f
#define	VPROBEADDR	0x2f000000L	/* (long)VPROBESEG<<24 */
#define	VPROBEHOME	0x2f00		/* identity base page, as at reset */
#define	VPHRBASE	0x3e00		/* HR bitmap, phys 0x3E0000 (md.s:1125) */
#define	VPLRBASE	0x3700		/* text framebuffer, phys 0x370000
					 * (md.s:1133) */
#define	PGSEGVIDA	0x3a		/* the ROM's display planes, named
					 * here so the banner above and the
					 * check in pgalloc.c agree	 */
#define	PGSEGVIDB	0x3b
