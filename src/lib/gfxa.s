/ Copyright (c) 2026 Kevin Dedon.
/ SPDX-License-Identifier: MIT

/ gfxa.s -- the drawing library's byte loops, for the target build.
/
/ gfxb.c holds the same functions in C and stays the reference.  Each
/ runs inside one segment: callers split at the surface's segment end.
/ Arguments start at rr14(4); r0..r5 are scratch.

	.shri
	.globl	gfx_bcopy_, gfx_bfill_, gfx_bxor_, gfx_m1row_
	.globl	gfx_c4row_

/ gfx_bcopy(d, s, n)  -- n bytes, lowest first, so d may sit below s.
/ Words when d and s are both even.
gfx_bcopy_:
	ldl	rr4, rr14(4)		/ d
	ldl	rr2, rr14(8)		/ s
	ld	r1, rr14(12)		/ n
	test	r1
	jr	z, 9f
	ld	r0, r3
	or	r0, r5
	bit	r0, $0
	jr	nz, 2f
	ld	r0, r1
	srl	r0, $1
	jr	z, 1f
	ldir	@rr4, @rr2, r0
1:
	bit	r1, $0
	jr	z, 9f
	ldb	rl0, @rr2
	ldb	@rr4, rl0
	jr	9f
2:
	ldirb	@rr4, @rr2, r1
9:
	ret

/ gfx_bfill(d, n, v)  -- n bytes of v.  An even word of v, then a word
/ ldir reading one word behind its writes.
gfx_bfill_:
	ldl	rr2, rr14(4)		/ d
	ld	r1, rr14(8)		/ n
	ldb	rl0, rr14(11)		/ v
	ldb	rh0, rl0
	test	r1
	jr	z, 9f
	bit	r3, $0
	jr	z, 1f
	ldb	@rr2, rl0
	inc	r3, $1
	dec	r1, $1
1:
	ld	r4, r1
	srl	r4, $1			/ words
	jr	z, 3f
	ld	@rr2, r0
	ldl	rr4, rr2
	inc	r5, $2
	ld	r0, r1
	srl	r0, $1
	dec	r0, $1
	jr	z, 2f
	ldir	@rr4, @rr2, r0
2:
	ldl	rr2, rr4		/ the odd last byte, if any
	ldb	rl0, rr14(11)
3:
	bit	r1, $0
	jr	z, 9f
	ldb	@rr2, rl0
9:
	ret

/ gfx_m1row(p, q, n, s, hi, avail, m0, m1, op)  -- see gfxb.c.
/ rh0:rl0 is the source window, shifted left s so rh1 is the byte's
/ source; rh6 the byte's mask, rl6 the last byte's.
gfx_m1row_:
	sub	r15, $12
	ldm	@rr14, r6, $6
	ldl	rr2, rr14(16)		/ p
	ldl	rr4, rr14(20)		/ q
	ld	r9, rr14(24)		/ n
	ld	r8, rr14(26)		/ s
	ldb	rh0, rr14(29)		/ hi
	ld	r10, rr14(30)		/ avail
	ldb	rh6, rr14(33)		/ m0
	ldb	rl6, rr14(35)		/ m1
	ld	r11, rr14(36)		/ op
	test	r9
	jr	z, 9f
1:
	clrb	rl0
	test	r10
	jr	z, 2f
	ldb	rl0, @rr4
	inc	r5, $1
	dec	r10, $1
2:
	ld	r1, r0
	sdl	r1, r8
	cp	r9, $1
	jr	ne, 3f
	andb	rh6, rl6
3:
	andb	rh1, rh6
	ldb	rl7, @rr2
	cp	r11, $2
	jr	ne, 4f
	xorb	rl7, rh1		/ xor
	jr	8f
4:
	jr	gt, 6f			/ erase
	test	r11
	jr	nz, 5f
	ldb	rh7, rh6		/ set
	comb	rh7
	andb	rl7, rh7
	orb	rl7, rh1
	jr	8f
5:
	ldb	rh1, rh6		/ clear
6:
	comb	rh1
	andb	rl7, rh1
8:
	ldb	@rr2, rl7
	inc	r3, $1
	ldb	rh0, rl0
	ldb	rh6, $0xff
	djnz	r9, 1b
9:
	ldm	r6, @rr14, $6
	add	r15, $12
	ret

/ gfx_c4row(p, q, n, s, hi, avail, t, op)  -- see gfxb.c.
/ The window as in gfx_m1row; rh1 holds a source byte's bits still to
/ use, rl1 how many pixel pairs are left in it.
gfx_c4row_:
	sub	r15, $14
	ldm	@rr14, r6, $7
	ldl	rr2, rr14(18)		/ p
	ldl	rr4, rr14(22)		/ q
	ld	r9, rr14(26)		/ n
	ld	r8, rr14(28)		/ s
	ldb	rh0, rr14(31)		/ hi
	ld	r10, rr14(32)		/ avail
	ldl	rr6, rr14(34)		/ t
	ld	r11, rr14(38)		/ op
	test	r9
	jr	z, 9f
1:
	clrb	rl0
	test	r10
	jr	z, 2f
	ldb	rl0, @rr4
	inc	r5, $1
	dec	r10, $1
2:
	ld	r1, r0
	sdl	r1, r8
	ldb	rh0, rl0
	ldb	rl1, $4
3:
	ld	r12, r1
	srl	r12, $14
	ldb	rl0, rr6(r12)
	test	r11
	jr	nz, 4f
	ldb	@rr2, rl0		/ set
	jr	6f
4:
	cp	r11, $2
	jr	ne, 5f
	xorb	rl0, @rr2		/ xor
	ldb	@rr2, rl0
	jr	6f
5:
	andb	rl0, @rr2		/ erase
	ldb	@rr2, rl0
6:
	inc	r3, $1
	dec	r9, $1
	jr	z, 9f
	sllb	rh1, $2
	decb	rl1, $1
	jr	nz, 3b
	jr	1b
9:
	ldm	r6, @rr14, $7
	add	r15, $14
	ret

/ gfx_bxor(d, n, v)  -- n bytes XORed with v.
gfx_bxor_:
	ldl	rr2, rr14(4)		/ d
	ld	r1, rr14(8)		/ n
	ld	r0, rr14(10)		/ v
	test	r1
	jr	z, 9f
1:
	ldb	rh0, @rr2
	xorb	rh0, rl0
	ldb	@rr2, rh0
	inc	r3, $1
	djnz	r1, 1b
9:
	ret
