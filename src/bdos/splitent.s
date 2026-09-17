/ Copyright (c) 2026 Kevin Dedon.
/ SPDX-License-Identifier: MIT

/ ***************************************************************
/
/	splitent.s -- the split-I/D module's fixed interface header.
/
/	This file is NOT part of CPM.SYS.  It is linked FIRST into
/	build/split.mod, the separately linked module that holds the
/	SC #255 slow path (splitsc.c), the instruction decoder
/	(zsplit.c) and the load-time scanner (splitscan.c).  The
/	module is relocated to segment SPLITMSEG and its image is
/	carried in CPM.SYS's data segment (splitimg.o), copied into
/	place at cold boot (cmain.c).
/
/	Everything the resident system reaches in the module reaches
/	it through the block below, at FIXED offsets from the segment
/	base.  The resident side spells those offsets out in
/	c900cfg.h and turns them into ordinary global symbols with
/	assembler equates (sys/splitmod.s), so no resident source
/	knows that spemu_ or sptop_ moved.  Nothing resident refers to
/	a module symbol by its linked address, which is what keeps the
/	module rebuildable without relinking CPM.SYS -- the `ld -k'
/	staleness trap, avoided by construction rather than by rule.
/
/	The offsets are asserted after every module link (host/
/	mkblob.py): a link that moves them fails the build.
/
/	The three words at the end are live state, not constants:
/	sptop_ is written by the scanner and read by the resident
/	assembly fast path (splitfast.s) on every trap, and spusp_/
/	spufp_ are the user r15/r14 mirrors the SC gate (bdosglue.s)
/	fills before entering the emulator.  They live here, in the
/	module's own segment, because that is the only place both
/	sides can name without either one being relinked; the segment
/	is mapped SYS r/w (attr 0x02), so text-resident data is
/	writable here in a way it is not in CPM.SYS's segment 0x30.
/
/ ***************************************************************

#include "c900cfg.h"

/ The segment every compiled frame reference is relocated against: cc2 emits
/ each frame address's segment byte as a relocation adding SS.  The module is
/ linked on its own, so it states this itself: its C runs on the resident
/ system's stack -- the SC gate enters it there and cmain_ calls the scanner
/ from it -- which crt.s maps at segment 0x3F.  Both bytes carry it, as the
/ Coherent kernel's md.s spells it.
	.globl	SS
SS	=	0x3f3f

	.shri

	.globl	spment_			/ +0:  the header itself
	.globl	spemu_			/ splitsc.c
	.globl	spscan_			/ splitscan.c
	.globl	sptop_			/ +16: read by splitfast.s
	.globl	spusp_			/ +18: written by bdosglue.s
	.globl	spufp_			/ +20: written by bdosglue.s

spment_:
	.word	SPM_MAGIC		/ +0
	.word	SPM_VERS		/ +2
ment0:
	jp	spemu_			/ +4:  SC #255 slow path
	.blkb	6+ment0-.
ment1:
	jp	spscan_			/ +10: load-time scan and patch
	.blkb	6+ment1-.
sptop_:
	.word	0			/ +16
spusp_:
	.word	0			/ +18
spufp_:
	.word	0			/ +20
