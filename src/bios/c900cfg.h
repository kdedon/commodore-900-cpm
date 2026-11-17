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
 * load-time scanner's side table in a third.  Both are pool pages mapped
 * when such a program loads, SYS-only: the program itself never addresses
 * them -- every access goes through the SC #255 trap handler (splitsc.c)
 * or the BDOS gate's split-aware pointer mapping (bdosglue.s).  Neither
 * segment is in the MRT, so the BDOS never offers them as TPA.
 */
#define	SPLITDSEG	0x35		/* data bank */
#define	SPLITDBASE	0x35000000L
#define	SPLITTSEG	0x36		/* side table + scanner scratch */
#define	SPLITTBASE	0x36000000L
/* Modules (mk/config.mk MODULES): each is linked alone at the base of its
 * own logical segment.  Their images load over cpm.sys's bss; cmain.c
 * copies them into this physical page at cold boot, packed on 1 KB
 * boundaries, and maps each segment onto its slice.  The page stops at MODLIM, short
 * of the ROM's state at physical 0x0FE400 on a 512 KB machine.
 * Every segment base must be 1 KB-aligned: A8 and A9 bypass the MMU. */
#define	MODPHYSPAGE	0x0f		/* phys 0x0F0000 */
#define	MODLIM		0xE3		/* last usable 256-byte block */

/* System-only disk cache segment, in page 0x09 between CPM.SYS's bss and
 * the supervisor stacks, based on a 1 KB boundary. Buffers occupy NBCB*512
 * bytes starting at offset zero; the directory signature table starts at
 * 0x4000 and uses DHMAX entries of five bytes each, ending 0x200 below the
 * lowest stack. WD DMA uses physical addresses. Keep NBCB, DHBASE, BUFLIM
 * and DHPHYS consistent. */
#define	BUFSEG		0x33
#define	BUFBASE		0x33000000L
#define	BUFPHYS		0x00094000L	/* phys 0x094000 (DMA target) */
#define	BUFPAGE		0x0940		/* mapseg base page */
#define	BUFLIM		0x49		/* last 256-byte block */
#define	NBCB		32		/* sector buffers in the pool */
#define	DHBASE		0x33004000L	/* NBCB*512 = 0x4000 */
#define	DHPHYS		0x00098000L
#define	DHMAX		512		/* directory entries the table covers */
					/* per-drive on/off: dskhash.c hashen[] */
/* The CCP image cache: a pool page mapped System-only on CCPCSEG only
 * while the loader fills or copies it, CPU-inhibited otherwise. */
#define	CCPCSEG		0x3e
#define	CCPCBASE	0x3e000000L
#define	CCPCMAX		480		/* records; 60 KB */
#define	LDCOFF		0		/* ldcmode: read the disk only	*/
#define	LDCFILL		1		/*   read it and fill the cache	*/
#define	LDCPLAY		2		/*   read the cache		*/

/* Supervisor stacks: segment 0x3F is based on phys page 0x09, so a stack
 * offset is also the offset in that page.  Seven 4 KB stacks run down from
 * SSTKTOP to 0x8C00, above the disk cache.  The descriptor is a DIRW stack
 * segment whose limit is the running stack's lowest block: a write there
 * traps, and anything below it faults. */
#define	SSTKPAGE	0x0900		/* phys 0x090000 (mapseg base page) */
#define	SSTKTOP		0xfc00
#define	SSTKSZ		0x1000
#define	SSTKATTR	0x22		/* SYS | DIRW */

#define	SPLITSCW	0x7FFF		/* the patch word: SC #255 */
#define	SPLITMAXT	0xE000		/* max text bytes: the side table's
					 * scan scratch begins at this offset
					 * in the SPLITTSEG segment */

/* Allocator slots are physical 64 KB pages, from PGPGLO to the end of the
 * RAM the ROM reports, less its top page (ROM state), the video holes and
 * the resident pages PGSYSPG names.
 * A slot takes one of PGNSEG logical segments while allocated; a parked
 * process image gives its segment back (pgalloc.c).
 *
 * THE POOL MUST NOT OVERLAP THE ROM'S VIDEO DESCRIPTORS.  It used to run
 * 0x38..0x3E, which INCLUDES logical segments 0x3A and 0x3B -- the ROM's
 * two display planes (crsr.c writes character cells in 0x3a and homes the
 * HR bitmap through 0x3b).  pgalloc() mapseg()s the segment it hands
 * out, so the third allocation from an empty pool reprogrammed the display
 * descriptor onto a pool page: console writes went into process memory and
 * pgfree() never put the video page back.  A serial console never shows
 * it; on a machine with an LR or HR console the display died at the third
 * allocation and stayed dead.  The pool is therefore moved wholesale,
 * keeping all seven slots -- shrinking it would cost the large-model
 * CP/M-86 case, which holds six at once.
 *
 * 0x28..0x2E is free: the ROM identity-maps all 64 descriptors at reset
 * and thereafter only ever addresses segments 0,
 * 1, 0x3A, 0x3B and 0x3F; this port programs 0x32..0x36, 0x3E, 0x3F and the
 * module segments (0x37..0x39, 0x3C, 0x3D), and nothing in the tree names a segment between 0x02 and
 * 0x31.  Only pgalloc.c uses PGSEGLO, so moving it is a config change.
 *
 * The logical segments are 0x28..0x2F, then 0x27 down to 0x0A, handed out
 * in that order.  Physical pages run to 16 MB; the MMU reaches past the
 * motherboard's 4 MB decode.  The holes are skipped whatever card is
 * fitted: the BIOS does not probe when the loader picks the console, and
 * a probe cannot tell HR bitmap RAM from memory.  Without an HR card,
 * 0x3F is the ROM's stack RAM.
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
#define	PGNSEG		38		/* 0x28..0x2F, then 0x27..0x0A --
					 * NOT 0x38..0x3E: 0x3A/0x3B are
					 * the video planes */
#define	PGSEG(i)	((i) < PGNUP ? PGSEGLO + (i) \
				     : PGSEGLO - 1 - ((i) - PGNUP))
#define	PGPGLO		0x0b		/* lowest page the system frees	 */
#define	PGSYSPG		{ MODPHYSPAGE }	/* still resident */
#define	PGNSYSPG	1
#define	PGPGHI		0x100		/* 16 MB: one past the last page */
#define	PGLRTEXT	0x37		/* never RAM, even in a loader's map */
#define	PGHOLES		{ PGLRTEXT, 0x3e, 0x3f }	/* LR text, HR bitmap */
#define	PGNHOLE		3
#define	PGNSLOT		(PGPGHI - 1 - PGPGLO - PGNHOLE - PGNSYSPG)	/* 240 */
#define	PGPARK		0x40		/* handle of parked slot i: PGPARK+i */
/*
 * The video-card probe's scratch segment (crsinit.c vprobe, D8).  COHERENT
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
