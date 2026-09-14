
	.globl	setjmp_
	.globl	longjmp_
	.shri

setjmp_:
	ldk	r1, $0		/ direct call: return 0

longjmp_:
	ldl	rr2, rr14(4)	/ rr2 = jmp_buf pointer
	ld	r1, rr14(8)	/ r1 = val (becomes setjmp's return value)
