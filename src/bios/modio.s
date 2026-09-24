/ Copyright (c) 2026 Kevin Dedon.
/ SPDX-License-Identifier: MIT

/ Modules' own copies of glue.s's port and copy helpers: a module cannot
/ call into cpm.sys.

	.globl	inb_, outb_, mem_cpy_
	.shri

/ int inb(port)
inb_:
	ld	r1, rr14(4)
	inb	rl1, (r1)
	subb	rh1, rh1
	ret

/ outb(port, val)
outb_:
	ld	r1, rr14(4)
	ld	r0, rr14(6)
	outb	(r1), rl0
	ret

/ mem_cpy(src, dst, len)  XADDR src, dst; long len;  low word of len used.
mem_cpy_:
	ldl	rr2, rr14(4)
	ldl	rr4, rr14(8)
	ld	r0, rr14(14)
	test	r0
	jr	eq, 1f
	ldirb	@rr4, @rr2, r0
1:
	ret
