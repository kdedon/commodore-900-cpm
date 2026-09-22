/ Copyright (c) 2026 Kevin Dedon.
/ SPDX-License-Identifier: MIT

/ bdossc.s -- the BDOS system-call shim for MWC-built transient programs.
/
/ int __bdos(func, param)  int func;  long param;
/
/ Register contract of the SC #2 gate (bdosglue.s / DRI syscall.z8k):
/ r5 = function, rr6 = LONG parameter, result returns in r7.  A far
/ pointer's XADDR value passes through unchanged (the caller runs
/ segmented, so the gate takes the parameter verbatim).  rr6 is
/ callee-saved in the C ABI and the gate restores r7 to the value it
/ finds in the frame, so both are preserved around the trap here.

	.globl	__bdos_
	.shri

__bdos_:
	pushl	(rr14), rr6
	ld	r5, rr14(8)	/ func  (first arg past ret addr + save)
	ldl	rr6, rr14(10)	/ param
	sc	2
	ld	r1, r7		/ result -> C int return
	popl	rr6, (rr14)
	ret

/ long __bdosl(func, param)  int func;  long param;
/
/ The same gate, for the functions whose result is a LONG rather than a
/ word.  Function 50 (direct BIOS call) is the only one today: the BIOS
/ answers a dph address for SELDSK, so a word return would lose half of
/ it.  The gate leaves the long result in the caller's rr6 (bdosglue.s
/ bioscall, `ldl rr14(12), rr0'), which is where it is read from here --
/ so rr6 is copied out BEFORE the saved value is popped back.

	.globl	__bdosl_
	.shri

__bdosl_:
	pushl	(rr14), rr6
	ld	r5, rr14(8)	/ func
	ldl	rr6, rr14(10)	/ param
	sc	2
	ldl	rr0, rr6	/ result -> C long return
	popl	rr6, (rr14)
	ret
