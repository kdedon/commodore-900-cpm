/ Copyright (c) 2026 Kevin Dedon.
/ SPDX-License-Identifier: MIT

/ sstktsc.s -- sc1cpy(src, dst, len): the SC #1 memory gate's copy form.
/ All three are longs; src and dst are XADDRs.

	.globl	sc1cpy_
	.shri

sc1cpy_:
	pushl	(rr14), rr6
	ldl	rr6, rr14(8)	/ src
	ldl	rr4, rr14(12)	/ dst
	ldl	rr2, rr14(16)	/ len
	sc	1
	popl	rr6, (rr14)
	ret
