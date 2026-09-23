/ Copyright (c) 2026 Kevin Dedon.
/ SPDX-License-Identifier: MIT

/ z80runa.s -- the run loop, for the target build.
/
/	int z80run(m, n, k)  struct z80 *m;  long n, *k;
/
/ Same answers as z80run.c over z80step(), which stay the portable
/ reference; ZRUNT runs both from the same states and compares them.
/
/ The machine pointer, guest memory, PC and the count left live in
/ registers.  A table indexed by opcode jumps to its class's handler,
/ which reads the operand fields from the decoder's base map.  The
/ frequent classes run here; the rest, and every prefixed opcode, are
/ decoded by z80dec() and run by z80exec() in C.  Registers across the
/ loop:
/
/	rr12	m			rr10	guest memory
/	r9	guest PC		rr6	instructions left
/	r1	the opcode times 2 on entry to a handler; times 16 is
/		its record in btab
/
/ B C D E H L are bytes 0-5 of the pair file, so an r field below 6 is
/ its own offset into the machine.

/ struct z80 offsets
#define M_B	0
#define M_DE	2
#define M_HL	4
#define M_SP	6
#define M_A	8
#define M_F	9
#define M_PC	10
#define M_LZ	25
#define M_LC	26
#define M_LA	28
#define M_LB	30
#define M_LR	32
#define M_MEM	36

/ base-map record fields
#define B_FL	btab+2
#define B_X	btab+3
#define B_Y	btab+4

#define LZ_SUB	2
#define LZ_LOG	4
#define LZ_INR	5
#define LZ_DCR	6
#define LZ_LDBLK 9
#define LZ_CPBLK 10
#define F_CY	0x01
#define F_AC	0x10
#define X_BAD	2

/ Lazy class in the high byte, carry in in the low.
#define LZADD	0x0100
#define LZSUB	0x0200
#define LZAND	0x0300
#define LZLOG	0x0400
#define LZINR	0x0500
#define LZDCR	0x0600

/ The frame: r6-r13, a struct z80in, then the return address and args.
#define F_IN	16
#define F_SIZE	28
#define A_M	32
#define A_N	36
#define A_K	40

/ Count one instruction and run the next.
#define NEXT	subl rr6, $1; jp nz, top; jp limit

/ r3 = the word after the opcode, r9 = PC past it.  FETCH2 stops short of
/ the last increment, for a jump that replaces r9 anyway.
#define FETCH2	inc r9, $1; ldb rl3, rr10(r9); inc r9, $1; ldb rh3, rr10(r9)
#define FETCHW	FETCH2; inc r9, $1

/ Push r0, pop into r0; both use r2.
#define PUSH	ld r2, rr12(M_SP); dec r2, $1; ldb rr10(r2), rh0; dec r2, $1; ldb rr10(r2), rl0; ld rr12(M_SP), r2
#define POP	ld r2, rr12(M_SP); ldb rl0, rr10(r2); inc r2, $1; ldb rh0, rr10(r2); inc r2, $1; ld rr12(M_SP), r2

	.shri

	.globl	z80run_
	.globl	btab
	.globl	z80dec_
	.globl	z80exec_
	.globl	z80flags_
	.globl	z80delay_
	.globl	z80fast_
	.globl	z80ninsn_
	.globl	z80nflag_

z80run_:
	sub	r15, $F_SIZE
	ldm	(rr14), r6, $8		/ r6..r13 are callee-saved
	ldl	rr12, rr14(A_M)
	ldl	rr10, rr12(M_MEM)
	ld	r9, rr12(M_PC)
	ldl	rr6, rr14(A_N)
	testl	rr6
	jp	z, limit
	jp	mi, limit
top:
	ldb	rl1, rr10(r9)
	clrb	rh1
	add	r1, r1
	ld	r2, otab(r1)
	jp	h0(r2)

/ ---- leaving: *k gets the X_OK count, z80ninsn that plus r0 (1 when the
/ ---- instruction that stopped the loop was decoded), r1 is the answer
limit:
	clr	r1
	clr	r0
out:
	ld	rr12(M_PC), r9
	ldl	rr2, rr14(A_N)
	subl	rr2, rr6
	ldl	rr4, rr14(A_K)
	ldl	rr6, (rr4)
	addl	rr6, rr2
	ldl	(rr4), rr6
	test	r0
	jr	z, 1f
	addl	rr2, $1
1:
	ldl	rr4, z80ninsn_
	addl	rr4, rr2
	ldl	z80ninsn_, rr4
	ldm	r6, (rr14), $8
	add	r15, $F_SIZE
	ret

/ ---- helpers

/ Materialise the pending flags by z80flags()' rules, which the four
/ prefix-group classes go to.  Keeps r1, as the handlers need it.  From
/ the record's low bytes: rl2 = r, rh4 = a, rl4 = b; rl3 collects F.
fold:
	ldb	rl0, rr12(M_LZ)
	testb	rl0
	jr	nz, 1f
	ret
1:
	cpb	rl0, $LZ_DCR
	jr	ugt, fslow
	ldl	rr2, z80nflag_
	addl	rr2, $1
	ldl	z80nflag_, rr2
	clr	r2
	ldb	rl2, rr12(M_LR+1)
	ldb	rl3, sztab(r2)
	ldb	rh4, rr12(M_LA+1)
	ldb	rl4, rr12(M_LB+1)
	cpb	rl0, $LZ_SUB
	jr	eq, fsub
	jr	ugt, fand
	ldb	rl5, rr12(M_LC)		/ ADD: lc ? r <= a : r < a
	testb	rl5
	jr	nz, 1f
	cpb	rl2, rh4
	jr	uge, fach
	orb	rl3, $F_CY
	jr	fach
1:
	cpb	rl2, rh4
	jr	ugt, fach
	orb	rl3, $F_CY
	jr	fach
fsub:
	ldb	rl5, rr12(M_LC)		/ lc ? a <= b : a < b
	testb	rl5
	jr	nz, 1f
	cpb	rh4, rl4
	jr	uge, facl
	orb	rl3, $F_CY
	jr	facl
1:
	cpb	rh4, rl4
	jr	ugt, facl
	orb	rl3, $F_CY
	jr	facl
fand:
	cpb	rl0, $LZ_LOG
	jr	eq, fdone
	jr	ugt, finc
	ldb	rl5, rh4		/ ANA: AC from bit 3 of a | b
	orb	rl5, rl4
	bitb	rl5, $3
	jr	z, fdone
	orb	rl3, $F_AC
	jr	fdone
finc:
	ldb	rl5, rr12(M_F)		/ INR and DCR keep CY
	andb	rl5, $F_CY
	orb	rl3, rl5
	cpb	rl0, $LZ_INR
	jr	ne, facl
fach:
	ldb	rl5, rh4		/ AC: the carry out of bit 3
	xorb	rl5, rl4
	xorb	rl5, rl2
	andb	rl5, $F_AC
	orb	rl3, rl5
	jr	fdone
facl:
	ldb	rl5, rh4		/ the 8080's inverted half borrow
	xorb	rl5, rl4
	xorb	rl5, rl2
	andb	rl5, $F_AC
	xorb	rl5, $F_AC
	orb	rl3, rl5
fdone:
	ldb	rr12(M_F), rl3
	clrb	rl3
	ldb	rr12(M_LZ), rl3
	ret
fslow:
	push	(rr14), r1
	pushl	(rr14), rr12
	call	z80flags_
	add	r15, $4
	pop	r1, (rr14)
	ret

/ r2 = an r field, 0..7.  getr: rl0 = the register.  setr: store rl0.
/ Both use r3.
getr:
	cp	r2, $6
	jr	eq, 1f
	jr	ugt, 2f
	ldb	rl0, rr12(r2)
	ret
1:
	ld	r3, rr12(M_HL)
	ldb	rl0, rr10(r3)
	ret
2:
	ldb	rl0, rr12(M_A)
	ret

setr:
	cp	r2, $6
	jr	eq, 1f
	jr	ugt, 2f
	ldb	rr12(r2), rl0
	ret
1:
	ld	r3, rr12(M_HL)
	ldb	rr10(r3), rl0
	ret
2:
	ldb	rr12(M_A), rl0
	ret

/ r2 = a condition code, NZ Z NC C PO PE P M.  r0 = 1 and Z clear when it
/ holds, else r0 = 0 and Z set.  The one flag tested comes from the lazy
/ record by z80lcond()'s rules.  Uses r3-r5.
cond:
	ldb	rl3, rr12(M_LZ)
	testb	rl3
	jr	z, cf
	cpb	rl3, $LZ_LDBLK
	jr	eq, cfl
	ld	r4, r2
	and	r4, $6
	jr	nz, 1f
	ld	r0, rr12(M_LR)		/ Z
	test	r0
	jr	z, ct
	jr	cn
1:
	cp	r4, $2
	jr	eq, ccy
	ld	r0, rr12(M_LR)
	cp	r4, $4
	jr	eq, 2f
	bit	r0, $7			/ M
	jr	nz, ct
	jr	cn
2:
	cpb	rl3, $LZ_CPBLK		/ PE
	jr	eq, clc
	testb	rl0
	jr	pe, ct
	jr	cn
ccy:
	cpb	rl3, $1
	jr	eq, cadd
	cpb	rl3, $2
	jr	eq, csub
	cpb	rl3, $3
	jr	eq, cn
	cpb	rl3, $4
	jr	eq, cn
	cpb	rl3, $7
	jr	eq, clc
	ldb	rl0, rr12(M_F)		/ a class that keeps CY in f
	andb	rl0, $1
	jr	nz, ct
	jr	cn
cadd:
	ld	r0, rr12(M_LR)		/ lc ? lr <= la : lr < la
	ld	r4, rr12(M_LA)
	jr	3f
csub:
	ld	r0, rr12(M_LA)		/ lc ? la <= lb : la < lb
	ld	r4, rr12(M_LB)
3:
	ldb	rl5, rr12(M_LC)
	testb	rl5
	jr	nz, 4f
	cp	r0, r4
	jr	ult, ct
	jr	cn
4:
	cp	r0, r4
	jr	ule, ct
	jr	cn
clc:
	ldb	rl0, rr12(M_LC)
	testb	rl0
	jr	nz, ct
	jr	cn
cfl:
	push	(rr14), r2
	call	fold
	pop	r2, (rr14)
cf:
	ld	r4, r2
	srl	r4, $1
	ldb	rl0, rr12(M_F)
	andb	rl0, cmask(r4)
	jr	nz, ct
cn:
	bit	r2, $0
	jr	z, cyes
cno:
	xor	r0, r0
	ret
ct:
	bit	r2, $0
	jr	z, cno
cyes:
	ld	r0, $1
	test	r0
	ret

cmask:
	.byte	0x40, 0x01, 0x04, 0x80	/ Z CY P S

/ S, Z, even parity and bit 1 of the F a result byte gives.
sztab:
	.byte	0x46, 0x02, 0x02, 0x06, 0x02, 0x06, 0x06, 0x02, 0x02, 0x06, 0x06, 0x02, 0x06, 0x02, 0x02, 0x06	/ 00
	.byte	0x02, 0x06, 0x06, 0x02, 0x06, 0x02, 0x02, 0x06, 0x06, 0x02, 0x02, 0x06, 0x02, 0x06, 0x06, 0x02	/ 10
	.byte	0x02, 0x06, 0x06, 0x02, 0x06, 0x02, 0x02, 0x06, 0x06, 0x02, 0x02, 0x06, 0x02, 0x06, 0x06, 0x02	/ 20
	.byte	0x06, 0x02, 0x02, 0x06, 0x02, 0x06, 0x06, 0x02, 0x02, 0x06, 0x06, 0x02, 0x06, 0x02, 0x02, 0x06	/ 30
	.byte	0x02, 0x06, 0x06, 0x02, 0x06, 0x02, 0x02, 0x06, 0x06, 0x02, 0x02, 0x06, 0x02, 0x06, 0x06, 0x02	/ 40
	.byte	0x06, 0x02, 0x02, 0x06, 0x02, 0x06, 0x06, 0x02, 0x02, 0x06, 0x06, 0x02, 0x06, 0x02, 0x02, 0x06	/ 50
	.byte	0x06, 0x02, 0x02, 0x06, 0x02, 0x06, 0x06, 0x02, 0x02, 0x06, 0x06, 0x02, 0x06, 0x02, 0x02, 0x06	/ 60
	.byte	0x02, 0x06, 0x06, 0x02, 0x06, 0x02, 0x02, 0x06, 0x06, 0x02, 0x02, 0x06, 0x02, 0x06, 0x06, 0x02	/ 70
	.byte	0x82, 0x86, 0x86, 0x82, 0x86, 0x82, 0x82, 0x86, 0x86, 0x82, 0x82, 0x86, 0x82, 0x86, 0x86, 0x82	/ 80
	.byte	0x86, 0x82, 0x82, 0x86, 0x82, 0x86, 0x86, 0x82, 0x82, 0x86, 0x86, 0x82, 0x86, 0x82, 0x82, 0x86	/ 90
	.byte	0x86, 0x82, 0x82, 0x86, 0x82, 0x86, 0x86, 0x82, 0x82, 0x86, 0x86, 0x82, 0x86, 0x82, 0x82, 0x86	/ A0
	.byte	0x82, 0x86, 0x86, 0x82, 0x86, 0x82, 0x82, 0x86, 0x86, 0x82, 0x82, 0x86, 0x82, 0x86, 0x86, 0x82	/ B0
	.byte	0x86, 0x82, 0x82, 0x86, 0x82, 0x86, 0x86, 0x82, 0x82, 0x86, 0x86, 0x82, 0x86, 0x82, 0x82, 0x86	/ C0
	.byte	0x82, 0x86, 0x86, 0x82, 0x86, 0x82, 0x82, 0x86, 0x86, 0x82, 0x82, 0x86, 0x82, 0x86, 0x86, 0x82	/ D0
	.byte	0x82, 0x86, 0x86, 0x82, 0x86, 0x82, 0x82, 0x86, 0x86, 0x82, 0x82, 0x86, 0x82, 0x86, 0x86, 0x82	/ E0
	.byte	0x86, 0x82, 0x82, 0x86, 0x82, 0x86, 0x86, 0x82, 0x82, 0x86, 0x86, 0x82, 0x86, 0x82, 0x82, 0x86	/ F0
	.even

/ ---- the handlers

h0:
h_nop:
	inc	r9, $1
	NEXT

/ getr and setr, inline.
h_ldrr:
	sll	r1, $3
	clr	r2
	ldb	rl2, B_Y(r1)
	cp	r2, $6
	jr	eq, 1f
	jr	ugt, 2f
	ldb	rl0, rr12(r2)
	jr	3f
1:
	ld	r3, rr12(M_HL)
	ldb	rl0, rr10(r3)
	jr	3f
2:
	ldb	rl0, rr12(M_A)
3:
	ldb	rl2, B_X(r1)
	inc	r9, $1
	cp	r2, $6
	jr	eq, 1f
	jr	ugt, 2f
	ldb	rr12(r2), rl0
	NEXT
1:
	ld	r3, rr12(M_HL)
	ldb	rr10(r3), rl0
	NEXT
2:
	ldb	rr12(M_A), rl0
	NEXT

h_ldri:
	sll	r1, $3
	inc	r9, $1
	ldb	rl0, rr10(r9)
	inc	r9, $1
	clr	r2
	ldb	rl2, B_X(r1)
	call	setr
	NEXT

h_jmp:
	FETCH2
	ld	r9, r3
	NEXT

h_jcc:
	sll	r1, $3
	clr	r2
	ldb	rl2, B_X(r1)
	call	cond
	jr	z, 1f
	FETCH2
	ld	r9, r3
	NEXT
1:
	inc	r9, $3
	NEXT

h_call:
	FETCHW
	ld	r0, r9
	PUSH
	ld	r9, r3
	NEXT

h_ccc:
	sll	r1, $3
	clr	r2
	ldb	rl2, B_X(r1)
	call	cond
	jr	z, 1f
	FETCHW
	ld	r0, r9
	PUSH
	ld	r9, r3
	NEXT
1:
	inc	r9, $3
	NEXT

h_ret:
	POP
	ld	r9, r0
	NEXT

h_rcc:
	sll	r1, $3
	clr	r2
	ldb	rl2, B_X(r1)
	call	cond
	jr	z, 1f
	POP
	ld	r9, r0
	NEXT
1:
	inc	r9, $1
	NEXT

h_push:
	sll	r1, $3
	clr	r2
	ldb	rl2, B_X(r1)
	cp	r2, $3
	jr	eq, 1f
	add	r2, r2
	ld	r0, rr12(r2)
	jr	2f
1:
	call	fold			/ PSW: A over the real flags
	ld	r0, rr12(M_A)
2:
	PUSH
	inc	r9, $1
	NEXT

h_pop:
	sll	r1, $3
	POP
	clr	r2
	ldb	rl2, B_X(r1)
	cp	r2, $3
	jr	eq, 1f
	add	r2, r2
	ld	rr12(r2), r0
	inc	r9, $1
	NEXT
1:
	ldb	rr12(M_A), rh0		/ PSW: bit 1 set, bits 5 and 3 clear
	orb	rl0, $0x02
	andb	rl0, $0xd7
	ldb	rr12(M_F), rl0
	clrb	rh0
	ldb	rr12(M_LZ), rh0
	inc	r9, $1
	NEXT

h_lxi:
	sll	r1, $3
	FETCHW
	clr	r2
	ldb	rl2, B_X(r1)
	add	r2, r2
	ld	rr12(r2), r3
	NEXT

h_lhld:
	FETCHW
	ldb	rl0, rr10(r3)
	inc	r3, $1
	ldb	rh0, rr10(r3)
	ld	rr12(M_HL), r0
	NEXT

h_shld:
	FETCHW
	ld	r0, rr12(M_HL)
	ldb	rr10(r3), rl0
	inc	r3, $1
	ldb	rr10(r3), rh0
	NEXT

h_lda:
	FETCHW
	ldb	rl0, rr10(r3)
	ldb	rr12(M_A), rl0
	NEXT

h_sta:
	FETCHW
	ldb	rl0, rr12(M_A)
	ldb	rr10(r3), rl0
	NEXT

h_inx:
	sll	r1, $3
	clr	r2
	ldb	rl2, B_X(r1)
	add	r2, r2
	ld	r0, rr12(r2)
	inc	r0, $1
	ld	rr12(r2), r0
	inc	r9, $1
	NEXT

h_dcx:
	sll	r1, $3
	clr	r2
	ldb	rl2, B_X(r1)
	add	r2, r2
	ld	r0, rr12(r2)
	dec	r0, $1
	ld	rr12(r2), r0
	inc	r9, $1
	NEXT

h_ldax:
	sll	r1, $3
	clr	r2
	ldb	rl2, B_X(r1)
	add	r2, r2
	ld	r3, rr12(r2)
	ldb	rl0, rr10(r3)
	ldb	rr12(M_A), rl0
	inc	r9, $1
	NEXT

h_stax:
	sll	r1, $3
	clr	r2
	ldb	rl2, B_X(r1)
	add	r2, r2
	ld	r3, rr12(r2)
	ldb	rl0, rr12(M_A)
	ldb	rr10(r3), rl0
	inc	r9, $1
	NEXT

h_xchg:
	ldl	rr2, rr12(M_DE)
	ex	r2, r3
	ldl	rr12(M_DE), rr2
	inc	r9, $1
	NEXT

h_cma:
	ldb	rl0, rr12(M_A)
	comb	rl0
	ldb	rr12(M_A), rl0
	inc	r9, $1
	NEXT

h_pchl:
	ld	r9, rr12(M_HL)
	NEXT

/ DAD sets CY alone, over the materialised flags.
h_dad:
	sll	r1, $3
	call	fold
	clr	r2
	ldb	rl2, B_X(r1)
	add	r2, r2
	ld	r3, rr12(r2)
	ld	r0, rr12(M_HL)
	add	r0, r3
	ld	rr12(M_HL), r0
	ldb	rl0, rr12(M_F)
	jr	c, 1f
	andb	rl0, $0xd6
	orb	rl0, $0x02
	jr	2f
1:
	andb	rl0, $0xd6
	orb	rl0, $0x03
2:
	ldb	rr12(M_F), rl0
	inc	r9, $1
	NEXT

/ INR and DCR keep CY: fold first unless the pending record is one of
/ theirs, which left it in f.
h_inr:
	sll	r1, $3
	ldb	rl0, rr12(M_LZ)
	subb	rl0, $5
	cpb	rl0, $1
	jr	ule, 1f
	call	fold
1:
	clr	r2
	ldb	rl2, B_X(r1)
	call	getr
	clrb	rh0
	ld	r4, r0
	incb	rl0, $1
	call	setr
	ld	r5, $LZINR
	jr	incdec

h_dcr:
	sll	r1, $3
	ldb	rl0, rr12(M_LZ)
	subb	rl0, $5
	cpb	rl0, $1
	jr	ule, 1f
	call	fold
1:
	clr	r2
	ldb	rl2, B_X(r1)
	call	getr
	clrb	rh0
	ld	r4, r0
	decb	rl0, $1
	call	setr
	ld	r5, $LZDCR
	testb	rl0
	jr	z, incdec
	cp	r2, $6
	jr	eq, incdec
	ld	r3, r9			/ JNZ or JR NZ back to the DCR
	inc	r3, $1
	ldb	rl1, rr10(r3)
	inc	r3, $1
	cpb	rl1, $0xc2
	jr	ne, 1f
	ldb	rl1, rr10(r3)
	inc	r3, $1
	ldb	rh1, rr10(r3)
	cp	r1, r9
	jr	ne, incdec
	jr	2f
1:
	cpb	rl1, $0x20
	jr	ne, incdec
	ldb	rl1, rr10(r3)
	cpb	rl1, $0xfd
	jr	ne, incdec
2:
	ld	r3, z80fast_
	test	r3
	jr	z, incdec
	ld	rr12(M_LA), r4
	ld	r3, $1
	ld	rr12(M_LB), r3
	ld	rr12(M_LR), r0
	ldb	rr12(M_LZ), rh5
	ldb	rr12(M_LC), rl5
	inc	r9, $1
	ld	rr12(M_PC), r9
	dec	r9, $1
	push	(rr14), r0
	push	(rr14), r9
	push	(rr14), r2
	pushl	(rr14), rr12
	call	z80delay_
	add	r15, $10
	ld	r9, rr12(M_PC)
	NEXT

incdec:
	ld	rr12(M_LA), r4
	ld	r3, $1
	ld	rr12(M_LB), r3
	ld	rr12(M_LR), r0
	ldb	rr12(M_LZ), rh5
	ldb	rr12(M_LC), rl5
	inc	r9, $1
	NEXT

/ The ALU eight: r0 = the source byte, r4 = A, then by operation.  Each
/ leaves r3 = the result and r5 = the lazy class and carry in.
h_alu:
	sll	r1, $3
	ldb	rl0, B_FL(r1)
	bitb	rl0, $0			/ ZF_IMM
	jr	nz, 1f
	clr	r2
	ldb	rl2, B_Y(r1)
	inc	r9, $1
	cp	r2, $6
	jr	eq, 3f
	jr	ugt, 4f
	ldb	rl0, rr12(r2)
	jr	2f
3:
	ld	r3, rr12(M_HL)
	ldb	rl0, rr10(r3)
	jr	2f
4:
	ldb	rl0, rr12(M_A)
	jr	2f
1:
	inc	r9, $1
	ldb	rl0, rr10(r9)
	inc	r9, $1
2:
	clrb	rh0
	clr	r4
	ldb	rl4, rr12(M_A)
	clr	r2
	ldb	rl2, B_X(r1)
	add	r2, r2
	ld	r2, atab(r2)
	jp	h0(r2)

a_add:
	ld	r3, r4
	addb	rl3, rl0
	ldb	rr12(M_A), rl3
	ld	r5, $LZADD
	jr	rec
a_sub:
	ld	r3, r4
	subb	rl3, rl0
	ldb	rr12(M_A), rl3
	ld	r5, $LZSUB
	jr	rec
a_cmp:
	ld	r3, r4
	subb	rl3, rl0
	ld	r5, $LZSUB
	jr	rec
a_ana:
	ld	r3, r4
	andb	rl3, rl0
	ldb	rr12(M_A), rl3
	ld	r5, $LZAND
	jr	rec
a_xra:
	ld	r3, r4
	xorb	rl3, rl0
	ldb	rr12(M_A), rl3
	ld	r5, $LZLOG
	jr	rec
a_ora:
	ld	r3, r4
	orb	rl3, rl0
	ldb	rr12(M_A), rl3
	ld	r5, $LZLOG
	jr	rec
a_adc:
	push	(rr14), r0
	call	fold
	pop	r0, (rr14)
	clr	r4
	ldb	rl4, rr12(M_A)
	ldb	rl5, rr12(M_F)
	andb	rl5, $1
	ld	r3, r4
	addb	rl3, rl0
	addb	rl3, rl5
	ldb	rr12(M_A), rl3
	ldb	rh5, $1
	jr	rec
a_sbb:
	push	(rr14), r0
	call	fold
	pop	r0, (rr14)
	clr	r4
	ldb	rl4, rr12(M_A)
	ldb	rl5, rr12(M_F)
	andb	rl5, $1
	ld	r3, r4
	subb	rl3, rl0
	subb	rl3, rl5
	ldb	rr12(M_A), rl3
	ldb	rh5, $2
rec:
	ldb	rr12(M_LZ), rh5
	ldb	rr12(M_LC), rl5
	ld	rr12(M_LA), r4
	ld	rr12(M_LB), r0
	ld	rr12(M_LR), r3
	NEXT

/ JR and DJNZ: the target is PC past the two bytes plus the displacement.
h_jr:
	sll	r1, $3
	inc	r9, $1
	ldb	rl0, rr10(r9)
	inc	r9, $1
	extsb	r0
	add	r0, r9
	ld	r8, r0
	clr	r2
	ldb	rl2, B_X(r1)
	cp	r2, $4
	jr	eq, 1f
	call	cond
	jr	z, 2f
1:
	ld	r9, r8
2:
	NEXT

h_djnz:
	inc	r9, $1
	ldb	rl0, rr10(r9)
	inc	r9, $1
	extsb	r0
	add	r0, r9
	ldb	rl2, rr12(M_B)
	decb	rl2, $1
	ldb	rr12(M_B), rl2
	jr	z, 1f
	ld	r9, r0
1:
	NEXT

/ Everything else: the whole decode, then the C executor.  A refusal
/ leaves the loop; Z_BAD leaves PC on the bytes and is not counted.
h_cold:
	ldl	rr2, rr14
	add	r3, $F_IN
	pushl	(rr14), rr2
	push	(rr14), r9
	pushl	(rr14), rr10
	call	z80dec_
	add	r15, $10
	ldb	rl0, rr14(F_IN+1)
	testb	rl0
	jr	z, 1f
	ld	r8, r9
	add	r9, r1
	ld	rr12(M_PC), r9
	ldl	rr2, rr14
	add	r3, $F_IN
	push	(rr14), r8
	pushl	(rr14), rr2
	pushl	(rr14), rr12
	call	z80exec_
	add	r15, $10
	ld	r9, rr12(M_PC)
	test	r1
	jr	nz, 2f
	NEXT
1:
	ld	r1, $X_BAD
	clr	r0
	jp	out
2:
	ld	r0, $1
	jp	out

/ ---- dispatch tables

/ Each Z_* class's handler, by z80.h's number.
#define K0	h_cold	/* BAD */
#define K1	h_alu
#define K2	h_jcc
#define K3	h_push
#define K4	h_pop
#define K5	h_ldrr
#define K6	h_ldri
#define K7	h_jmp
#define K8	h_lhld
#define K9	h_call
#define K10	h_ret
#define K11	h_lxi
#define K12	h_lda
#define K13	h_inx
#define K14	h_cold	/* ROT */
#define K15	h_cold	/* HOOK */
#define K16	h_shld
#define K17	h_dad
#define K18	h_sta
#define K19	h_inr
#define K20	h_ldax
#define K21	h_dcr
#define K22	h_rcc
#define K23	h_dcx
#define K24	h_xchg
#define K25	h_nop
#define K26	h_cma
#define K27	h_pchl
#define K28	h_ccc
#define K29	h_stax
#define K30	h_cold	/* DAA */
#define K31	h_cold	/* STC */
#define K32	h_cold	/* CMC */
#define K33	h_cold	/* RST */
#define K34	h_cold	/* SPHL */
#define K35	h_cold	/* XTHL */
#define K36	h_cold	/* IN */
#define K37	h_cold	/* OUT */
#define K38	h_cold	/* EI */
#define K39	h_cold	/* DI */
#define K40	h_cold	/* HLT */
#define K41	h_jr
#define K42	h_djnz
#define K43	h_cold	/* EXAF */
#define K44	h_cold	/* EXX */
#define K45	h_cold	/* CB */
#define K46	h_cold	/* ED */
#define K47	h_cold	/* IX */
#define K48	h_cold	/* IXCB */

/ Each opcode's handler, from the base map's class column; a prefix byte
/ has class 0.
#define E(l,o,f,x,y,u) .word K/**/o-h0
otab:
#include "z80btab.h"

/ The ALU eight, in the opcode's order.
atab:
	.word	a_add-h0, a_adc-h0, a_sub-h0, a_sbb-h0
	.word	a_ana-h0, a_xra-h0, a_ora-h0, a_cmp-h0
