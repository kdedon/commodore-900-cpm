/ Copyright (c) 2026 Kevin Dedon.
/ SPDX-License-Identifier: MIT

/ z80deca.s -- the 8080/Z80 decoder, for the target build.
/
/	int z80dec(m, pc, in)  char *m;  z16 pc;  struct z80in *in;
/
/ Same answers as z80dec.c, which stays the portable reference.  ZDECT
/ runs both decoders over the opcode space on the machine and compares
/ every field, so a fix made to one and not the other fails the suite.
/
/ The base map is one 16-byte record per opcode whose first twelve bytes
/ ARE a struct z80in: a decode is a seven-word load and a six-word store,
/ and the operand fetch is the only thing left to do.  The last two words
/ are read through the DD/FD prefix alone -- the opcode's own length, and
/ whether it names the byte at (HL) and so grows a displacement under the
/ prefix.  A zero length marks the four prefix bytes; their record is all
/ zeros, which is exactly the cleared fields the prefix paths start from.
/
/ Guest fetches wrap at 16 bits: the offset half of the segmented pointer
/ is added to in a word register and the base+displacement forms below
/ wrap within the segment, so an instruction straddling 0xFFFF continues
/ at 0 without a test.

/ struct z80in offsets and the Z_*/ZF_* values z80.h gives the C.
#define IN_LEN	0
#define IN_OP	1
#define IN_FL	2
#define IN_X	3
#define IN_Y	4
#define IN_PFX	5
#define IN_SUB	6
#define IN_IMM	8
#define IN_DISP	10

#define Z_BAD	0
#define Z_HOOK	15
#define Z_JR	41
#define Z_DJNZ	42
#define Z_CB	45
#define Z_ED	46
#define Z_IX	47
#define Z_IXCB	48

#define R_M	6
#define ZF_PFX	8
#define HOOK_MAX 63
/ Flag combinations, spelled out: the assembler's `|' does not answer what
/ C's does.
#define FL_CBM	10		/ ZF_PFX|ZF_MEM
#define FL_EDNN	15		/ ZF_PFX|ZF_IMM|ZF_ADDR|ZF_MEM

/ Arguments, past the 4-byte return address and the 14 bytes of saved
/ registers.
#define A_M	18
#define A_PC	22
#define A_IN	24

	.shri

	.globl	z80dec_

z80dec_:
	sub	r15, $14
	ldm	(rr14), r6, $7		/ r6..r12 are callee-saved
	ldl	rr2, rr14(A_M)
	ld	r1, rr14(A_PC)
	add	r3, r1			/ rr2 -> the opcode byte
	clrb	rh1
	ldb	rl1, (rr2)
	sll	r1, $4
	ldm	r6, btab(r1), $7	/ the record, then its length word
	ldl	rr4, rr14(A_IN)
	ldm	(rr4), r6, $6
	cp	r12, $1
	jr	ne, wide
out:
	ld	r1, r12			/ the length is the return value
	ldm	r6, (rr14), $7
	add	r15, $14
	ret

/ ---- an operand to fetch, or a prefix to take apart
wide:
	cp	r12, $3
	jr	eq, w3
	cp	r12, $2
	jp	ne, pfx
	clrb	rh0
	ldb	rl0, rr2(1)
	cpb	rl6, $Z_JR		/ .imm of a relative branch is the
	jr	eq, wrel		/ target, not the displacement
	cpb	rl6, $Z_DJNZ
	jr	eq, wrel
	ld	rr4(IN_IMM), r0
	jr	out
wrel:
	extsb	r0
	ld	r1, rr14(A_PC)
	add	r0, r1
	inc	r0, $2
	ld	rr4(IN_IMM), r0
	jr	out
w3:
	ldb	rl0, rr2(1)		/ the guest stores the low byte first
	ldb	rh0, rr2(2)
	ld	rr4(IN_IMM), r0
	jr	out

/ ---- CB, ED, DD and FD: four opcode spaces, four length rules
pfx:
	srl	r1, $4			/ the prefix byte
	clrb	rh0
	ldb	rl0, rr2(1)		/ the byte after it
	ldb	rr4(IN_PFX), rl1
	ldb	rr4(IN_SUB), rl0
	ldb	rh7, $ZF_PFX
	ldb	rr4(IN_FL), rh7
	cp	r1, $0xcb
	jr	eq, pcb
	cp	r1, $0xed
	jp	eq, ped

/ DD/FD.  A prefix on a prefix is discarded by the hardware -- DD ED B0
/ is a plain LDIR -- so the length is ONE and the next decode starts on
/ the second prefix.
	cp	r0, $0xdd
	jr	eq, pix1
	cp	r0, $0xfd
	jr	eq, pix1
	cp	r0, $0xed
	jr	eq, pix1
	cp	r0, $0xcb
	jr	eq, pixcb
	ldb	rh7, $Z_IX
	ldb	rr4(IN_OP), rh7
	ld	r7, r0
	sll	r7, $4
	ld	r6, btab+12(r7)		/ the covered opcode's own length
	inc	r6, $1
	ld	r8, btab+14(r7)		/ .. and its (HL), which becomes (IX+d)
	test	r8
	jr	z, plen
	inc	r6, $1
	ldb	rl7, rr2(2)
	extsb	r7
	ld	rr4(IN_DISP), r7
	jr	plen
pix1:
	ldb	rh7, $Z_IX
	ldb	rr4(IN_OP), rh7
	ld	r6, $1
	jr	plen
pixcb:
/ DD CB d op: the displacement comes BEFORE the operation byte.
	ldb	rh7, $Z_IXCB
	ldb	rr4(IN_OP), rh7
	ldb	rl7, rr2(2)
	extsb	r7
	ld	rr4(IN_DISP), r7
	ld	r6, $4
	jr	plen

/ CB: two bits of group, three of operation or bit number, three of
/ register, always two bytes.
pcb:
	ldb	rh7, $Z_CB
	ldb	rr4(IN_OP), rh7
	ld	r7, r0
	srl	r7, $6
	ldb	rr4(IN_X), rl7
	ld	r7, r0
	srl	r7, $3
	and	r7, $7
	ld	rr4(IN_IMM), r7
	ld	r7, r0
	and	r7, $7
	ldb	rr4(IN_Y), rl7
	ld	r6, $2
	cp	r7, $R_M
	jr	ne, plen
	ldb	rh7, $FL_CBM
	ldb	rr4(IN_FL), rh7
	jr	plen

/ ED.  FE is OUR escape and takes a third byte; the eight LD (nn),dd and
/ LD dd,(nn) forms -- 01xxx011 -- carry an address; everything else in
/ the group is two bytes.
ped:
	cpb	rl0, $0xfe
	jr	eq, phook
	ldb	rh7, $Z_ED
	ldb	rr4(IN_OP), rh7
	ld	r6, $2
	ld	r7, r0
	and	r7, $0xc7
	cp	r7, $0x43
	jr	ne, plen
	ld	r6, $4
	ld	r7, r0
	srl	r7, $4
	and	r7, $3
	ldb	rr4(IN_X), rl7		/ the register pair
	ldb	rl7, rr2(2)
	ldb	rh7, rr2(3)
	ld	rr4(IN_IMM), r7
	ldb	rh7, $FL_EDNN
	ldb	rr4(IN_FL), rh7
	jr	plen
phook:
	ldb	rh7, $Z_HOOK
	ldb	rr4(IN_OP), rh7
	clrb	rh7
	ldb	rl7, rr2(2)
	ldb	rr4(IN_X), rl7
	ld	r6, $3
	cp	r7, $HOOK_MAX
	jr	ule, plen
	ldb	rh7, $Z_BAD
	ldb	rr4(IN_OP), rh7
plen:
	ldb	rr4(IN_LEN), rl6
	ld	r1, r6
	ldm	r6, (rr14), $7
	add	r15, $14
	ret

/ ---- the base map: len, op, fl, x, y, pfx, sub, pad, imm, disp, then
/ ---- the length again as a word, then (HL) as a word.
#define E(l,o,f,x,y,u) .byte l,o,f,x,y,0,0,0,0,0,0,0,0,l,0,u

	.globl	btab		/ the run loop reads its fields too
	.even
btab:
#include "z80btab.h"
