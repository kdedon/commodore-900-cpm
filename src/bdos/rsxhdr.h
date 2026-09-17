/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/* Fixed RSX prefix. Entries and links are 16-bit TPA offsets.
 * org and len describe a fixed-address image; there is no relocation.
 * Layout derives from the CP/M 3 prefix (ref/cpm3/getrsx.asm). */

#define	RSXMAGIC	0x5253		/* 'RS' at prefix offset 8	*/
#define	RSXNAMELEN	8

struct rsxhdr {
	UBYTE	serial[6];	/* 00: system serial number		*/
	UWORD	entry;		/* 06: TPA offset of the intercept code	*/
	UWORD	magic;		/* 08: RSXMAGIC				*/
	UWORD	next;		/* 0A: next module, 0 = the BDOS itself	*/
	UWORD	prev;		/* 0C: previous module, 0 = none	*/
	UBYTE	warmflg;	/* 0E: 0FFh = remove at warm boot	*/
	UBYTE	nbank;		/* 0F: non-banked-only flag (unused)	*/
	UBYTE	name[RSXNAMELEN]; /* 10: module name, blank padded	*/
	UBYTE	endchain;	/* 18: 0FFh on the topmost module	*/
	UBYTE	rsvd1;		/* 19					*/
	UBYTE	patch;		/* 1A: patch level			*/
	UBYTE	rsvd2;		/* 1B					*/
	UWORD	org;		/* 1C: TPA offset this module is linked	*/
				/*     for -- see note 3 above		*/
	UWORD	len;		/* 1E: total bytes of the module image	*/
};

#define	RSXHDRLEN	32	/* sizeof (struct rsxhdr)		*/

/* serial, patch and endchain are descriptive prefix metadata. GENCOM uses
 * nbank to mark temporary modules; removal follows next links until zero. */

/*  The parameter block of BDOS function 60.  v3 passes DE = the address
    of a block whose first byte is the sub-function number, which is all
    the RSXes read (getrsx.asm:246-249, `ldax d' then compare).  Ours
    keeps that and defines the fields the attach service needs after it. */

struct rsxpb {
	UBYTE	rpfunc;		/* 0: sub-function number		*/
	UBYTE	rprsvd;		/* 1					*/
	UWORD	rporg;		/* 2: attach: TPA offset to place at	*/
	UWORD	rplen;		/* 4: attach: image length		*/
	XADDR	rpsrc;		/* 6: attach: where the image is now	*/
};

/*  Sub-function numbers.  128..143 are spoken for by v3's own RSXes
    (getrsx.asm:92-104: GET 128-130/140, PUT 132-138, JOURNAL 141-143),
    so the system's own services sit below them.  */

#define	RSX_ATTACH	127	/* attach the module described by the pb	*/
#define	RSX_QUERY	126	/* -> the chain head, 0 = no modules	*/

#define	RSX_NOTHANDLED	0xff	/* fn 60 return when nothing claimed it	*/

/*  Why an attach was refused.  v3 has no attach service to be faithful
    to -- its loader either relocates the module or prints `Cannot load
    Program' and warm boots (loader3.asm:197-201) -- so these are ours,
    and they exist because every one of them is a build mistake a person
    will make: an image that is not a module, one linked for a different
    address than it claims, or one that no longer fits under the chain. */

#define	RSX_EPB		0xf1	/* the parameter block is not usable	*/
#define	RSX_EROOM	0xf2	/* it does not fit below the chain	*/
#define	RSX_EMAGIC	0xf3	/* no RSXMAGIC: not a module		*/
#define	RSX_EORG	0xf4	/* org/len disagree with the caller	*/
