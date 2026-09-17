/ Copyright (c) 2026 Kevin Dedon.
/ SPDX-License-Identifier: MIT

/ biossc.s -- the raw SC #3 BIOS shim for MWC-built transient programs.
/
/ long __bios(func, p1, p2)  int func;  long p1, p2;
/
/ Register contract of the SC #3 gate (bdosglue.s `biosgate', header
/ comment: "SC #3 BIOS: r3 = function, rr4 = P1, rr6 = P2, result ->
/ rr6" -- the same syscall.z8k _bios contract stock DRI binaries trap
/ through).  This is the RAW gate: unlike SC #2 function 50 (bioscl(),
/ sys/iosys.c), it has no refusal list, so it is the only route a TPA
/ program has to a BIOS function bioscl() refuses -- in particular
/ CONOUT (2-7) and the BIOCOST-only codes 100/101 that src/bios/
/ bios900.c defines purely for this exerciser (cpm.h BIOS_ROMCHAR,
/ BIOS_VSETCHAR).  A far pointer's XADDR value passes through
/ unchanged (the caller runs segmented).
/
/ rr6 is callee-saved in the C ABI (bdossc.s's header comment) and the
/ gate leaves the LONG result there, so it is saved before the trap and
/ restored after the result is copied out -- same shape as __bdosl in
/ bdossc.s.  rr4/r3 are caller-saved: nothing here need preserve them.

	.globl	__bios_
	.shri

__bios_:
	pushl	(rr14), rr6
	ld	r3, rr14(8)	/ func   (word arg, first past ret addr + save)
	ldl	rr4, rr14(10)	/ p1     (long)
	ldl	rr6, rr14(14)	/ p2     (long)
	sc	3
	ldl	rr0, rr6	/ result -> C long return
	popl	rr6, (rr14)
	ret
