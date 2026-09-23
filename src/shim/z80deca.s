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
#define Z_JR	40
#define Z_DJNZ	41
#define Z_CB	44
#define Z_ED	45
#define Z_IX	46
#define Z_IXCB	47
#define Z_HOOK	48

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

	.even
btab:
	E(1,1,0,0,0,0)	/ 00
	E(3,7,1,0,0,0)	/ 01
	E(1,12,2,0,0,0)	/ 02
	E(1,9,0,0,0,0)	/ 03
	E(1,5,0,0,0,0)	/ 04
	E(1,6,0,0,0,0)	/ 05
	E(2,3,1,0,0,0)	/ 06
	E(1,17,0,0,0,0)	/ 07
	E(1,42,0,0,0,0)	/ 08
	E(1,8,0,0,0,0)	/ 09
	E(1,11,2,0,0,0)	/ 0A
	E(1,10,0,0,0,0)	/ 0B
	E(1,5,0,1,0,0)	/ 0C
	E(1,6,0,1,0,0)	/ 0D
	E(2,3,1,1,0,0)	/ 0E
	E(1,17,0,1,0,0)	/ 0F
	E(2,41,0,0,0,0)	/ 10
	E(3,7,1,1,0,0)	/ 11
	E(1,12,2,1,0,0)	/ 12
	E(1,9,0,1,0,0)	/ 13
	E(1,5,0,2,0,0)	/ 14
	E(1,6,0,2,0,0)	/ 15
	E(2,3,1,2,0,0)	/ 16
	E(1,17,0,2,0,0)	/ 17
	E(2,40,0,4,0,0)	/ 18
	E(1,8,0,1,0,0)	/ 19
	E(1,11,2,1,0,0)	/ 1A
	E(1,10,0,1,0,0)	/ 1B
	E(1,5,0,3,0,0)	/ 1C
	E(1,6,0,3,0,0)	/ 1D
	E(2,3,1,3,0,0)	/ 1E
	E(1,17,0,3,0,0)	/ 1F
	E(2,40,0,0,0,0)	/ 20
	E(3,7,1,2,0,0)	/ 21
	E(3,16,7,0,0,0)	/ 22
	E(1,9,0,2,0,0)	/ 23
	E(1,5,0,4,0,0)	/ 24
	E(1,6,0,4,0,0)	/ 25
	E(2,3,1,4,0,0)	/ 26
	E(1,18,0,0,0,0)	/ 27
	E(2,40,0,1,0,0)	/ 28
	E(1,8,0,2,0,0)	/ 29
	E(3,15,7,0,0,0)	/ 2A
	E(1,10,0,2,0,0)	/ 2B
	E(1,5,0,5,0,0)	/ 2C
	E(1,6,0,5,0,0)	/ 2D
	E(2,3,1,5,0,0)	/ 2E
	E(1,19,0,0,0,0)	/ 2F
	E(2,40,0,2,0,0)	/ 30
	E(3,7,1,3,0,0)	/ 31
	E(3,14,7,0,0,0)	/ 32
	E(1,9,0,3,0,0)	/ 33
	E(1,5,2,6,0,1)	/ 34
	E(1,6,2,6,0,1)	/ 35
	E(2,3,3,6,0,1)	/ 36
	E(1,20,0,0,0,0)	/ 37
	E(2,40,0,3,0,0)	/ 38
	E(1,8,0,3,0,0)	/ 39
	E(3,13,7,0,0,0)	/ 3A
	E(1,10,0,3,0,0)	/ 3B
	E(1,5,0,7,0,0)	/ 3C
	E(1,6,0,7,0,0)	/ 3D
	E(2,3,1,7,0,0)	/ 3E
	E(1,21,0,0,0,0)	/ 3F
	E(1,2,0,0,0,0)	/ 40
	E(1,2,0,0,1,0)	/ 41
	E(1,2,0,0,2,0)	/ 42
	E(1,2,0,0,3,0)	/ 43
	E(1,2,0,0,4,0)	/ 44
	E(1,2,0,0,5,0)	/ 45
	E(1,2,2,0,6,1)	/ 46
	E(1,2,0,0,7,0)	/ 47
	E(1,2,0,1,0,0)	/ 48
	E(1,2,0,1,1,0)	/ 49
	E(1,2,0,1,2,0)	/ 4A
	E(1,2,0,1,3,0)	/ 4B
	E(1,2,0,1,4,0)	/ 4C
	E(1,2,0,1,5,0)	/ 4D
	E(1,2,2,1,6,1)	/ 4E
	E(1,2,0,1,7,0)	/ 4F
	E(1,2,0,2,0,0)	/ 50
	E(1,2,0,2,1,0)	/ 51
	E(1,2,0,2,2,0)	/ 52
	E(1,2,0,2,3,0)	/ 53
	E(1,2,0,2,4,0)	/ 54
	E(1,2,0,2,5,0)	/ 55
	E(1,2,2,2,6,1)	/ 56
	E(1,2,0,2,7,0)	/ 57
	E(1,2,0,3,0,0)	/ 58
	E(1,2,0,3,1,0)	/ 59
	E(1,2,0,3,2,0)	/ 5A
	E(1,2,0,3,3,0)	/ 5B
	E(1,2,0,3,4,0)	/ 5C
	E(1,2,0,3,5,0)	/ 5D
	E(1,2,2,3,6,1)	/ 5E
	E(1,2,0,3,7,0)	/ 5F
	E(1,2,0,4,0,0)	/ 60
	E(1,2,0,4,1,0)	/ 61
	E(1,2,0,4,2,0)	/ 62
	E(1,2,0,4,3,0)	/ 63
	E(1,2,0,4,4,0)	/ 64
	E(1,2,0,4,5,0)	/ 65
	E(1,2,2,4,6,1)	/ 66
	E(1,2,0,4,7,0)	/ 67
	E(1,2,0,5,0,0)	/ 68
	E(1,2,0,5,1,0)	/ 69
	E(1,2,0,5,2,0)	/ 6A
	E(1,2,0,5,3,0)	/ 6B
	E(1,2,0,5,4,0)	/ 6C
	E(1,2,0,5,5,0)	/ 6D
	E(1,2,2,5,6,1)	/ 6E
	E(1,2,0,5,7,0)	/ 6F
	E(1,2,2,6,0,1)	/ 70
	E(1,2,2,6,1,1)	/ 71
	E(1,2,2,6,2,1)	/ 72
	E(1,2,2,6,3,1)	/ 73
	E(1,2,2,6,4,1)	/ 74
	E(1,2,2,6,5,1)	/ 75
	E(1,39,0,0,0,0)	/ 76
	E(1,2,2,6,7,1)	/ 77
	E(1,2,0,7,0,0)	/ 78
	E(1,2,0,7,1,0)	/ 79
	E(1,2,0,7,2,0)	/ 7A
	E(1,2,0,7,3,0)	/ 7B
	E(1,2,0,7,4,0)	/ 7C
	E(1,2,0,7,5,0)	/ 7D
	E(1,2,2,7,6,1)	/ 7E
	E(1,2,0,7,7,0)	/ 7F
	E(1,4,0,0,0,0)	/ 80
	E(1,4,0,0,1,0)	/ 81
	E(1,4,0,0,2,0)	/ 82
	E(1,4,0,0,3,0)	/ 83
	E(1,4,0,0,4,0)	/ 84
	E(1,4,0,0,5,0)	/ 85
	E(1,4,2,0,6,1)	/ 86
	E(1,4,0,0,7,0)	/ 87
	E(1,4,0,1,0,0)	/ 88
	E(1,4,0,1,1,0)	/ 89
	E(1,4,0,1,2,0)	/ 8A
	E(1,4,0,1,3,0)	/ 8B
	E(1,4,0,1,4,0)	/ 8C
	E(1,4,0,1,5,0)	/ 8D
	E(1,4,2,1,6,1)	/ 8E
	E(1,4,0,1,7,0)	/ 8F
	E(1,4,0,2,0,0)	/ 90
	E(1,4,0,2,1,0)	/ 91
	E(1,4,0,2,2,0)	/ 92
	E(1,4,0,2,3,0)	/ 93
	E(1,4,0,2,4,0)	/ 94
	E(1,4,0,2,5,0)	/ 95
	E(1,4,2,2,6,1)	/ 96
	E(1,4,0,2,7,0)	/ 97
	E(1,4,0,3,0,0)	/ 98
	E(1,4,0,3,1,0)	/ 99
	E(1,4,0,3,2,0)	/ 9A
	E(1,4,0,3,3,0)	/ 9B
	E(1,4,0,3,4,0)	/ 9C
	E(1,4,0,3,5,0)	/ 9D
	E(1,4,2,3,6,1)	/ 9E
	E(1,4,0,3,7,0)	/ 9F
	E(1,4,0,4,0,0)	/ A0
	E(1,4,0,4,1,0)	/ A1
	E(1,4,0,4,2,0)	/ A2
	E(1,4,0,4,3,0)	/ A3
	E(1,4,0,4,4,0)	/ A4
	E(1,4,0,4,5,0)	/ A5
	E(1,4,2,4,6,1)	/ A6
	E(1,4,0,4,7,0)	/ A7
	E(1,4,0,5,0,0)	/ A8
	E(1,4,0,5,1,0)	/ A9
	E(1,4,0,5,2,0)	/ AA
	E(1,4,0,5,3,0)	/ AB
	E(1,4,0,5,4,0)	/ AC
	E(1,4,0,5,5,0)	/ AD
	E(1,4,2,5,6,1)	/ AE
	E(1,4,0,5,7,0)	/ AF
	E(1,4,0,6,0,0)	/ B0
	E(1,4,0,6,1,0)	/ B1
	E(1,4,0,6,2,0)	/ B2
	E(1,4,0,6,3,0)	/ B3
	E(1,4,0,6,4,0)	/ B4
	E(1,4,0,6,5,0)	/ B5
	E(1,4,2,6,6,1)	/ B6
	E(1,4,0,6,7,0)	/ B7
	E(1,4,0,7,0,0)	/ B8
	E(1,4,0,7,1,0)	/ B9
	E(1,4,0,7,2,0)	/ BA
	E(1,4,0,7,3,0)	/ BB
	E(1,4,0,7,4,0)	/ BC
	E(1,4,0,7,5,0)	/ BD
	E(1,4,2,7,6,1)	/ BE
	E(1,4,0,7,7,0)	/ BF
	E(1,27,0,0,0,0)	/ C0
	E(1,34,0,0,0,0)	/ C1
	E(3,23,4,0,0,0)	/ C2
	E(3,22,4,0,0,0)	/ C3
	E(3,25,4,0,0,0)	/ C4
	E(1,33,0,0,0,0)	/ C5
	E(2,4,1,0,0,0)	/ C6
	E(1,28,0,0,0,0)	/ C7
	E(1,27,0,1,0,0)	/ C8
	E(1,26,0,0,0,0)	/ C9
	E(3,23,4,1,0,0)	/ CA
	E(0,0,0,0,0,0)	/ CB
	E(3,25,4,1,0,0)	/ CC
	E(3,24,4,0,0,0)	/ CD
	E(2,4,1,1,0,0)	/ CE
	E(1,28,0,1,0,0)	/ CF
	E(1,27,0,2,0,0)	/ D0
	E(1,34,0,1,0,0)	/ D1
	E(3,23,4,2,0,0)	/ D2
	E(2,36,0,0,0,0)	/ D3
	E(3,25,4,2,0,0)	/ D4
	E(1,33,0,1,0,0)	/ D5
	E(2,4,1,2,0,0)	/ D6
	E(1,28,0,2,0,0)	/ D7
	E(1,27,0,3,0,0)	/ D8
	E(1,43,0,0,0,0)	/ D9
	E(3,23,4,3,0,0)	/ DA
	E(2,35,0,0,0,0)	/ DB
	E(3,25,4,3,0,0)	/ DC
	E(0,0,0,0,0,0)	/ DD
	E(2,4,1,3,0,0)	/ DE
	E(1,28,0,3,0,0)	/ DF
	E(1,27,0,4,0,0)	/ E0
	E(1,34,0,2,0,0)	/ E1
	E(3,23,4,4,0,0)	/ E2
	E(1,31,2,0,0,0)	/ E3
	E(3,25,4,4,0,0)	/ E4
	E(1,33,0,2,0,0)	/ E5
	E(2,4,1,4,0,0)	/ E6
	E(1,28,0,4,0,0)	/ E7
	E(1,27,0,5,0,0)	/ E8
	E(1,29,0,0,0,0)	/ E9
	E(3,23,4,5,0,0)	/ EA
	E(1,32,0,0,0,0)	/ EB
	E(3,25,4,5,0,0)	/ EC
	E(0,0,0,0,0,0)	/ ED
	E(2,4,1,5,0,0)	/ EE
	E(1,28,0,5,0,0)	/ EF
	E(1,27,0,6,0,0)	/ F0
	E(1,34,0,3,0,0)	/ F1
	E(3,23,4,6,0,0)	/ F2
	E(1,38,0,0,0,0)	/ F3
	E(3,25,4,6,0,0)	/ F4
	E(1,33,0,3,0,0)	/ F5
	E(2,4,1,6,0,0)	/ F6
	E(1,28,0,6,0,0)	/ F7
	E(1,27,0,7,0,0)	/ F8
	E(1,30,0,0,0,0)	/ F9
	E(3,23,4,7,0,0)	/ FA
	E(1,37,0,0,0,0)	/ FB
	E(3,25,4,7,0,0)	/ FC
	E(0,0,0,0,0,0)	/ FD
	E(2,4,1,7,0,0)	/ FE
	E(1,28,0,7,0,0)	/ FF
