/ pipjmp.s -- setjmp/longjmp for PIP using the MWC Z8001 ABI.
/ The 12-word buffer holds the return PC, r6-r13, and rr14. Save SP
/ after popping the return PC so later calls cannot overwrite the saved
/ return address. The staged setjmp.h must allocate 12 words.
/ Leaf arguments follow the four-byte segmented return address.

	.globl	setjmp_
	.globl	longjmp_
	.shri

setjmp_:
	popl	rr4, (rr14)	/ rr4 = return PC; SP now addresses the arg
	ldl	rr2, (rr14)	/ rr2 = jmp_buf pointer
	ldm	(rr2), r4, $12	/ buf = PC, r6..r13, and RR14 as it now stands
	ldk	r1, $0		/ direct call: return 0
	jp	(rr4)		/ return, leaving the arg for the caller to pop

longjmp_:
	ldl	rr2, rr14(4)	/ rr2 = jmp_buf pointer
	ld	r1, rr14(8)	/ r1 = val (becomes setjmp's return value)
	ldm	r4, (rr2), $12	/ PC, r6..r13 and SP all back at once
	jp	(rr4)		/ resume where setjmp returned, with the
				/   stack exactly as setjmp left it
