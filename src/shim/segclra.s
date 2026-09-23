/ Copyright (c) 2026 Kevin Dedon.
/ SPDX-License-Identifier: MIT

/ segclra.s -- zero a 64 KB guest segment, for the target build.
/
/	int segclr(p)  char *p;
/
/ Same result as segclr.c, which stays the portable reference.  Eight
/ zeroed registers are stored 16 bytes at a time; p's offset wraps, so
/ an even p anywhere in the segment clears all of it.

	.shri
	.globl	segclr_

segclr_:
	ldl	rr2, rr14(4)
	sub	r15, $10
	ldm	(rr14), r6, $5		/ r6..r10 are callee-saved
	ldl	rr8, rr2
	clr	r0
	clr	r1
	clr	r2
	clr	r3
	clr	r4
	clr	r5
	clr	r6
	clr	r7
	ld	r10, $4096
1:
	ldm	(rr8), r0, $8
	inc	r9, $16
	djnz	r10, 1b
	ldm	r6, (rr14), $5
	add	r15, $10
	ret
