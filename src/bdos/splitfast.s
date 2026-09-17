/ Copyright (c) 2026 Kevin Dedon.
/ SPDX-License-Identifier: MIT

/ ***************************************************************
/
/	splitfast.s -- assembly fast path for the split-I/D SC trap
/
/	Entered from splitgate (bdosglue.s) with the full SC frame
/	built: caller r0-r13 saved at (rr14), then identifier, FCW,
/	PCseg, PCoff at +28/+30/+32/+34.  A nonseg caller's r14/r15
/	are banked in NSPSEG/NSPOFF.  Interrupts are off (gate FCW).
/
/	Handles the dominant no-flag data-access forms of a patched
/	0xEE0B instruction -- plain loads and stores (byte/word/long,
/	IR/DA/X/BA/BX), immediate stores, clears and push-immediate --
/	entirely in assembly against the saved frame, then resumes via
/	the common register-restore/IRET tail (spret_).  Any other
/	form, or a PC outside the side table, jumps to spslow_, the
/	original C route (spemu in splitsc.c), which emulates every
/	remaining form and owns the not-patched/unimplemented policy.
/	Each handled case mirrors its splitsc.c fast-path twin exactly,
/	including the n2 == 0 escapes and the RR14 operand escapes.
/
/	Bank routing matches splitsc.c bnk(): an effective address at
/	or above the user SP (NSPOFF at trap time) is the live stack
/	region in the CODE bank (the TPA); below it is the data bank.
/	A push routes by its pre-push SP (address + width).
/
/	Register plan after dispatch (all are frame-saved, so free):
/	  r0  = text offset of the SC word (becomes the resumed PC)
/	  r1  = w0, the original first instruction word
/	  r5  = w1, the second instruction word
/	  r6  = user SP (bank boundary)
/	  r8  = n3, w0 bits 3..0	r9  = n2, w0 bits 7..4
/	  r10 = instruction length in bytes for the PC advance
/	  r7  = long/byte data scratch (rh7/rl7)
/	  r2:r3 = far pointer to the routed bank; r4 = EA / word data
/	  rr12 = frame base (rr14 copy: CALR moves rr14, frame refs
/		 must not)
/
/ ***************************************************************

#include "c900cfg.h"

	.shri

	.globl	spfast_
	.globl	spslow_		/ bdosglue.s: the C emulator route
	.globl	spret_		/ bdosglue.s: restore r0-r13 + IRET
	.globl	sptop_		/ splitsc.c: first offset past the table

spfast_:
	ld	r12, r14	/ stable frame base: CALR pushes on rr14
	ld	r13, r15
	ld	r0, rr12(34)	/ trap PC offset
	sub	r0, $2		/ = text offset of the SC word
	cp	r0, sptop_
	jr	uge, miss
	ld	r2, $[SPLITTSEG*256]
	ld	r3, r0
	ld	r1, (rr2)	/ w0 = sptab[offset >> 1]
	test	r1
	jr	z, miss		/ not a patch site
	ld	r2, $[TPASEG*256]
	ld	r3, r0
	add	r3, $2
	ld	r5, (rr2)	/ w1 (unused by 1-word forms)
	ld	r2, r1
	srl	r2, $8		/ opcode byte
	cp	r2, $0x80
	jr	uge, miss	/ table covers 0x00-0x7F only
	add	r2, r2
	ldar	rr10, jtab
	add	r11, r2
	ld	r11, (rr10)	/ case label offset
	ld	r9, r1
	srl	r9, $4
	and	r9, $15		/ n2
	ld	r8, r1
	and	r8, $15		/ n3
	ldctl	r6, NSPOFF	/ user SP = bank boundary
	jp	(rr10)

miss:
	jp	spslow_

/ ---- per-opcode cases: leave EA in r4, length in r10, then join a
/ ---- shared move tail

/ @Rs loads (n2 == 0 is not IR: leave it to the C decode)
c20:	test	r9
	jp	z, miss
	ld	r10, $2
	ld	r3, r9
	calr	getw_
	jp	tbload
c21:	test	r9
	jp	z, miss
	ld	r10, $2
	ld	r3, r9
	calr	getw_
	jp	twload
c14:	test	r9
	jp	z, miss
	cp	r8, $14
	jp	uge, miss	/ RR14 lives banked: C path
	ld	r10, $2
	ld	r3, r9
	calr	getw_
	jp	tlload

/ @Rd stores
c2e:	ld	r10, $2
	ld	r3, r9
	calr	getw_
	jp	tbstore
c2f:	ld	r10, $2
	ld	r3, r9
	calr	getw_
	jp	twstore
c1d:	cp	r8, $14
	jp	uge, miss
	ld	r10, $2
	ld	r3, r9
	calr	getw_
	jp	tlstore

/ DA|X loads
c60:	ld	r10, $4
	calr	eadax_
	jp	tbload
c61:	ld	r10, $4
	calr	eadax_
	jp	twload
c54:	cp	r8, $14
	jp	uge, miss
	ld	r10, $4
	calr	eadax_
	jp	tlload

/ DA|X stores
c6e:	ld	r10, $4
	calr	eadax_
	jp	tbstore
c6f:	ld	r10, $4
	calr	eadax_
	jp	twstore
c5d:	cp	r8, $14
	jp	uge, miss
	ld	r10, $4
	calr	eadax_
	jp	tlstore

/ base+displacement loads (n2 == 0 is LDR: never patched)
c30:	test	r9
	jp	z, miss
	ld	r10, $4
	ld	r3, r9
	calr	getw_
	add	r4, r5
	jp	tbload
c31:	test	r9
	jp	z, miss
	ld	r10, $4
	ld	r3, r9
	calr	getw_
	add	r4, r5
	jp	twload
c35:	test	r9
	jp	z, miss
	cp	r8, $14
	jp	uge, miss
	ld	r10, $4
	ld	r3, r9
	calr	getw_
	add	r4, r5
	jp	tlload

/ base+displacement stores
c32:	test	r9
	jp	z, miss
	ld	r10, $4
	ld	r3, r9
	calr	getw_
	add	r4, r5
	jp	tbstore
c33:	test	r9
	jp	z, miss
	ld	r10, $4
	ld	r3, r9
	calr	getw_
	add	r4, r5
	jp	twstore
c37:	test	r9
	jp	z, miss
	cp	r8, $14
	jp	uge, miss
	ld	r10, $4
	ld	r3, r9
	calr	getw_
	add	r4, r5
	jp	tlstore

/ base+index loads
c70:	ld	r10, $4
	calr	eabx_
	jp	tbload
c71:	ld	r10, $4
	calr	eabx_
	jp	twload
c75:	cp	r8, $14
	jp	uge, miss
	ld	r10, $4
	calr	eabx_
	jp	tlload

/ base+index stores
c72:	ld	r10, $4
	calr	eabx_
	jp	tbstore
c73:	ld	r10, $4
	calr	eabx_
	jp	twstore
c77:	cp	r8, $14
	jp	uge, miss
	ld	r10, $4
	calr	eabx_
	jp	tlstore

/ @Rd byte immediate store (n3 = 5) and clear (n3 = 8)
c0c:	cp	r8, $5
	jr	eq, 1f
	cp	r8, $8
	jp	ne, miss
	ld	r10, $2
	ld	r3, r9
	calr	getw_
	calr	bank_
	clr	r7
	ldb	(rr2), rh7
	jp	setpc
1:	ld	r10, $4
	ld	r3, r9
	calr	getw_
	calr	bank_
	ldb	(rr2), rh5	/ byte immediate = high half of w1
	jp	setpc

/ @Rd word immediate store / clear / push-immediate (n3 = 5/8/9)
c0d:	cp	r8, $5
	jr	eq, 1f
	cp	r8, $8
	jr	eq, 2f
	cp	r8, $9
	jp	ne, miss
	ld	r10, $4		/ PUSH @Rd, #imm
	ld	r3, r9
	calr	getw_
	sub	r4, $2
	ld	r3, r9
	calr	putw_		/ pre-decrement the push pointer
	ld	r7, r4		/ route by the pre-push SP (EA + 2)
	add	r7, $2
	cp	r7, r6
	jr	uge, 3f
	ld	r2, $[SPLITDSEG*256]
	jr	4f
3:	ld	r2, $[TPASEG*256]
4:	ld	r3, r4
	ld	(rr2), r5
	jp	setpc
1:	ld	r10, $4
	ld	r3, r9
	calr	getw_
	calr	bank_
	ld	(rr2), r5
	jp	setpc
2:	ld	r10, $2
	ld	r3, r9
	calr	getw_
	calr	bank_
	clr	r7
	ld	(rr2), r7
	jp	setpc

/ DA|X byte immediate store (n3 = 5) and clear (n3 = 8)
c4c:	cp	r8, $5
	jr	eq, 1f
	cp	r8, $8
	jp	ne, miss
	ld	r10, $4
	calr	eadax_
	calr	bank_
	clr	r7
	ldb	(rr2), rh7
	jp	setpc
1:	ld	r10, $6		/ immediate = w2, the third word
	ld	r2, $[TPASEG*256]
	ld	r3, r0
	add	r3, $4
	ld	r7, (rr2)
	calr	eadax_
	calr	bank_
	ldb	(rr2), rh7	/ byte immediate in the high half
	jp	setpc

/ DA|X word immediate store (n3 = 5) and clear (n3 = 8)
c4d:	cp	r8, $5
	jr	eq, 1f
	cp	r8, $8
	jp	ne, miss
	ld	r10, $4
	calr	eadax_
	calr	bank_
	clr	r7
	ld	(rr2), r7
	jp	setpc
1:	ld	r10, $6
	ld	r2, $[TPASEG*256]
	ld	r3, r0
	add	r3, $4
	ld	r7, (rr2)
	calr	eadax_
	calr	bank_
	ld	(rr2), r7
	jp	setpc

/ ---- shared move tails: EA in r4 on entry ----

twload:	calr	bank_
	ld	r4, (rr2)
	ld	r3, r8
	calr	putw_
	jr	setpc

tbload:	calr	bank_
	ldb	rl7, (rr2)
	ld	r3, r8
	calr	putb_
	jr	setpc

tlload:	calr	bank_
	ld	r7, r8		/ frame byte offset of RRn's high word
	and	r7, $14
	add	r7, r7
	ld	r4, (rr2)
	ld	rr12(r7), r4
	add	r3, $2		/ offsets wrap mod 64K like the CPU's
	add	r7, $2
	ld	r4, (rr2)
	ld	rr12(r7), r4
	jr	setpc

twstore:
	ld	r2, r4		/ EA aside: the value fetch uses r3/r4
	ld	r3, r8
	calr	getw_
	ld	r7, r4		/ value
	ld	r4, r2
	calr	bank_
	ld	(rr2), r7
	jr	setpc

tbstore:
	ld	r3, r8
	calr	getb_
	calr	bank_
	ldb	(rr2), rl7
	jr	setpc

tlstore:
	calr	bank_
	ld	r7, r8
	and	r7, $14
	add	r7, r7
	ld	r4, rr12(r7)
	ld	(rr2), r4
	add	r3, $2
	add	r7, $2
	ld	r4, rr12(r7)
	ld	(rr2), r4
	jr	setpc

setpc:	add	r0, r10
	ld	rr12(34), r0	/ resume past the emulated instruction
	jp	spret_

/ ---- helpers (CALR: frame refs stay off rr14) ----
/
/ Frame slots are reached base+index, and the base is written OUTSIDE
/ the parentheses, exactly as in setpc's `ld rr12(34), r0' above:
/ `ld r4, rr12(r3)' is base rr12 indexed by r3, 0x71C4 0x0300.  The
/ mirror image `ld r4, r3(rr12)' assembles without complaint into base
/ RR3 indexed by r12, 0x7134 0x0C00 -- an address built from whatever
/ rr2 holds, which in this file is the dispatch scratch.

/ word register read: nibble in r3 -> r4; r14/r15 live banked
getw_:	cp	r3, $14
	jr	uge, 1f
	add	r3, r3
	ld	r4, rr12(r3)
	ret
1:	jr	eq, 2f
	ldctl	r4, NSPOFF
	ret
2:	ldctl	r4, NSPSEG
	ret

/ word register write: nibble in r3, value in r4
putw_:	cp	r3, $14
	jr	uge, 1f
	add	r3, r3
	ld	rr12(r3), r4
	ret
1:	jr	eq, 2f
	ldctl	NSPOFF, r4
	ret
2:	ldctl	NSPSEG, r4
	ret

/ byte register read: nibble in r3 -> rl7 (0-7 = RH0-7, 8-15 = RL0-7;
/ RHn is the even frame byte of Rn, RLn the odd one)
getb_:	ld	r2, r3
	and	r3, $7
	add	r3, r3
	srl	r2, $3
	add	r3, r2
	ldb	rl7, rr12(r3)
	ret

/ byte register write: nibble in r3, value in rl7
putb_:	ld	r2, r3
	and	r3, $7
	add	r3, r3
	srl	r2, $3
	add	r3, r2
	ldb	rr12(r3), rl7
	ret

/ effective address for DA (n2 = 0) or X: r4 = w1 [+ Rn2]
eadax_:	test	r9
	jr	z, 1f
	ld	r3, r9
	calr	getw_
	add	r4, r5
	ret
1:	ld	r4, r5
	ret

/ effective address for BX: r4 = Rn2 + Rx (x = w1 bits 15..8)
eabx_:	ld	r3, r9
	calr	getw_
	ld	r2, r4
	ld	r3, r5
	srl	r3, $8
	and	r3, $15
	calr	getw_
	add	r4, r2
	ret

/ route EA in r4 to its bank: rr2 = far pointer (r4 preserved)
bank_:	cp	r4, r6
	jr	uge, 1f
	ld	r2, $[SPLITDSEG*256]
	ld	r3, r4
	ret
1:	ld	r2, $[TPASEG*256]
	ld	r3, r4
	ret

/ ---- dispatch table: case label per opcode byte 0x00-0x7F ----

jtab:
	.word	miss, miss, miss, miss, miss, miss, miss, miss
	.word	miss, miss, miss, miss, c0c,  c0d,  miss, miss
	.word	miss, miss, miss, miss, c14,  miss, miss, miss
	.word	miss, miss, miss, miss, miss, c1d,  miss, miss
	.word	c20,  c21,  miss, miss, miss, miss, miss, miss
	.word	miss, miss, miss, miss, miss, miss, c2e,  c2f
	.word	c30,  c31,  c32,  c33,  miss, c35,  miss, c37
	.word	miss, miss, miss, miss, miss, miss, miss, miss
	.word	miss, miss, miss, miss, miss, miss, miss, miss
	.word	miss, miss, miss, miss, c4c,  c4d,  miss, miss
	.word	miss, miss, miss, miss, c54,  miss, miss, miss
	.word	miss, miss, miss, miss, miss, c5d,  miss, miss
	.word	c60,  c61,  miss, miss, miss, miss, miss, miss
	.word	miss, miss, miss, miss, miss, miss, c6e,  c6f
	.word	c70,  c71,  c72,  c73,  miss, c75,  miss, c77
	.word	miss, miss, miss, miss, miss, miss, miss, miss
