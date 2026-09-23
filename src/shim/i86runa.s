/ Copyright (c) 2026 Kevin Dedon.
/ SPDX-License-Identifier: MIT

/ i86runa.s -- the 8086 run loop, for the target build.
/
/	int i86runa(m, in)  struct i86 *m;  struct i86in *in;
/
/ Same answers as i86run() in i86exec.c, which stays the portable
/ reference.  The common classes run here; any other class, and any
/ case a fast path does not cover, goes to i86step(), which decodes the
/ instruction again.  Nothing is changed before that choice is made.
/ IRUNT runs both on the machine and compares the whole machine state.
/
/ Held across the loop: rr10 = m, r9 = steps left.  After the decode:
/
/	r1 next ip   r2 fl,w   r3 x,mod   r4 reg,rm   r5 seg,pad
/	r6 disp      r7 imm
/
/ Every segment bias is zero here, so no reference can fault, and a word
/ at offset 0xFFFF wraps inside the host segment just as the C does it.

/ struct i86
#define M_BX	6
#define M_CX	2
#define M_SP	8
#define M_BP	10
#define M_SI	12
#define M_DI	14
#define M_SB	24
#define M_SBCS	28
#define M_SBSS	32
#define M_SO	40
#define M_IP	48
#define M_FL	50
#define M_LZ	52
#define M_LC	54
#define M_LA	56
#define M_LB	58
#define M_LR	60
#define M_FAULT	63

/ fl bit numbers: IN_MEM, IN_IMM, IN_DIR
#define B_MEM	1
#define B_IMM	6
#define B_DIR	7

/ lz, as the high byte of the lz,lw word
#define LZ_ADD	0x100
#define LZ_SUB	0x200
#define LZ_LOG	0x300
#define LZ_INC	0x400
#define LZ_DEC	0x500

/ Classes below this have an entry in jtab.
#define NHOT	47

/ Frame: the arguments i86dec takes (cs, ip, in), which it leaves alone,
/ saved r6..r13, two words, then the return address.
#define F_IP	4
#define F_IN	6
#define F_NCOLD	26
#define F_N0	28
#define A_M	34
#define A_IN	38

	.shri

	.globl	i86runa_

rb:
i86runa_:
	sub	r15, $20
	ldm	@rr14, r6, $8
	sub	r15, $10
	ldl	rr10, rr14(A_M)
	ldl	rr0, rr14(A_IN)
	ldl	rr14(F_IN), rr0
	ld	r9, i86nrun_
	ld	rr14(F_N0), r9
	clr	r0
	ld	rr14(F_NCOLD), r0
	ldb	rr10(M_FAULT), rl0
	jr	chk
loop:
	ld	r0, rr10(M_IP)
	ld	rr14(F_IP), r0
	call	i86dec_
	ldl	rr12, rr14(F_IN)
	ldm	r1, @rr12, $7
	ld	r8, r1
	and	r8, $0xff
	cp	r8, $NHOT
	jr	uge, cold
	srl	r1, $8
	ld	r0, rr14(F_IP)
	add	r1, r0
	add	r8, r8
	ld	r8, jtab(r8)
	jp	rb(r8)

cold:
	ldl	rr12, rr14(F_IN)
	pushl	@rr14, rr12
	pushl	@rr14, rr10
	call	i86step_
	add	r15, $8
	test	r1
	jr	nz, out
	ld	r0, rr14(F_NCOLD)
	inc	r0, $1
	ld	rr14(F_NCOLD), r0
	dec	r9, $1
	jr	z, 1f
/ Only i86step sets TF or a segment bias; while either holds, it runs
/ everything.
chk:
	ld	r0, rr10(M_FL)
	bit	r0, $8			/ TF
	jr	nz, cold
	ldl	rr0, rr10(M_SO)
	or	r0, r1
	ldl	rr2, rr10(M_SO+4)
	or	r0, r2
	or	r0, r3
	jr	nz, cold
	ldl	rr0, rr10(M_SBCS)
	ldl	@rr14, rr0
	jr	loop
commit:
	ld	rr10(M_IP), r1
next:
	dec	r9, $1
	jp	nz, loop
1:
	clr	r1
out:
	ld	i86nrun_, r9
	ld	r3, rr14(F_N0)		/ i86step counted its own
	sub	r3, r9
	ld	r0, rr14(F_NCOLD)
	sub	r3, r0
	clr	r2
	ldl	rr4, i86ninsn_
	addl	rr4, rr2
	ldl	i86ninsn_, rr4
	add	r15, $10
	ldm	r6, @rr14, $8
	add	r15, $20
	ret

/ ---- operands

/ r0 = the effective address.  Uses r8.
ea:
	testb	rl3
	jr	nz, 1f
	cpb	rl4, $6
	jr	ne, 1f
	ld	r0, r6
	ret
1:
eab:					/ not a direct address
	ld	r8, r4
	and	r8, $7
	add	r8, r8
	ld	r8, eatab(r8)
	jp	rb(r8)
ea0:
	ld	r0, rr10(M_BX)
	ld	r8, rr10(M_SI)
	jr	ea8
ea1:
	ld	r0, rr10(M_BX)
	ld	r8, rr10(M_DI)
	jr	ea8
ea2:
	ld	r0, rr10(M_BP)
	ld	r8, rr10(M_SI)
	jr	ea8
ea3:
	ld	r0, rr10(M_BP)
	ld	r8, rr10(M_DI)
ea8:
	add	r0, r8
	add	r0, r6
	ret
ea4:
	ld	r0, rr10(M_SI)
	add	r0, r6
	ret
ea5:
	ld	r0, rr10(M_DI)
	add	r0, r6
	ret
ea6:
	ld	r0, rr10(M_BP)
	add	r0, r6
	ret
ea7:
	ld	r0, rr10(M_BX)
	add	r0, r6
	ret

/ rr12 = the mod r/m operand, r5 = its kind: bit 0 word, bit 1 memory.
/ Called from a handler only, as it may leave for i86step.  Uses r0, r8.
rmop:
	cpb	rl3, $3
	jr	ne, 2f
	ld	r8, r4
	and	r8, $7
	add	r8, r8
	clr	r5
	ldb	rl5, rl2
	testb	rl2
	jr	nz, 1f
	ld	r8, boffw(r8)
1:
	ldl	rr12, rr10
	add	r13, r8
	ret
2:
	bitb	rh2, $B_MEM
	jr	z, 9f
	ld	r0, r6
	testb	rl3
	jr	nz, 3f
	cpb	rl4, $6
	jr	eq, 4f
3:
	calr	eab
4:
	ld	r8, r5
	srl	r8, $6
	and	r8, $0x0c
	add	r8, $M_SB		/ sb[seg]
	ldl	rr12, rr10(r8)
	add	r13, r0
	ld	r5, $2
	orb	rl5, rl2
	ret
9:
	add	r15, $4
	jp	cold

/ r0 = the operand at rr12, zero-extended.
rdrm:
	bit	r5, $0
	jr	z, 1f
	bit	r5, $1
	jr	z, 2f
	ldb	rl0, @rr12
	ldb	rh0, rr12(1)
	ret
2:
	ld	r0, @rr12
	ret
1:
	ldb	rl0, @rr12
	clrb	rh0
	ret

/ The operand at rr12 = r0.
wrrm:
	bit	r5, $0
	jr	z, 1f
	bit	r5, $1
	jr	z, 2f
	ldb	@rr12, rl0
	ldb	rr12(1), rh0
	ret
2:
	ld	@rr12, r0
	ret
1:
	ldb	@rr12, rl0
	ret

/ r8 = the offset in m of the reg-field register.
rgix:
	ld	r8, r4
	srl	r8, $7
	and	r8, $0x0e
	testb	rl2
	jr	nz, 1f
	ld	r8, boffw(r8)
1:
	ret

/ r0 = that register, zero-extended.
rdrg:
	testb	rl2
	jr	z, 1f
	ld	r0, rr10(r8)
	ret
1:
	ldb	rl0, rr10(r8)
	clrb	rh0
	ret

/ That register = r0.
wrrg:
	testb	rl2
	jr	z, 1f
	ld	rr10(r8), r0
	ret
1:
	ldb	rr10(r8), rl0
	ret

/ ---- flags

/ The lazy record: class r4, carry in rl3, a r0, b r7, result r6.
lzst:
	orb	rl4, rl2
	ld	rr10(M_LZ), r4
	ldb	rr10(M_LC), rl3
	ld	rr10(M_LA), r0
	ld	rr10(M_LB), r7
	ld	rr10(M_LR), r6
	ret

/ i86flags(m): materialise a pending record into fl.  Keeps every
/ register.  a r1, b r2, result r3, masked to lw; msb r4; flags r5.
fold:
	sub	r15, $14
	ldm	@rr14, r0, $7
	ldb	rl0, rr10(M_LZ)
	testb	rl0
	jp	z, 9f
	ldl	rr2, i86nflag_
	addl	rr2, $1
	ldl	i86nflag_, rr2
	ld	r1, rr10(M_LA)
	ld	r2, rr10(M_LB)
	ld	r3, rr10(M_LR)
	ld	r4, $0x8000
	ldb	rh0, rr10(M_LZ+1)
	testb	rh0
	jr	nz, 1f
	and	r1, $0xff
	and	r2, $0xff
	and	r3, $0xff
	ld	r4, $0x80
1:
	clr	r5
	test	r3
	jr	nz, 2f
	set	r5, $6			/ ZF
2:
	ld	r6, r3
	and	r6, r4
	jr	z, 3f
	set	r5, $7			/ SF
3:
	testb	rl3
	jr	po, 4f
	set	r5, $2			/ PF
4:
	cpb	rl0, $1
	jr	eq, fADD
	cpb	rl0, $2
	jr	eq, fSUB
	cpb	rl0, $4
	jr	eq, fINC
	cpb	rl0, $5
	jr	eq, fDEC
	jr	fdone
fADD:
	ldb	rh0, rr10(M_LC)
	testb	rh0
	jr	z, 1f
	cp	r3, r1
	jr	ugt, faf
	set	r5, $0
	jr	faf
1:
	cp	r3, r1
	jr	uge, faf
	set	r5, $0
	jr	faf
fINC:
	ld	r6, rr10(M_FL)
	and	r6, $1
	or	r5, r6
faf:
	ld	r6, r1			/ AF
	xor	r6, r2
	xor	r6, r3
	bit	r6, $4
	jr	z, 1f
	set	r5, $4
1:
	ld	r6, r1			/ OF of a sum
	xor	r6, r3
	xor	r2, r3
	and	r6, r2
	and	r6, r4
	jr	z, fdone
	set	r5, $11
	jr	fdone
fSUB:
	ldb	rh0, rr10(M_LC)
	testb	rh0
	jr	z, 1f
	cp	r1, r2
	jr	ugt, fsaf
	set	r5, $0
	jr	fsaf
1:
	cp	r1, r2
	jr	uge, fsaf
	set	r5, $0
	jr	fsaf
fDEC:
	ld	r6, rr10(M_FL)
	and	r6, $1
	or	r5, r6
fsaf:
	ld	r6, r1
	xor	r6, r2
	xor	r6, r3
	bit	r6, $4
	jr	z, 1f
	set	r5, $4
1:
	ld	r6, r1			/ OF of a difference
	xor	r6, r2
	xor	r1, r3
	and	r6, r1
	and	r6, r4
	jr	z, fdone
	set	r5, $11
fdone:
	ld	r6, rr10(M_FL)
	and	r6, $0xf72a		/ all but the six
	or	r6, r5
	ld	rr10(M_FL), r6
	clrb	rl0
	ldb	rr10(M_LZ), rl0
9:
	ldm	r0, @rr14, $7
	add	r15, $14
	ret

/ fold, unless the record is an INC or DEC, whose CF is already in fl.
ifold:
	ld	r8, rr10(M_LZ)
	and	r8, $0xff00
	cp	r8, $LZ_INC
	jr	eq, 1f
	cp	r8, $LZ_DEC
	jr	ne, fold
1:
	ret

/ ---- the classes

hALU:
	calr	rmop
	bitb	rh2, $B_IMM
	jr	z, 1f
	calr	rdrm
	jr	3f
1:
	calr	rgix
	bitb	rh2, $B_DIR
	jr	z, 2f
	calr	rdrm
	ld	r7, r0
	calr	rdrg
	jr	3f
2:
	calr	rdrg
	ld	r7, r0
	calr	rdrm
3:
	clrb	rl3
	ld	r4, r3
	srl	r4, $7
	and	r4, $0x0e
	ld	r4, alutab(r4)
	jp	rb(r4)
aADD:
	ld	r6, r0
	add	r6, r7
	ld	r4, $LZ_ADD
	jr	alz
aOR:
	ld	r6, r0
	or	r6, r7
	ld	r4, $LZ_LOG
	jr	alz
aADC:
	calr	fold
	ld	r4, rr10(M_FL)
	and	r4, $1
	ldb	rl3, rl4
	ld	r6, r0
	add	r6, r7
	add	r6, r4
	ld	r4, $LZ_ADD
	jr	alz
aSBB:
	calr	fold
	ld	r4, rr10(M_FL)
	and	r4, $1
	ldb	rl3, rl4
	ld	r6, r0
	sub	r6, r7
	sub	r6, r4
	ld	r4, $LZ_SUB
	jr	alz
aAND:
	ld	r6, r0
	and	r6, r7
	ld	r4, $LZ_LOG
	jr	alz
aXOR:
	ld	r6, r0
	xor	r6, r7
	ld	r4, $LZ_LOG
	jr	alz
aSUB:
	ld	r6, r0
	sub	r6, r7
	ld	r4, $LZ_SUB
alz:
	orb	rl4, rl2
	ld	rr10(M_LZ), r4
	ldb	rr10(M_LC), rl3
	ld	rr10(M_LA), r0
	ld	rr10(M_LB), r7
	ld	rr10(M_LR), r6
	cpb	rh3, $7			/ CMP stores nothing
	jp	eq, commit
	ld	r0, r6
	bitb	rh2, $B_DIR
	jr	nz, 4f
	calr	wrrm
	jp	commit
4:
	calr	wrrg
	jp	commit

hMOV:
	calr	rmop
	bitb	rh2, $B_IMM
	jr	z, 1f
	ld	r0, r7
	calr	wrrm
	jp	commit
1:
	calr	rgix
	bitb	rh2, $B_DIR
	jr	z, 2f
	calr	rdrm
	calr	wrrg
	jp	commit
2:
	calr	rdrg
	calr	wrrm
	jp	commit

hTEST:
	calr	rmop
	bitb	rh2, $B_IMM
	jr	nz, 1f
	calr	rgix
	calr	rdrg
	ld	r7, r0
1:
	calr	rdrm
	ld	r6, r0
	and	r6, r7
	ld	r4, $LZ_LOG
	clrb	rl3
	calr	lzst
	jp	commit

hNOT:
	calr	rmop
	calr	rdrm
	com	r0
	calr	wrrm
	jp	commit

hNEG:
	calr	rmop
	calr	rdrm
	ld	r7, r0
	clr	r0
	clr	r6
	sub	r6, r7
	ld	r4, $LZ_SUB
	clrb	rl3
	calr	lzst
	ld	r0, r6
	calr	wrrm
	jp	commit

hXCHG:
	calr	rmop
	calr	rgix
	calr	rdrg
	ld	r7, r0
	calr	rdrm
	ld	r6, r0
	ld	r0, r7
	calr	wrrm
	ld	r0, r6
	calr	wrrg
	jp	commit

hINC:
	calr	rmop
	calr	rdrm
	calr	ifold
	ld	r6, r0
	inc	r6, $1
	ld	r4, $LZ_INC
	jr	incdec

/ A register DEC that a JNZ back to it follows may be a delay loop,
/ which i86step runs in one step.
hDEC:
	cpb	rl3, $3
	jr	ne, 1f
	ldl	rr12, rr10(M_SBCS)
	add	r13, r1
	ldb	rl0, @rr12
	cpb	rl0, $0x75
	jr	ne, 1f
	inc	r13, $1
	ldb	rl0, @rr12
	ld	r7, rr10(M_IP)
	sub	r7, r1
	dec	r7, $2
	cpb	rl0, rl7
	jp	eq, cold
1:
	calr	rmop
	calr	rdrm
	calr	ifold
	ld	r6, r0
	dec	r6, $1
	ld	r4, $LZ_DEC
incdec:
	ld	r7, $1
	clrb	rl3
	calr	lzst
	ld	r0, r6
	calr	wrrm
	jp	commit

/ PUSH SP pushes the decremented SP; i86step has that.
hPUSH:
	cpb	rl3, $3
	jr	ne, 1f
	cpb	rl4, $4
	jp	eq, cold
1:
	calr	rmop
	calr	rdrm
	ld	r7, r0
	ld	r0, rr10(M_SP)
	dec	r0, $2
	ldl	rr12, rr10(M_SBSS)
	add	r13, r0
	ld	rr10(M_SP), r0
	ldb	@rr12, rl7
	ldb	rr12(1), rh7
	jp	commit

hPOP:
	calr	rmop
	ldl	rr6, rr12
	ld	r0, rr10(M_SP)
	ldl	rr12, rr10(M_SBSS)
	add	r13, r0
	ldb	rl4, @rr12
	ldb	rh4, rr12(1)
	inc	r0, $2
	ld	rr10(M_SP), r0
	ldl	rr12, rr6
	ld	r0, r4
	calr	wrrm
	jp	commit

hCALL:
	ld	r0, rr10(M_SP)
	dec	r0, $2
	ldl	rr12, rr10(M_SBSS)
	add	r13, r0
	ld	rr10(M_SP), r0
	ldb	@rr12, rl1
	ldb	rr12(1), rh1
	ld	rr10(M_IP), r6
	jp	next

hRET:
	ld	r0, rr10(M_SP)
	ldl	rr12, rr10(M_SBSS)
	add	r13, r0
	ldb	rl1, @rr12
	ldb	rh1, rr12(1)
	inc	r0, $2
	bitb	rh2, $B_IMM
	jr	z, 1f
	add	r0, r7
1:
	ld	rr10(M_SP), r0
	jp	commit

hJMP:
	ld	rr10(M_IP), r6
	jp	next

/ i86lcond(m, x): condition x>>1 into r0, nonzero for true, then x&1
/ inverts it.
hJCC:
	ld	r8, r3
	srl	r8, $8
	and	r8, $0x0e
	ldb	rl2, rr10(M_LZ)
	testb	rl2
	jr	nz, jlazy
	ld	r0, rr10(M_FL)
	ld	r8, fctab(r8)
	jp	rb(r8)
fO:
	and	r0, $0x0800
	jr	jtest
fB:
	and	r0, $0x0001
	jr	jtest
fE:
	and	r0, $0x0040
	jr	jtest
fBE:
	and	r0, $0x0041
	jr	jtest
fS:
	and	r0, $0x0080
	jr	jtest
fP:
	and	r0, $0x0004
	jr	jtest
fL:
	ld	r13, r0			/ OF down to SF
	srl	r13, $4
	xor	r0, r13
	and	r0, $0x0080
	jr	jtest
fLE:
	ld	r13, r0
	srl	r13, $4
	xor	r13, r0
	and	r13, $0x0080
	and	r0, $0x0040
	or	r0, r13
jtest:
	bitb	rh3, $0
	jr	nz, 1f
	test	r0
	jp	z, commit
	jr	jtake
1:
	test	r0
	jp	nz, commit
jtake:
	ld	rr10(M_IP), r6
	jp	next

/ The record: a r4, b r5, result r7, masked to lw; msb r12.
jlazy:
	ld	r4, rr10(M_LA)
	ld	r5, rr10(M_LB)
	ld	r7, rr10(M_LR)
	ld	r12, $0x8000
	ldb	rh2, rr10(M_LZ+1)
	testb	rh2
	jr	nz, 1f
	and	r4, $0xff
	and	r5, $0xff
	and	r7, $0xff
	ld	r12, $0x80
1:
	ld	r8, lctab(r8)
	jp	rb(r8)
lE:
	clr	r0
	test	r7
	jr	nz, jtest
	inc	r0, $1
	jr	jtest
lS:
	ld	r0, r7
	and	r0, r12
	jr	jtest
lP:
	clr	r0
	testb	rl7
	jr	po, jtest
	inc	r0, $1
	jr	jtest
/ CF into r0 and OF into r13, by the class in rl2.
lCO:
	clr	r0
	clr	r13
	cpb	rl2, $1
	jr	eq, lADD
	cpb	rl2, $2
	jr	eq, lSUB
	cpb	rl2, $4
	jr	eq, lINC
	cpb	rl2, $5
	jr	eq, lDEC
	jr	lsel
lADD:
	ldb	rh2, rr10(M_LC)
	testb	rh2
	jr	z, 1f
	cp	r7, r4
	jr	ugt, aof
	inc	r0, $1
	jr	aof
1:
	cp	r7, r4
	jr	uge, aof
	inc	r0, $1
	jr	aof
lINC:
	ld	r0, rr10(M_FL)
	and	r0, $1
aof:
	ld	r8, r4
	xor	r8, r7
	ld	r2, r5
	xor	r2, r7
	and	r8, r2
	and	r8, r12
	jr	z, lsel
	inc	r13, $1
	jr	lsel
lSUB:
	ldb	rh2, rr10(M_LC)
	testb	rh2
	jr	z, 1f
	cp	r4, r5
	jr	ugt, sof
	inc	r0, $1
	jr	sof
1:
	cp	r4, r5
	jr	uge, sof
	inc	r0, $1
	jr	sof
lDEC:
	ld	r0, rr10(M_FL)
	and	r0, $1
sof:
	ld	r8, r4
	xor	r8, r5
	ld	r2, r4
	xor	r2, r7
	and	r8, r2
	and	r8, r12
	jr	z, lsel
	inc	r13, $1
lsel:
	ld	r8, r3
	srl	r8, $8
	and	r8, $0x0e
	ld	r8, lstab(r8)
	jp	rb(r8)
sO:
	ld	r0, r13
	jp	jtest
sB:
	jp	jtest
sBE:
	test	r7
	jp	nz, jtest
	ld	r0, $1
	jp	jtest
sL:
	ld	r0, r13
	ld	r8, r7
	and	r8, r12
	jp	z, jtest
	xor	r0, $1
	jp	jtest
sLE:
	ld	r0, r13
	ld	r8, r7
	and	r8, r12
	jr	z, 1f
	xor	r0, $1
1:
	test	r7
	jp	nz, jtest
	ld	r0, $1
	jp	jtest

/ Shifts and rotates by one; by CL they go to i86step.  After the fold
/ a rotate changes only CF and OF.  A shift sets all six flags, so it
/ only counts the fold that i86flags would do.
hSHIFT:
	ldl	rr12, rr14(F_IN)
	ld	r0, rr12(14)		/ imm2
	test	r0
	jp	nz, cold
	calr	rmop
	cpb	rh3, $4
	jr	uge, 1f
	calr	fold
	jr	2f
1:
	ldb	rl0, rr10(M_LZ)
	testb	rl0
	jr	z, 2f
	ldl	rr6, i86nflag_
	addl	rr6, $1
	ldl	i86nflag_, rr6
2:
	calr	rdrm
	ld	r6, rr10(M_FL)
	ld	r7, $0x8000		/ msb
	testb	rl2
	jr	nz, 3f
	ld	r7, $0x80
3:
	ld	r2, r7			/ mask
	add	r2, r2
	dec	r2, $1
	ld	r8, r3
	srl	r8, $7
	and	r8, $0x0e
	ld	r8, shtab(r8)
	jp	rb(r8)
/ Each leaves the value in r0, CF in r4 and OF as r8 nonzero.
sROL:
	clr	r4
	ld	r8, r0
	and	r8, r7
	jr	z, 1f
	inc	r4, $1
1:
	add	r0, r0
	or	r0, r4
	and	r0, r2
	jr	tailA
sRCL:
	ld	r8, r6
	and	r8, $1
	ld	r4, r0
	and	r4, r7
	jr	z, 1f
	ld	r4, $1
1:
	add	r0, r0
	or	r0, r8
	and	r0, r2
	jr	tailA
sSHL:
	clr	r4
	ld	r8, r0
	and	r8, r7
	jr	z, 1f
	inc	r4, $1
1:
	add	r0, r0
	and	r0, r2
tailA:
	ld	r8, r0			/ OF = top bit != CF
	and	r8, r7
	jr	z, 1f
	ld	r8, $1
1:
	xor	r8, r4
	jr	sfin
sROR:
	ld	r4, r0
	and	r4, $1
	srl	r0, $1
	test	r4
	jr	z, tailB
	or	r0, r7
	jr	tailB
sRCR:
	ld	r4, r0
	and	r4, $1
	srl	r0, $1
	bit	r6, $0
	jr	z, tailB
	or	r0, r7
tailB:
	ld	r8, r0			/ OF = top two bits differ
	add	r8, r8
	xor	r8, r0
	and	r8, r7
	jr	sfin
sSHR:
	ld	r4, r0
	and	r4, $1
	ld	r8, r0
	and	r8, r7
	srl	r0, $1
	jr	sfin
sSAR:
	ld	r4, r0
	and	r4, $1
	ld	r8, r0
	and	r8, r7
	srl	r0, $1
	or	r0, r8
	clr	r8
sfin:
	and	r6, $0xf7fe		/ CF OF
	or	r6, r4
	test	r8
	jr	z, 1f
	set	r6, $11
1:
	cpb	rh3, $4
	jr	ult, 2f
	and	r6, $0xff2b		/ PF AF ZF SF
	test	r0
	jr	nz, 3f
	set	r6, $6
3:
	ld	r8, r0
	and	r8, r7
	jr	z, 4f
	set	r6, $7
4:
	testb	rl0
	jr	po, 2f
	set	r6, $2
2:
	ld	rr10(M_FL), r6
	clrb	rl4
	ldb	rr10(M_LZ), rl4
	calr	wrrm
	jp	commit

/ JCXZ and LOOP; LOOPE and LOOPNE go to i86step.
hLOOP:
	cpb	rh3, $3
	jr	ne, 1f
	ld	r0, rr10(M_CX)
	test	r0
	jp	nz, commit
	ld	rr10(M_IP), r6
	jp	next
1:
	cpb	rh3, $2
	jp	ne, cold
	ld	r0, rr10(M_CX)
	dec	r0, $1
	ld	rr10(M_CX), r0
	jp	z, commit
	ld	rr10(M_IP), r6
	jp	next

hCBW:
	ld	r0, rr10(0)
	extsb	r0
	ld	rr10(0), r0
	jp	commit

hCWD:
	ld	r5, rr10(0)
	exts	rr4
	ld	rr10(4), r4		/ DX
	jp	commit

hLAHF:
	calr	fold
	ld	r0, rr10(M_FL)
	ldb	rr10(0), rl0		/ AH
	jp	commit

hSAHF:
	calr	fold
	ld	r0, rr10(M_FL)
	ldb	rh4, rr10(0)		/ AH
	andb	rh4, $0xd5
	orb	rh4, $0x02
	ldb	rl0, rh4
	ld	rr10(M_FL), r0
	jp	commit

/ CLC STC CMC CLI STI CLD STD: x 0 clear, 1 set, 2 complement bits imm.
hFLAG:
	calr	fold
	ld	r0, rr10(M_FL)
	cpb	rh3, $1
	jr	eq, 1f
	jr	ugt, 2f
	com	r7
	and	r0, r7
	jr	3f
1:
	or	r0, r7
	jr	3f
2:
	xor	r0, r7
3:
	ld	rr10(M_FL), r0
	jp	commit

hNOP:
	jp	commit

hLEA:
	bitb	rh2, $B_MEM
	jp	z, cold
	calr	ea
	ld	r8, r4
	srl	r8, $7
	and	r8, $0x0e
	ld	rr10(r8), r0
	jp	commit

/ ---- tables

	.even
jtab:
	.word	cold-rb, hALU-rb, hMOV-rb, hJCC-rb, hJMP-rb, hSHIFT-rb
	.word	hNOT-rb, hINC-rb, hDEC-rb, hPUSH-rb, hPOP-rb, hCALL-rb
	.word	hRET-rb, hTEST-rb, hNEG-rb, hXCHG-rb, hLEA-rb, cold-rb
	.word	cold-rb, cold-rb, cold-rb, hLOOP-rb, hFLAG-rb, cold-rb
	.word	cold-rb, cold-rb, cold-rb, cold-rb, cold-rb, cold-rb
	.word	cold-rb, cold-rb, cold-rb, cold-rb, cold-rb, cold-rb
	.word	hCBW-rb, hCWD-rb, hLAHF-rb, hSAHF-rb, cold-rb, cold-rb
	.word	cold-rb, cold-rb, cold-rb, cold-rb, hNOP-rb
eatab:
	.word	ea0-rb, ea1-rb, ea2-rb, ea3-rb, ea4-rb, ea5-rb, ea6-rb, ea7-rb
alutab:
	.word	aADD-rb, aOR-rb, aADC-rb, aSBB-rb, aAND-rb, aSUB-rb, aXOR-rb
	.word	aSUB-rb
shtab:
	.word	sROL-rb, sROR-rb, sRCL-rb, sRCR-rb, sSHL-rb, sSHR-rb, sSHL-rb
	.word	sSAR-rb
/ conditions O B E BE S P L LE, from fl and from the record
fctab:
	.word	fO-rb, fB-rb, fE-rb, fBE-rb, fS-rb, fP-rb, fL-rb, fLE-rb
lctab:
	.word	lCO-rb, lCO-rb, lE-rb, lCO-rb, lS-rb, lP-rb, lCO-rb, lCO-rb
lstab:
	.word	sO-rb, sB-rb, sO-rb, sBE-rb, sO-rb, sO-rb, sL-rb, sLE-rb
/ offset in m of byte register n: AL is the low, odd, byte of AX
boffw:
	.word	1, 3, 5, 7, 0, 2, 4, 6
