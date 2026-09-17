/ Copyright (c) 2026 Kevin Dedon.
/ SPDX-License-Identifier: MIT

/ ***************************************************************
/
/	splitmod.s -- resident stand-ins for the relocated split-I/D
/	module's four public symbols.
/
/	splitsc.o + zsplit.o were 17,850 bytes of segment-0x30 text,
/	the largest subsystem in CPM.SYS, for a shim only the four
/	stock 0xEE0B tools need.  They now live in build/split.mod,
/	linked separately at SPLITMBASE, whose image CPM.SYS carries
/	in its data segment and copies into place at cold boot.
/
/	A Z8001 CALL in segmented mode takes a full 32-bit segmented
/	address, so calling into another segment needs no thunk and no
/	change of calling convention -- only an address.  These four
/	equates supply it.  Each names a FIXED offset in the module's
/	interface header (splitent.s), never a linked module address,
/	so the module can be rebuilt, grow or shrink without CPM.SYS
/	being relinked or even recompiled.
/
/	Because they are ordinary globals, every resident reference is
/	unchanged from when the code was resident: bdosglue.s still
/	says `call spemu_', splitfast.s still says `cp r0, sptop_',
/	splitld.c still calls spscan().  The only difference is the
/	segment the linker resolves them into.
/
/ ***************************************************************

#include "c900cfg.h"

	.globl	spemu_			/ bdosglue.s: the SC #255 slow path
	.globl	spscan_			/ splitld.c: the load-time scan
	.globl	sptop_			/ splitfast.s: end of the side table
	.globl	spusp_			/ bdosglue.s: user r15 mirror
	.globl	spufp_			/ bdosglue.s: user r14 mirror

spemu_ = SPM_EMU
spscan_ = SPM_SCAN
sptop_ = SPM_TOP
spusp_ = SPM_USP
spufp_ = SPM_UFP
