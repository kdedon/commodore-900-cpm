/ Copyright (c) 2026 Kevin Dedon.
/ SPDX-License-Identifier: MIT

/ i86deca.s -- the 8086 decoder, for the target build.
/
/	int i86dec(cs, ip, in)  char *cs;  i16 ip;  struct i86in *in;
/
/ Same answers as i86dec.c, which stays the portable reference.  IDECT
/ runs both decoders on the machine and compares every field, so a fix
/ made to one and not the other fails the suite.
/
/ Each opcode has a 16-byte record that IS a struct i86in as far as the
/ opcode alone settles it.  It is loaded into r1..r8, a handler fills in
/ what the following bytes say, and one ldm stores it:
/
/	r1 len,op   r2 fl,w   r3 x,mod   r4 reg,rm   r5 seg,pad
/	r6 disp     r7 imm    r8 imm2
/
/ imm2 holds the handler, as an offset from h0; zero means nothing
/ follows the opcode.  A mod r/m opcode's disp holds what runs after the
/ mod r/m byte.  A prefix's pad byte holds the fl bit it sets, and an
/ override's segment in the top two bits.
/
/ rr12 points at the opcode byte.  Fetches and relative branch targets
/ wrap in the 16-bit offset, so cs must have offset 0, as the executor
/ ensures.

/ Values from i86.h and i86dec.c.
#define IN_IMM	0x40
#define S_SS	2
#define MAXPFX	14

/ Arguments, past the 4-byte return address and 16 bytes of saved
/ registers.
#define A_CS	20
#define A_IP	24
#define A_IN	26

	.shri

	.globl	i86dec_

h0:
i86dec_:
	sub	r15, $16
	ldm	@rr14, r6, $8
	ldl	rr12, rr14(A_CS)
	ld	r0, rr14(A_IP)
	add	r13, r0
	clr	r0
	ldb	rl0, @rr12
	ld	r9, r0			/ r0 cannot index
	sll	r9, $4
	ldm	r1, rtab(r9), $8
	test	r8
	jp	nz, h0(r8)
cdone:
	clr	r8
store:
	ldl	rr10, rr14(A_IN)
	ldm	@rr10, r1, $8
	srl	r1, $8			/ the length is the return value
	ldm	r6, @rr14, $8
	add	r15, $16
	ret

/ ---- the operand the opcode names
hIB:
	ldb	rl7, rr12(1)
	jr	cdone
hIW:
	ldb	rl7, rr12(1)
	ldb	rh7, rr12(2)
	jr	cdone
hJ8:
	ldb	rl6, rr12(1)
	extsb	r6
	add	r6, r13
	inc	r6, $2
	jr	cdone
hJW:
	ldb	rl6, rr12(1)
	ldb	rh6, rr12(2)
	add	r6, r13
	inc	r6, $3
	jr	cdone
hDA:
	ldb	rl6, rr12(1)
	ldb	rh6, rr12(2)
	jr	cdone
hFP:
	ldb	rl7, rr12(1)
	ldb	rh7, rr12(2)
	ldb	rl0, rr12(3)
	ldb	rh0, rr12(4)
	ld	r8, r0
	jr	store

/ ---- after the mod r/m byte and its displacement.  rr12 has moved past
/ ---- the displacement, so an immediate is at rr12(2).
cX1:
	ldb	rh3, rh4		/ D2, D3: the count is CL
	ld	r8, $1
	jr	store
cX:
	ldb	rh3, rh4
	jr	cdone
cSR:
	ldb	rh3, rh4
	andb	rh3, $3
	jr	cdone
cXSB:
	ldb	rh3, rh4
	ldb	rl7, rr12(2)
	extsb	r7
	jr	cdone
cXIW:
	ldb	rh3, rh4
cIW:
	ldb	rl7, rr12(2)
	ldb	rh7, rr12(3)
	jr	cdone
cXIB:
	ldb	rh3, rh4
cIB:
	ldb	rl7, rr12(2)
	jr	cdone

/ FE, FF and F6/F7 take op and x from a table indexed by reg.  F6/F7 /0
/ and /1 are TEST and carry an immediate.
cFE:
	ld	r9, r4
	srl	r9, $7
	and	r9, $0x0e		/ reg * 2
	ld	r0, fetab(r9)
	jr	grp
cFF:
	ld	r9, r4
	srl	r9, $7
	and	r9, $0x0e
	ld	r0, fftab(r9)
grp:
	ldb	rl1, rh0
	ldb	rh3, rl0
	jr	cdone
cG3:
	ld	r9, r4
	srl	r9, $7
	and	r9, $0x0e
	ld	r0, g3tab(r9)
	ldb	rl1, rh0
	ldb	rh3, rl0
	cpb	rh4, $1
	jr	ugt, cdone
	orb	rh2, $IN_IMM
	ldb	rl7, rr12(2)
	add	r1, $0x100
	testb	rl2
	jr	z, cdone
	ldb	rh7, rr12(3)
	add	r1, $0x100
	jr	cdone

/ ---- the mod r/m byte: mtab gives reg,rm and one of eight paths, which
/ ---- differ in length, mod, displacement and default segment.  The
/ ---- addl sets IN_MODRM (and IN_MEM) in fl and mod in one step.
hM:
	ldb	rl4, rr12(1)		/ reg,rm is zero in the record
	sll	r4, $2
	ldl	rr8, mtab(r4)
	ld	r4, r8
	ld	r10, r6			/ what runs next
	jp	h0(r9)
mreg:
	add	r1, $0x100
	addl	rr2, $0x01000003
	clr	r6
	jp	h0(r10)
m0ss:
	bitb	rh2, $2			/ IN_SEGOVR
	jr	nz, m0ds
	ldb	rh5, $S_SS
m0ds:
	add	r1, $0x100
	addl	rr2, $0x03000000
	clr	r6
	jp	h0(r10)
mdir:
	add	r1, $0x300
	addl	rr2, $0x03000000
	ldb	rl6, rr12(2)
	ldb	rh6, rr12(3)
	inc	r13, $2
	jp	h0(r10)
m8ss:
	bitb	rh2, $2
	jr	nz, m8ds
	ldb	rh5, $S_SS
m8ds:
	add	r1, $0x200
	addl	rr2, $0x03000001
	ldb	rl6, rr12(2)
	extsb	r6
	inc	r13, $1
	jp	h0(r10)
m16ss:
	bitb	rh2, $2
	jr	nz, m16ds
	ldb	rh5, $S_SS
m16ds:
	add	r1, $0x300
	addl	rr2, $0x03000002
	ldb	rl6, rr12(2)
	ldb	rh6, rr12(3)
	inc	r13, $2
	jp	h0(r10)

/ ---- prefixes.  Gather what they set, then decode the opcode after them
/ ---- as if unprefixed and fold it in.  At most MAXPFX are taken; the
/ ---- byte after that is the opcode even if it is a prefix, and then it
/ ---- is I_BAD, which its record already says.
hP:
	clr	r2			/ rh2: fl bits  rl2: the last override
	clr	r11			/ the count, as a length
	ld	r9, $MAXPFX
ploop:
	clr	r4
	ldb	rl4, @rr12
	sll	r4, $4
	ldb	rl3, rtab+9(r4)
	testb	rl3
	jr	z, pend
	orb	rh2, rl3
	bitb	rl3, $2
	jr	z, pnext
	ldb	rl2, rl3
pnext:
	inc	r13, $1
	add	r11, $0x100
	djnz	r9, ploop
	clr	r4
	ldb	rl4, @rr12
	sll	r4, $4
pend:
	ld	r10, r2
	ld	r9, r4
	ldm	r1, rtab(r9), $8
	ld	r0, r10
	andb	rh0, $0x3c		/ IN_SEGOVR IN_REP IN_REPNE IN_LOCK
	orb	rh2, rh0
	add	r1, r11
	bitb	rh0, $2
	jr	z, pdisp
	srlb	rl0, $6
	ldb	rh5, rl0
pdisp:
	cp	r8, $hP-h0
	jp	eq, cdone
	test	r8
	jp	nz, h0(r8)
	jp	cdone

/ ---- the tables, from tools/gen-i86dectab.c.  The op columns follow
/ ---- the I_* numbering in i86.h.
/
/ R: len, op, fl, w, x, mod, rm, pad, then what runs after the mod r/m
/ byte, imm, and the handler; reg is 0 and seg S_DS.  h0 names nothing.
/ M: reg,rm and the mod r/m path.
#define R(l,o,f,w,x,m,r,p,d,i,h) .byte l,o,f,w,x,m,0,r,3,p; .word d-h0,i,h-h0
#define M(r,t) .word r,t-h0;

	.even
rtab:
	R(1,1,0,0,0,0,0,0,cdone,0,hM)	/ 00
	R(1,1,0,1,0,0,0,0,cdone,0,hM)	/ 01
	R(1,1,128,0,0,0,0,0,cdone,0,hM)	/ 02
	R(1,1,128,1,0,0,0,0,cdone,0,hM)	/ 03
	R(2,1,64,0,0,3,0,0,h0,0,hIB)	/ 04
	R(3,1,64,1,0,3,0,0,h0,0,hIW)	/ 05
	R(1,26,0,0,0,0,0,0,h0,0,h0)	/ 06
	R(1,27,0,0,0,0,0,0,h0,0,h0)	/ 07
	R(1,1,0,0,1,0,0,0,cdone,0,hM)	/ 08
	R(1,1,0,1,1,0,0,0,cdone,0,hM)	/ 09
	R(1,1,128,0,1,0,0,0,cdone,0,hM)	/ 0A
	R(1,1,128,1,1,0,0,0,cdone,0,hM)	/ 0B
	R(2,1,64,0,1,3,0,0,h0,0,hIB)	/ 0C
	R(3,1,64,1,1,3,0,0,h0,0,hIW)	/ 0D
	R(1,26,0,0,1,0,0,0,h0,0,h0)	/ 0E
	R(1,27,0,0,1,0,0,0,h0,0,h0)	/ 0F
	R(1,1,0,0,2,0,0,0,cdone,0,hM)	/ 10
	R(1,1,0,1,2,0,0,0,cdone,0,hM)	/ 11
	R(1,1,128,0,2,0,0,0,cdone,0,hM)	/ 12
	R(1,1,128,1,2,0,0,0,cdone,0,hM)	/ 13
	R(2,1,64,0,2,3,0,0,h0,0,hIB)	/ 14
	R(3,1,64,1,2,3,0,0,h0,0,hIW)	/ 15
	R(1,26,0,0,2,0,0,0,h0,0,h0)	/ 16
	R(1,27,0,0,2,0,0,0,h0,0,h0)	/ 17
	R(1,1,0,0,3,0,0,0,cdone,0,hM)	/ 18
	R(1,1,0,1,3,0,0,0,cdone,0,hM)	/ 19
	R(1,1,128,0,3,0,0,0,cdone,0,hM)	/ 1A
	R(1,1,128,1,3,0,0,0,cdone,0,hM)	/ 1B
	R(2,1,64,0,3,3,0,0,h0,0,hIB)	/ 1C
	R(3,1,64,1,3,3,0,0,h0,0,hIW)	/ 1D
	R(1,26,0,0,3,0,0,0,h0,0,h0)	/ 1E
	R(1,27,0,0,3,0,0,0,h0,0,h0)	/ 1F
	R(1,1,0,0,4,0,0,0,cdone,0,hM)	/ 20
	R(1,1,0,1,4,0,0,0,cdone,0,hM)	/ 21
	R(1,1,128,0,4,0,0,0,cdone,0,hM)	/ 22
	R(1,1,128,1,4,0,0,0,cdone,0,hM)	/ 23
	R(2,1,64,0,4,3,0,0,h0,0,hIB)	/ 24
	R(3,1,64,1,4,3,0,0,h0,0,hIW)	/ 25
	R(1,0,0,0,0,0,0,4,h0,0,hP)	/ 26
	R(1,40,0,0,0,0,0,0,h0,0,h0)	/ 27
	R(1,1,0,0,5,0,0,0,cdone,0,hM)	/ 28
	R(1,1,0,1,5,0,0,0,cdone,0,hM)	/ 29
	R(1,1,128,0,5,0,0,0,cdone,0,hM)	/ 2A
	R(1,1,128,1,5,0,0,0,cdone,0,hM)	/ 2B
	R(2,1,64,0,5,3,0,0,h0,0,hIB)	/ 2C
	R(3,1,64,1,5,3,0,0,h0,0,hIW)	/ 2D
	R(1,0,0,0,0,0,0,68,h0,0,hP)	/ 2E
	R(1,41,0,0,0,0,0,0,h0,0,h0)	/ 2F
	R(1,1,0,0,6,0,0,0,cdone,0,hM)	/ 30
	R(1,1,0,1,6,0,0,0,cdone,0,hM)	/ 31
	R(1,1,128,0,6,0,0,0,cdone,0,hM)	/ 32
	R(1,1,128,1,6,0,0,0,cdone,0,hM)	/ 33
	R(2,1,64,0,6,3,0,0,h0,0,hIB)	/ 34
	R(3,1,64,1,6,3,0,0,h0,0,hIW)	/ 35
	R(1,0,0,0,0,0,0,132,h0,0,hP)	/ 36
	R(1,42,0,0,0,0,0,0,h0,0,h0)	/ 37
	R(1,1,0,0,7,0,0,0,cdone,0,hM)	/ 38
	R(1,1,0,1,7,0,0,0,cdone,0,hM)	/ 39
	R(1,1,128,0,7,0,0,0,cdone,0,hM)	/ 3A
	R(1,1,128,1,7,0,0,0,cdone,0,hM)	/ 3B
	R(2,1,64,0,7,3,0,0,h0,0,hIB)	/ 3C
	R(3,1,64,1,7,3,0,0,h0,0,hIW)	/ 3D
	R(1,0,0,0,0,0,0,196,h0,0,hP)	/ 3E
	R(1,43,0,0,0,0,0,0,h0,0,h0)	/ 3F
	R(1,7,0,1,0,3,0,0,h0,0,h0)	/ 40
	R(1,7,0,1,0,3,1,0,h0,0,h0)	/ 41
	R(1,7,0,1,0,3,2,0,h0,0,h0)	/ 42
	R(1,7,0,1,0,3,3,0,h0,0,h0)	/ 43
	R(1,7,0,1,0,3,4,0,h0,0,h0)	/ 44
	R(1,7,0,1,0,3,5,0,h0,0,h0)	/ 45
	R(1,7,0,1,0,3,6,0,h0,0,h0)	/ 46
	R(1,7,0,1,0,3,7,0,h0,0,h0)	/ 47
	R(1,8,0,1,0,3,0,0,h0,0,h0)	/ 48
	R(1,8,0,1,0,3,1,0,h0,0,h0)	/ 49
	R(1,8,0,1,0,3,2,0,h0,0,h0)	/ 4A
	R(1,8,0,1,0,3,3,0,h0,0,h0)	/ 4B
	R(1,8,0,1,0,3,4,0,h0,0,h0)	/ 4C
	R(1,8,0,1,0,3,5,0,h0,0,h0)	/ 4D
	R(1,8,0,1,0,3,6,0,h0,0,h0)	/ 4E
	R(1,8,0,1,0,3,7,0,h0,0,h0)	/ 4F
	R(1,9,0,1,0,3,0,0,h0,0,h0)	/ 50
	R(1,9,0,1,0,3,1,0,h0,0,h0)	/ 51
	R(1,9,0,1,0,3,2,0,h0,0,h0)	/ 52
	R(1,9,0,1,0,3,3,0,h0,0,h0)	/ 53
	R(1,9,0,1,0,3,4,0,h0,0,h0)	/ 54
	R(1,9,0,1,0,3,5,0,h0,0,h0)	/ 55
	R(1,9,0,1,0,3,6,0,h0,0,h0)	/ 56
	R(1,9,0,1,0,3,7,0,h0,0,h0)	/ 57
	R(1,10,0,1,0,3,0,0,h0,0,h0)	/ 58
	R(1,10,0,1,0,3,1,0,h0,0,h0)	/ 59
	R(1,10,0,1,0,3,2,0,h0,0,h0)	/ 5A
	R(1,10,0,1,0,3,3,0,h0,0,h0)	/ 5B
	R(1,10,0,1,0,3,4,0,h0,0,h0)	/ 5C
	R(1,10,0,1,0,3,5,0,h0,0,h0)	/ 5D
	R(1,10,0,1,0,3,6,0,h0,0,h0)	/ 5E
	R(1,10,0,1,0,3,7,0,h0,0,h0)	/ 5F
	R(2,3,0,0,0,0,0,0,h0,0,hJ8)	/ 60
	R(2,3,0,0,1,0,0,0,h0,0,hJ8)	/ 61
	R(2,3,0,0,2,0,0,0,h0,0,hJ8)	/ 62
	R(2,3,0,0,3,0,0,0,h0,0,hJ8)	/ 63
	R(2,3,0,0,4,0,0,0,h0,0,hJ8)	/ 64
	R(2,3,0,0,5,0,0,0,h0,0,hJ8)	/ 65
	R(2,3,0,0,6,0,0,0,h0,0,hJ8)	/ 66
	R(2,3,0,0,7,0,0,0,h0,0,hJ8)	/ 67
	R(2,3,0,0,8,0,0,0,h0,0,hJ8)	/ 68
	R(2,3,0,0,9,0,0,0,h0,0,hJ8)	/ 69
	R(2,3,0,0,10,0,0,0,h0,0,hJ8)	/ 6A
	R(2,3,0,0,11,0,0,0,h0,0,hJ8)	/ 6B
	R(2,3,0,0,12,0,0,0,h0,0,hJ8)	/ 6C
	R(2,3,0,0,13,0,0,0,h0,0,hJ8)	/ 6D
	R(2,3,0,0,14,0,0,0,h0,0,hJ8)	/ 6E
	R(2,3,0,0,15,0,0,0,h0,0,hJ8)	/ 6F
	R(2,3,0,0,0,0,0,0,h0,0,hJ8)	/ 70
	R(2,3,0,0,1,0,0,0,h0,0,hJ8)	/ 71
	R(2,3,0,0,2,0,0,0,h0,0,hJ8)	/ 72
	R(2,3,0,0,3,0,0,0,h0,0,hJ8)	/ 73
	R(2,3,0,0,4,0,0,0,h0,0,hJ8)	/ 74
	R(2,3,0,0,5,0,0,0,h0,0,hJ8)	/ 75
	R(2,3,0,0,6,0,0,0,h0,0,hJ8)	/ 76
	R(2,3,0,0,7,0,0,0,h0,0,hJ8)	/ 77
	R(2,3,0,0,8,0,0,0,h0,0,hJ8)	/ 78
	R(2,3,0,0,9,0,0,0,h0,0,hJ8)	/ 79
	R(2,3,0,0,10,0,0,0,h0,0,hJ8)	/ 7A
	R(2,3,0,0,11,0,0,0,h0,0,hJ8)	/ 7B
	R(2,3,0,0,12,0,0,0,h0,0,hJ8)	/ 7C
	R(2,3,0,0,13,0,0,0,h0,0,hJ8)	/ 7D
	R(2,3,0,0,14,0,0,0,h0,0,hJ8)	/ 7E
	R(2,3,0,0,15,0,0,0,h0,0,hJ8)	/ 7F
	R(2,1,64,0,0,0,0,0,cXIB,0,hM)	/ 80
	R(3,1,64,1,0,0,0,0,cXIW,0,hM)	/ 81
	R(2,1,64,0,0,0,0,0,cXIB,0,hM)	/ 82
	R(2,1,64,1,0,0,0,0,cXSB,0,hM)	/ 83
	R(1,13,0,0,0,0,0,0,cdone,0,hM)	/ 84
	R(1,13,0,1,0,0,0,0,cdone,0,hM)	/ 85
	R(1,15,0,0,0,0,0,0,cdone,0,hM)	/ 86
	R(1,15,0,1,0,0,0,0,cdone,0,hM)	/ 87
	R(1,2,0,0,0,0,0,0,cdone,0,hM)	/ 88
	R(1,2,0,1,0,0,0,0,cdone,0,hM)	/ 89
	R(1,2,128,0,0,0,0,0,cdone,0,hM)	/ 8A
	R(1,2,128,1,0,0,0,0,cdone,0,hM)	/ 8B
	R(1,17,0,1,0,0,0,0,cSR,0,hM)	/ 8C
	R(1,16,0,1,0,0,0,0,cdone,0,hM)	/ 8D
	R(1,17,128,1,0,0,0,0,cSR,0,hM)	/ 8E
	R(1,10,0,1,0,0,0,0,cdone,0,hM)	/ 8F
	R(1,46,0,0,0,0,0,0,h0,0,h0)	/ 90
	R(1,15,0,1,0,3,1,0,h0,0,h0)	/ 91
	R(1,15,0,1,0,3,2,0,h0,0,h0)	/ 92
	R(1,15,0,1,0,3,3,0,h0,0,h0)	/ 93
	R(1,15,0,1,0,3,4,0,h0,0,h0)	/ 94
	R(1,15,0,1,0,3,5,0,h0,0,h0)	/ 95
	R(1,15,0,1,0,3,6,0,h0,0,h0)	/ 96
	R(1,15,0,1,0,3,7,0,h0,0,h0)	/ 97
	R(1,36,0,0,0,0,0,0,h0,0,h0)	/ 98
	R(1,37,0,0,0,0,0,0,h0,0,h0)	/ 99
	R(5,30,0,0,0,0,0,0,h0,0,hFP)	/ 9A
	R(1,48,0,0,0,0,0,0,h0,0,h0)	/ 9B
	R(1,28,0,0,0,0,0,0,h0,0,h0)	/ 9C
	R(1,29,0,0,0,0,0,0,h0,0,h0)	/ 9D
	R(1,39,0,0,0,0,0,0,h0,0,h0)	/ 9E
	R(1,38,0,0,0,0,0,0,h0,0,h0)	/ 9F
	R(3,2,131,0,0,0,6,0,h0,0,hDA)	/ A0
	R(3,2,131,1,0,0,6,0,h0,0,hDA)	/ A1
	R(3,2,3,0,0,0,6,0,h0,0,hDA)	/ A2
	R(3,2,3,1,0,0,6,0,h0,0,hDA)	/ A3
	R(1,20,0,0,0,0,0,0,h0,0,h0)	/ A4
	R(1,20,0,1,0,0,0,0,h0,0,h0)	/ A5
	R(1,20,0,0,1,0,0,0,h0,0,h0)	/ A6
	R(1,20,0,1,1,0,0,0,h0,0,h0)	/ A7
	R(2,13,64,0,0,3,0,0,h0,0,hIB)	/ A8
	R(3,13,64,1,0,3,0,0,h0,0,hIW)	/ A9
	R(1,20,0,0,2,0,0,0,h0,0,h0)	/ AA
	R(1,20,0,1,2,0,0,0,h0,0,h0)	/ AB
	R(1,20,0,0,3,0,0,0,h0,0,h0)	/ AC
	R(1,20,0,1,3,0,0,0,h0,0,h0)	/ AD
	R(1,20,0,0,4,0,0,0,h0,0,h0)	/ AE
	R(1,20,0,1,4,0,0,0,h0,0,h0)	/ AF
	R(2,2,64,0,0,3,0,0,h0,0,hIB)	/ B0
	R(2,2,64,0,0,3,1,0,h0,0,hIB)	/ B1
	R(2,2,64,0,0,3,2,0,h0,0,hIB)	/ B2
	R(2,2,64,0,0,3,3,0,h0,0,hIB)	/ B3
	R(2,2,64,0,0,3,4,0,h0,0,hIB)	/ B4
	R(2,2,64,0,0,3,5,0,h0,0,hIB)	/ B5
	R(2,2,64,0,0,3,6,0,h0,0,hIB)	/ B6
	R(2,2,64,0,0,3,7,0,h0,0,hIB)	/ B7
	R(3,2,64,1,0,3,0,0,h0,0,hIW)	/ B8
	R(3,2,64,1,0,3,1,0,h0,0,hIW)	/ B9
	R(3,2,64,1,0,3,2,0,h0,0,hIW)	/ BA
	R(3,2,64,1,0,3,3,0,h0,0,hIW)	/ BB
	R(3,2,64,1,0,3,4,0,h0,0,hIW)	/ BC
	R(3,2,64,1,0,3,5,0,h0,0,hIW)	/ BD
	R(3,2,64,1,0,3,6,0,h0,0,hIW)	/ BE
	R(3,2,64,1,0,3,7,0,h0,0,hIW)	/ BF
	R(3,12,64,0,0,0,0,0,h0,0,hIW)	/ C0
	R(1,12,0,0,0,0,0,0,h0,0,h0)	/ C1
	R(3,12,64,0,0,0,0,0,h0,0,hIW)	/ C2
	R(1,12,0,0,0,0,0,0,h0,0,h0)	/ C3
	R(1,18,0,1,0,0,0,0,cdone,0,hM)	/ C4
	R(1,18,0,1,3,0,0,0,cdone,0,hM)	/ C5
	R(2,2,64,0,0,0,0,0,cIB,0,hM)	/ C6
	R(3,2,64,1,0,0,0,0,cIW,0,hM)	/ C7
	R(3,32,64,0,0,0,0,0,h0,0,hIW)	/ C8
	R(1,32,0,0,0,0,0,0,h0,0,h0)	/ C9
	R(3,32,64,0,0,0,0,0,h0,0,hIW)	/ CA
	R(1,32,0,0,0,0,0,0,h0,0,h0)	/ CB
	R(1,23,0,0,0,0,0,0,h0,3,h0)	/ CC
	R(2,23,0,0,0,0,0,0,h0,0,hIB)	/ CD
	R(1,34,0,0,0,0,0,0,h0,0,h0)	/ CE
	R(1,33,0,0,0,0,0,0,h0,0,h0)	/ CF
	R(1,5,0,0,0,0,0,0,cX,0,hM)	/ D0
	R(1,5,0,1,0,0,0,0,cX,0,hM)	/ D1
	R(1,5,0,0,0,0,0,0,cX1,0,hM)	/ D2
	R(1,5,0,1,0,0,0,0,cX1,0,hM)	/ D3
	R(2,44,0,0,0,0,0,0,h0,0,hIB)	/ D4
	R(2,45,0,0,0,0,0,0,h0,0,hIB)	/ D5
	R(1,0,0,0,0,0,0,0,h0,0,h0)	/ D6
	R(1,35,0,0,0,0,0,0,h0,0,h0)	/ D7
	R(1,49,0,0,0,0,0,0,cdone,0,hM)	/ D8
	R(1,49,0,0,0,0,0,0,cdone,0,hM)	/ D9
	R(1,49,0,0,0,0,0,0,cdone,0,hM)	/ DA
	R(1,49,0,0,0,0,0,0,cdone,0,hM)	/ DB
	R(1,49,0,0,0,0,0,0,cdone,0,hM)	/ DC
	R(1,49,0,0,0,0,0,0,cdone,0,hM)	/ DD
	R(1,49,0,0,0,0,0,0,cdone,0,hM)	/ DE
	R(1,49,0,0,0,0,0,0,cdone,0,hM)	/ DF
	R(2,21,0,0,0,0,0,0,h0,0,hJ8)	/ E0
	R(2,21,0,0,1,0,0,0,h0,0,hJ8)	/ E1
	R(2,21,0,0,2,0,0,0,h0,0,hJ8)	/ E2
	R(2,21,0,0,3,0,0,0,h0,0,hJ8)	/ E3
	R(2,50,0,0,0,0,0,0,h0,0,hIB)	/ E4
	R(2,50,0,1,0,0,0,0,h0,0,hIB)	/ E5
	R(2,50,0,0,1,0,0,0,h0,0,hIB)	/ E6
	R(2,50,0,1,1,0,0,0,h0,0,hIB)	/ E7
	R(3,11,0,0,0,0,0,0,h0,0,hJW)	/ E8
	R(3,4,0,0,0,0,0,0,h0,0,hJW)	/ E9
	R(5,31,0,0,0,0,0,0,h0,0,hFP)	/ EA
	R(2,4,0,0,0,0,0,0,h0,0,hJ8)	/ EB
	R(1,50,0,0,2,0,0,0,h0,0,h0)	/ EC
	R(1,50,0,1,2,0,0,0,h0,0,h0)	/ ED
	R(1,50,0,0,3,0,0,0,h0,0,h0)	/ EE
	R(1,50,0,1,3,0,0,0,h0,0,h0)	/ EF
	R(1,0,0,0,0,0,0,32,h0,0,hP)	/ F0
	R(1,0,0,0,0,0,0,32,h0,0,hP)	/ F1
	R(1,0,0,0,0,0,0,16,h0,0,hP)	/ F2
	R(1,0,0,0,0,0,0,8,h0,0,hP)	/ F3
	R(1,47,0,0,0,0,0,0,h0,0,h0)	/ F4
	R(1,22,0,0,2,0,0,0,h0,1,h0)	/ F5
	R(1,0,0,0,0,0,0,0,cG3,0,hM)	/ F6
	R(1,0,0,1,0,0,0,0,cG3,0,hM)	/ F7
	R(1,22,0,0,0,0,0,0,h0,1,h0)	/ F8
	R(1,22,0,0,1,0,0,0,h0,1,h0)	/ F9
	R(1,22,0,0,0,0,0,0,h0,512,h0)	/ FA
	R(1,22,0,0,1,0,0,0,h0,512,h0)	/ FB
	R(1,22,0,0,0,0,0,0,h0,1024,h0)	/ FC
	R(1,22,0,0,1,0,0,0,h0,1024,h0)	/ FD
	R(1,0,0,0,0,0,0,0,cFE,0,hM)	/ FE
	R(1,0,0,1,0,0,0,0,cFF,0,hM)	/ FF
mtab:
	M(0x0000,m0ds)	M(0x0001,m0ds)	M(0x0002,m0ss)	M(0x0003,m0ss)	/ 00
	M(0x0004,m0ds)	M(0x0005,m0ds)	M(0x0006,mdir)	M(0x0007,m0ds)	/ 04
	M(0x0100,m0ds)	M(0x0101,m0ds)	M(0x0102,m0ss)	M(0x0103,m0ss)	/ 08
	M(0x0104,m0ds)	M(0x0105,m0ds)	M(0x0106,mdir)	M(0x0107,m0ds)	/ 0C
	M(0x0200,m0ds)	M(0x0201,m0ds)	M(0x0202,m0ss)	M(0x0203,m0ss)	/ 10
	M(0x0204,m0ds)	M(0x0205,m0ds)	M(0x0206,mdir)	M(0x0207,m0ds)	/ 14
	M(0x0300,m0ds)	M(0x0301,m0ds)	M(0x0302,m0ss)	M(0x0303,m0ss)	/ 18
	M(0x0304,m0ds)	M(0x0305,m0ds)	M(0x0306,mdir)	M(0x0307,m0ds)	/ 1C
	M(0x0400,m0ds)	M(0x0401,m0ds)	M(0x0402,m0ss)	M(0x0403,m0ss)	/ 20
	M(0x0404,m0ds)	M(0x0405,m0ds)	M(0x0406,mdir)	M(0x0407,m0ds)	/ 24
	M(0x0500,m0ds)	M(0x0501,m0ds)	M(0x0502,m0ss)	M(0x0503,m0ss)	/ 28
	M(0x0504,m0ds)	M(0x0505,m0ds)	M(0x0506,mdir)	M(0x0507,m0ds)	/ 2C
	M(0x0600,m0ds)	M(0x0601,m0ds)	M(0x0602,m0ss)	M(0x0603,m0ss)	/ 30
	M(0x0604,m0ds)	M(0x0605,m0ds)	M(0x0606,mdir)	M(0x0607,m0ds)	/ 34
	M(0x0700,m0ds)	M(0x0701,m0ds)	M(0x0702,m0ss)	M(0x0703,m0ss)	/ 38
	M(0x0704,m0ds)	M(0x0705,m0ds)	M(0x0706,mdir)	M(0x0707,m0ds)	/ 3C
	M(0x0000,m8ds)	M(0x0001,m8ds)	M(0x0002,m8ss)	M(0x0003,m8ss)	/ 40
	M(0x0004,m8ds)	M(0x0005,m8ds)	M(0x0006,m8ss)	M(0x0007,m8ds)	/ 44
	M(0x0100,m8ds)	M(0x0101,m8ds)	M(0x0102,m8ss)	M(0x0103,m8ss)	/ 48
	M(0x0104,m8ds)	M(0x0105,m8ds)	M(0x0106,m8ss)	M(0x0107,m8ds)	/ 4C
	M(0x0200,m8ds)	M(0x0201,m8ds)	M(0x0202,m8ss)	M(0x0203,m8ss)	/ 50
	M(0x0204,m8ds)	M(0x0205,m8ds)	M(0x0206,m8ss)	M(0x0207,m8ds)	/ 54
	M(0x0300,m8ds)	M(0x0301,m8ds)	M(0x0302,m8ss)	M(0x0303,m8ss)	/ 58
	M(0x0304,m8ds)	M(0x0305,m8ds)	M(0x0306,m8ss)	M(0x0307,m8ds)	/ 5C
	M(0x0400,m8ds)	M(0x0401,m8ds)	M(0x0402,m8ss)	M(0x0403,m8ss)	/ 60
	M(0x0404,m8ds)	M(0x0405,m8ds)	M(0x0406,m8ss)	M(0x0407,m8ds)	/ 64
	M(0x0500,m8ds)	M(0x0501,m8ds)	M(0x0502,m8ss)	M(0x0503,m8ss)	/ 68
	M(0x0504,m8ds)	M(0x0505,m8ds)	M(0x0506,m8ss)	M(0x0507,m8ds)	/ 6C
	M(0x0600,m8ds)	M(0x0601,m8ds)	M(0x0602,m8ss)	M(0x0603,m8ss)	/ 70
	M(0x0604,m8ds)	M(0x0605,m8ds)	M(0x0606,m8ss)	M(0x0607,m8ds)	/ 74
	M(0x0700,m8ds)	M(0x0701,m8ds)	M(0x0702,m8ss)	M(0x0703,m8ss)	/ 78
	M(0x0704,m8ds)	M(0x0705,m8ds)	M(0x0706,m8ss)	M(0x0707,m8ds)	/ 7C
	M(0x0000,m16ds)	M(0x0001,m16ds)	M(0x0002,m16ss)	M(0x0003,m16ss)	/ 80
	M(0x0004,m16ds)	M(0x0005,m16ds)	M(0x0006,m16ss)	M(0x0007,m16ds)	/ 84
	M(0x0100,m16ds)	M(0x0101,m16ds)	M(0x0102,m16ss)	M(0x0103,m16ss)	/ 88
	M(0x0104,m16ds)	M(0x0105,m16ds)	M(0x0106,m16ss)	M(0x0107,m16ds)	/ 8C
	M(0x0200,m16ds)	M(0x0201,m16ds)	M(0x0202,m16ss)	M(0x0203,m16ss)	/ 90
	M(0x0204,m16ds)	M(0x0205,m16ds)	M(0x0206,m16ss)	M(0x0207,m16ds)	/ 94
	M(0x0300,m16ds)	M(0x0301,m16ds)	M(0x0302,m16ss)	M(0x0303,m16ss)	/ 98
	M(0x0304,m16ds)	M(0x0305,m16ds)	M(0x0306,m16ss)	M(0x0307,m16ds)	/ 9C
	M(0x0400,m16ds)	M(0x0401,m16ds)	M(0x0402,m16ss)	M(0x0403,m16ss)	/ A0
	M(0x0404,m16ds)	M(0x0405,m16ds)	M(0x0406,m16ss)	M(0x0407,m16ds)	/ A4
	M(0x0500,m16ds)	M(0x0501,m16ds)	M(0x0502,m16ss)	M(0x0503,m16ss)	/ A8
	M(0x0504,m16ds)	M(0x0505,m16ds)	M(0x0506,m16ss)	M(0x0507,m16ds)	/ AC
	M(0x0600,m16ds)	M(0x0601,m16ds)	M(0x0602,m16ss)	M(0x0603,m16ss)	/ B0
	M(0x0604,m16ds)	M(0x0605,m16ds)	M(0x0606,m16ss)	M(0x0607,m16ds)	/ B4
	M(0x0700,m16ds)	M(0x0701,m16ds)	M(0x0702,m16ss)	M(0x0703,m16ss)	/ B8
	M(0x0704,m16ds)	M(0x0705,m16ds)	M(0x0706,m16ss)	M(0x0707,m16ds)	/ BC
	M(0x0000,mreg)	M(0x0001,mreg)	M(0x0002,mreg)	M(0x0003,mreg)	/ C0
	M(0x0004,mreg)	M(0x0005,mreg)	M(0x0006,mreg)	M(0x0007,mreg)	/ C4
	M(0x0100,mreg)	M(0x0101,mreg)	M(0x0102,mreg)	M(0x0103,mreg)	/ C8
	M(0x0104,mreg)	M(0x0105,mreg)	M(0x0106,mreg)	M(0x0107,mreg)	/ CC
	M(0x0200,mreg)	M(0x0201,mreg)	M(0x0202,mreg)	M(0x0203,mreg)	/ D0
	M(0x0204,mreg)	M(0x0205,mreg)	M(0x0206,mreg)	M(0x0207,mreg)	/ D4
	M(0x0300,mreg)	M(0x0301,mreg)	M(0x0302,mreg)	M(0x0303,mreg)	/ D8
	M(0x0304,mreg)	M(0x0305,mreg)	M(0x0306,mreg)	M(0x0307,mreg)	/ DC
	M(0x0400,mreg)	M(0x0401,mreg)	M(0x0402,mreg)	M(0x0403,mreg)	/ E0
	M(0x0404,mreg)	M(0x0405,mreg)	M(0x0406,mreg)	M(0x0407,mreg)	/ E4
	M(0x0500,mreg)	M(0x0501,mreg)	M(0x0502,mreg)	M(0x0503,mreg)	/ E8
	M(0x0504,mreg)	M(0x0505,mreg)	M(0x0506,mreg)	M(0x0507,mreg)	/ EC
	M(0x0600,mreg)	M(0x0601,mreg)	M(0x0602,mreg)	M(0x0603,mreg)	/ F0
	M(0x0604,mreg)	M(0x0605,mreg)	M(0x0606,mreg)	M(0x0607,mreg)	/ F4
	M(0x0700,mreg)	M(0x0701,mreg)	M(0x0702,mreg)	M(0x0703,mreg)	/ F8
	M(0x0704,mreg)	M(0x0705,mreg)	M(0x0706,mreg)	M(0x0707,mreg)	/ FC
fetab:	.byte	7,0, 8,0, 0,0, 0,0, 0,0, 0,0, 0,0, 0,0
fftab:	.byte	7,0, 8,0, 24,0, 24,1, 25,0, 25,1, 9,0, 0,0
g3tab:	.byte	13,0, 13,0, 6,0, 14,0, 19,4, 19,5, 19,6, 19,7
