/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/*
 * zsplit.c -- Z8001 nonsegmented instruction decode + the split-I/D
 * linear text scanner (see zsplit.h for the shim overview).
 *
 * zdecode() classifies exactly one instruction from its first word (and,
 * for the few encodings that need it, the second word): total length in
 * words, whether it references data-space memory, and every operand field
 * the trap-time interpreter needs.  Encodings follow the Zilog Z8000
 * family data book opcode map, nonsegmented lengths.
 *
 * Address-space rules for split I/D (Z8000 data book, MMU EXCV note:
 * execute-only segments admit "instruction fetch and load relative"
 * cycles only):
 *   - LDR/LDRB/LDRL and their store forms reference PROGRAM space;
 *     natively they read the code bank, which is correct -> ZK_PROG.
 *   - JP/CALL/JR/CALR/DJNZ/RET are instruction-space control flow, and
 *     their implicit stack traffic stays in the code bank where the shim
 *     keeps the user stack -> ZK_NONE.
 *   - LDA/LDAR compute an address without referencing memory -> ZK_NONE.
 *   - I/O and control instructions are privileged: a Normal-mode program
 *     takes a privilege trap before any memory access -> ZK_NONE.
 *   - everything else with a memory operand references DATA space ->
 *     ZK_DATA, and gets patched to the shim's SC trap.
 */

#include "zsplit.h"

#define NIB2(w)	(((w) >> 4) & 0x0F)
#define NIB3(w)	((w) & 0x0F)

/* Fill *id for the instruction whose first two words are w0/w1.
 * Returns the length in words (1..4).  Never returns 0. */
int zdecode(w0, w1, id)
zw w0, w1;
struct zid *id;
{
	register short hi, n2, n3;

	hi = (w0 >> 8) & 0xff;
	n2 = NIB2(w0);
	n3 = NIB3(w0);

	id->len = 1;
	id->klass = ZK_NONE;
	id->op = 0;
	id->width = ZW_W;
	id->mode = ZM_NONE;
	id->ra = n3;
	id->rb = n2;
	id->aop = 0;
	id->blk = 0;

	switch (hi >> 4) {

	case 0x0:
		if (hi <= 0x0B) {	/* ADD/SUB/OR/AND/XOR/CP  b/w  imm|@Rs */
			id->width = (hi & 1) ? ZW_W : ZW_B;
			if (n2 == 0) {	/* register dst, immediate src */
				id->len = 2;
				return (2);
			}
			id->klass = ZK_DATA;
			id->op = ZOP_ALUR;
			id->mode = ZM_IR;
			switch (hi >> 1) {
			case 0: id->aop = A_ADD; break;
			case 1: id->aop = A_SUB; break;
			case 2: id->aop = A_OR;  break;
			case 3: id->aop = A_AND; break;
			case 4: id->aop = A_XOR; break;
			case 5: id->aop = A_CP;  break;
			}
			return (1);
		}
		if (hi == 0x0C || hi == 0x0D) {	/* @Rs unary group */
			id->width = (hi == 0x0C) ? ZW_B : ZW_W;
			id->mode = ZM_IR;
			switch (n3) {
			case 0x0:
				id->klass = ZK_DATA; id->op = ZOP_ALUM;
				id->aop = A_COM; return (1);
			case 0x1:
				id->len = 2; id->klass = ZK_DATA;
				id->op = ZOP_TESTM; id->aop = A_CPIMM;
				return (2);
			case 0x2:
				id->klass = ZK_DATA; id->op = ZOP_ALUM;
				id->aop = A_NEG; return (1);
			case 0x4:
				id->klass = ZK_DATA; id->op = ZOP_TESTM;
				id->aop = A_TEST; return (1);
			case 0x5:
				id->len = 2; id->klass = ZK_DATA;
				id->op = ZOP_STIMM; return (2);
			case 0x6:
				id->klass = ZK_DATA; id->op = ZOP_ALUM;
				id->aop = A_TSET; return (1);
			case 0x8:
				id->klass = ZK_DATA; id->op = ZOP_CLR;
				return (1);
			case 0x9:
				if (hi == 0x0D) {	/* PUSH @Rd,#imm */
					id->len = 2; id->klass = ZK_DATA;
					id->op = ZOP_PUSHI; id->ra = n2;
					return (2);
				}
				id->klass = ZK_ILL; return (1);
			}
			id->klass = ZK_ILL;
			return (1);
		}
		/* 0x0E/0x0F: EPA (no EPU fitted; EPU trap policy skips) */
		return (1);

	case 0x1:
		switch (hi) {
		case 0x10: case 0x12: case 0x14: case 0x16:
			/* CPL/SUBL/LDL/ADDL  RRd, imm32|@Rs */
			id->width = ZW_L;
			if (n2 == 0) {
				id->len = 3;
				return (3);
			}
			id->klass = ZK_DATA; id->mode = ZM_IR;
			switch (hi) {
			case 0x10: id->op = ZOP_ALUR; id->aop = A_CP; break;
			case 0x12: id->op = ZOP_ALUR; id->aop = A_SUB; break;
			case 0x14: id->op = ZOP_LOAD; break;
			case 0x16: id->op = ZOP_ALUR; id->aop = A_ADD; break;
			}
			return (1);
		case 0x11:	/* PUSHL @Rd,@Rs */
			id->klass = ZK_DATA; id->op = ZOP_PUSH;
			id->width = ZW_L; id->mode = ZM_IR;
			id->ra = n2; id->rb = n3;
			return (1);
		case 0x13:	/* PUSH @Rd,@Rs */
			id->klass = ZK_DATA; id->op = ZOP_PUSH;
			id->mode = ZM_IR; id->ra = n2; id->rb = n3;
			return (1);
		case 0x15:	/* POPL @Rd,@Rs */
			id->klass = ZK_DATA; id->op = ZOP_POP;
			id->width = ZW_L; id->mode = ZM_IR;
			id->ra = n2; id->rb = n3;
			return (1);
		case 0x17:	/* POP @Rd,@Rs */
			id->klass = ZK_DATA; id->op = ZOP_POP;
			id->mode = ZM_IR; id->ra = n2; id->rb = n3;
			return (1);
		case 0x18:	/* MULTL */
		case 0x19:	/* MULT */
		case 0x1A:	/* DIVL */
		case 0x1B:	/* DIV */
			id->width = (hi & 1) ? ZW_W : ZW_L;
			if (n2 == 0) {
				id->len = (id->width == ZW_L) ? 3 : 2;
				return (id->len);
			}
			id->klass = ZK_DATA; id->mode = ZM_IR;
			id->op = (hi <= 0x19) ? ZOP_MUL : ZOP_DIV;
			return (1);
		case 0x1C:
			if (n3 == 0x1 || n3 == 0x9) {	/* LDM @Rs */
				id->len = 2; id->klass = ZK_DATA;
				id->op = (n3 == 0x1) ? ZOP_LDM : ZOP_STM;
				id->mode = ZM_IR;
				return (2);
			}
			if (n3 == 0x8) {		/* TESTL @Rs */
				id->klass = ZK_DATA; id->op = ZOP_TESTM;
				id->width = ZW_L; id->mode = ZM_IR;
				id->aop = A_TEST;
				return (1);
			}
			id->klass = ZK_ILL;
			return (1);
		case 0x1D:	/* LDL @Rd,RRs (store) */
			id->klass = ZK_DATA; id->op = ZOP_STORE;
			id->width = ZW_L; id->mode = ZM_IR;
			return (1);
		case 0x1E:	/* JP cc,@Rs */
		case 0x1F:	/* CALL @Rs */
			return (1);
		}
		return (1);

	case 0x2:
		switch (hi) {
		case 0x20: case 0x21:	/* LDB/LD Rd, imm|@Rs */
			id->width = (hi & 1) ? ZW_W : ZW_B;
			if (n2 == 0) {
				id->len = 2;
				return (2);
			}
			id->klass = ZK_DATA; id->op = ZOP_LOAD;
			id->mode = ZM_IR;
			return (1);
		case 0x22: case 0x23: case 0x24:
		case 0x25: case 0x26: case 0x27:
			/* RES/SET/BIT b/w: n2==0 dynamic (register dst),
			 * else static on @Rd */
			id->width = (hi & 1) ? ZW_W : ZW_B;
			if (n2 == 0) {
				id->len = 2;
				return (2);
			}
			id->klass = ZK_DATA; id->mode = ZM_IR;
			switch (hi) {
			case 0x22: case 0x23:
				id->op = ZOP_ALUM; id->aop = A_RES; break;
			case 0x24: case 0x25:
				id->op = ZOP_ALUM; id->aop = A_SET; break;
			default:
				id->op = ZOP_TESTM; id->aop = A_BIT; break;
			}
			return (1);
		case 0x28: case 0x29: case 0x2A: case 0x2B:
			/* INCB/INC/DECB/DEC @Rd,#n */
			id->width = (hi & 1) ? ZW_W : ZW_B;
			id->klass = ZK_DATA; id->op = ZOP_ALUM;
			id->mode = ZM_IR;
			id->aop = (hi <= 0x29) ? A_INC : A_DEC;
			return (1);
		case 0x2C: case 0x2D:	/* EXB/EX Rd,@Rs */
			id->width = (hi & 1) ? ZW_W : ZW_B;
			id->klass = ZK_DATA; id->op = ZOP_EX;
			id->mode = ZM_IR;
			return (1);
		case 0x2E: case 0x2F:	/* LDB/LD @Rd,Rs (store) */
			id->width = (hi & 1) ? ZW_W : ZW_B;
			id->klass = ZK_DATA; id->op = ZOP_STORE;
			id->mode = ZM_IR;
			return (1);
		}
		return (1);

	case 0x3:
		switch (hi) {
		case 0x30: case 0x31: case 0x35:
			/* n2==0: LDRB/LDR/LDRL Rd,disp (program space);
			 * else LDB/LD/LDL Rd, Rs(#disp) */
			id->width = (hi == 0x30) ? ZW_B :
				    (hi == 0x31) ? ZW_W : ZW_L;
			id->len = 2;
			if (n2 == 0) {
				id->klass = ZK_PROG;
				return (2);
			}
			id->klass = ZK_DATA; id->op = ZOP_LOAD;
			id->mode = ZM_BA;
			return (2);
		case 0x32: case 0x33: case 0x37:
			/* n2==0: LDRB/LDR/LDRL disp,Rs (program space);
			 * else LDB/LD/LDL Rd(#disp), Rs */
			id->width = (hi == 0x32) ? ZW_B :
				    (hi == 0x33) ? ZW_W : ZW_L;
			id->len = 2;
			if (n2 == 0) {
				id->klass = ZK_PROG;
				return (2);
			}
			id->klass = ZK_DATA; id->op = ZOP_STORE;
			id->mode = ZM_BA;
			return (2);
		case 0x34:	/* LDAR/LDA: address computation only */
		case 0x36:	/* unassigned */
			id->len = 2;
			if (hi == 0x36)
				id->klass = ZK_ILL;
			return (2);
		case 0x38:	/* unassigned */
			id->klass = ZK_ILL;
			return (1);
		case 0x39:	/* LDPS @Rs: privileged */
			return (1);
		case 0x3A: case 0x3B:	/* block/special I/O: privileged */
			id->len = 2;
			return (2);
		default:	/* 0x3C-0x3F IN/OUT reg: privileged */
			return (1);
		}

	case 0x4:
		if (hi <= 0x4B) {	/* ADD/SUB/OR/AND/XOR/CP b/w DA|X */
			id->width = (hi & 1) ? ZW_W : ZW_B;
			id->len = 2;
			id->klass = ZK_DATA; id->op = ZOP_ALUR;
			id->mode = (n2 == 0) ? ZM_DA : ZM_X;
			switch ((hi - 0x40) >> 1) {
			case 0: id->aop = A_ADD; break;
			case 1: id->aop = A_SUB; break;
			case 2: id->aop = A_OR;  break;
			case 3: id->aop = A_AND; break;
			case 4: id->aop = A_XOR; break;
			case 5: id->aop = A_CP;  break;
			}
			return (2);
		}
		if (hi == 0x4C || hi == 0x4D) {	/* DA|X unary group */
			id->width = (hi == 0x4C) ? ZW_B : ZW_W;
			id->mode = (n2 == 0) ? ZM_DA : ZM_X;
			id->len = 2;
			switch (n3) {
			case 0x0:
				id->klass = ZK_DATA; id->op = ZOP_ALUM;
				id->aop = A_COM; return (2);
			case 0x1:
				id->len = 3; id->klass = ZK_DATA;
				id->op = ZOP_TESTM; id->aop = A_CPIMM;
				return (3);
			case 0x2:
				id->klass = ZK_DATA; id->op = ZOP_ALUM;
				id->aop = A_NEG; return (2);
			case 0x4:
				id->klass = ZK_DATA; id->op = ZOP_TESTM;
				id->aop = A_TEST; return (2);
			case 0x5:
				id->len = 3; id->klass = ZK_DATA;
				id->op = ZOP_STIMM; return (3);
			case 0x6:
				id->klass = ZK_DATA; id->op = ZOP_ALUM;
				id->aop = A_TSET; return (2);
			case 0x8:
				id->klass = ZK_DATA; id->op = ZOP_CLR;
				return (2);
			}
			id->klass = ZK_ILL;
			return (2);
		}
		/* 0x4E/0x4F: EPA memory forms (no EPU; trap policy) */
		id->len = 2;
		return (2);

	case 0x5:
		switch (hi) {
		case 0x50: case 0x52: case 0x54: case 0x56:
			/* CPL/SUBL/LDL/ADDL RRd, DA|X */
			id->width = ZW_L; id->len = 2;
			id->klass = ZK_DATA;
			id->mode = (n2 == 0) ? ZM_DA : ZM_X;
			switch (hi) {
			case 0x50: id->op = ZOP_ALUR; id->aop = A_CP; break;
			case 0x52: id->op = ZOP_ALUR; id->aop = A_SUB; break;
			case 0x54: id->op = ZOP_LOAD; break;
			case 0x56: id->op = ZOP_ALUR; id->aop = A_ADD; break;
			}
			return (2);
		case 0x51: case 0x53: case 0x55: case 0x57:
			/* PUSHL/PUSH/POPL/POP @Rd, DA|X(Rx): sp reg is
			 * nib2; the index register of the X form is nib3 */
			id->len = 2; id->klass = ZK_DATA;
			id->width = (hi == 0x51 || hi == 0x55) ? ZW_L : ZW_W;
			id->op = (hi <= 0x53) ? ZOP_PUSH : ZOP_POP;
			id->ra = n2;
			id->rb = n3;
			id->mode = (n3 == 0) ? ZM_DA : ZM_X;
			return (2);
		case 0x58: case 0x59: case 0x5A: case 0x5B:
			/* MULTL/MULT/DIVL/DIV DA|X */
			id->width = (hi & 1) ? ZW_W : ZW_L;
			id->len = 2; id->klass = ZK_DATA;
			id->mode = (n2 == 0) ? ZM_DA : ZM_X;
			id->op = (hi <= 0x59) ? ZOP_MUL : ZOP_DIV;
			return (2);
		case 0x5C:
			if (n3 == 0x1 || n3 == 0x9) {	/* LDM DA|X */
				id->len = 3; id->klass = ZK_DATA;
				id->op = (n3 == 0x1) ? ZOP_LDM : ZOP_STM;
				id->mode = (n2 == 0) ? ZM_DA : ZM_X;
				return (3);
			}
			if (n3 == 0x8) {		/* TESTL DA|X */
				id->len = 2; id->klass = ZK_DATA;
				id->op = ZOP_TESTM; id->width = ZW_L;
				id->aop = A_TEST;
				id->mode = (n2 == 0) ? ZM_DA : ZM_X;
				return (2);
			}
			id->len = 2; id->klass = ZK_ILL;
			return (2);
		case 0x5D:	/* LDL DA|X, RRs (store) */
			id->width = ZW_L; id->len = 2;
			id->klass = ZK_DATA; id->op = ZOP_STORE;
			id->mode = (n2 == 0) ? ZM_DA : ZM_X;
			return (2);
		case 0x5E:	/* JP cc, DA|X */
		case 0x5F:	/* CALL DA|X */
			id->len = 2;
			return (2);
		}
		return (1);

	case 0x6:
		switch (hi) {
		case 0x60: case 0x61:	/* LDB/LD Rd, DA|X */
			id->width = (hi & 1) ? ZW_W : ZW_B;
			id->len = 2; id->klass = ZK_DATA;
			id->op = ZOP_LOAD;
			id->mode = (n2 == 0) ? ZM_DA : ZM_X;
			return (2);
		case 0x62: case 0x63: case 0x64:
		case 0x65: case 0x66: case 0x67:
			/* RES/SET/BIT b/w DA|X */
			id->width = (hi & 1) ? ZW_W : ZW_B;
			id->len = 2; id->klass = ZK_DATA;
			id->mode = (n2 == 0) ? ZM_DA : ZM_X;
			switch (hi) {
			case 0x62: case 0x63:
				id->op = ZOP_ALUM; id->aop = A_RES; break;
			case 0x64: case 0x65:
				id->op = ZOP_ALUM; id->aop = A_SET; break;
			default:
				id->op = ZOP_TESTM; id->aop = A_BIT; break;
			}
			return (2);
		case 0x68: case 0x69: case 0x6A: case 0x6B:
			/* INCB/INC/DECB/DEC DA|X */
			id->width = (hi & 1) ? ZW_W : ZW_B;
			id->len = 2; id->klass = ZK_DATA;
			id->op = ZOP_ALUM;
			id->mode = (n2 == 0) ? ZM_DA : ZM_X;
			id->aop = (hi <= 0x69) ? A_INC : A_DEC;
			return (2);
		case 0x6C: case 0x6D:	/* EXB/EX Rd, DA|X */
			id->width = (hi & 1) ? ZW_W : ZW_B;
			id->len = 2; id->klass = ZK_DATA;
			id->op = ZOP_EX;
			id->mode = (n2 == 0) ? ZM_DA : ZM_X;
			return (2);
		case 0x6E: case 0x6F:	/* LDB/LD DA|X, Rs (store) */
			id->width = (hi & 1) ? ZW_W : ZW_B;
			id->len = 2; id->klass = ZK_DATA;
			id->op = ZOP_STORE;
			id->mode = (n2 == 0) ? ZM_DA : ZM_X;
			return (2);
		}
		return (1);

	case 0x7:
		switch (hi) {
		case 0x70: case 0x71: case 0x75:
			/* LDB/LD/LDL Rd, Rs(Rx) */
			id->width = (hi == 0x70) ? ZW_B :
				    (hi == 0x71) ? ZW_W : ZW_L;
			id->len = 2; id->klass = ZK_DATA;
			id->op = ZOP_LOAD; id->mode = ZM_BX;
			return (2);
		case 0x72: case 0x73: case 0x77:
			/* LDB/LD/LDL Rd(Rx), Rs */
			id->width = (hi == 0x72) ? ZW_B :
				    (hi == 0x73) ? ZW_W : ZW_L;
			id->len = 2; id->klass = ZK_DATA;
			id->op = ZOP_STORE; id->mode = ZM_BX;
			return (2);
		case 0x74: case 0x76:	/* LDA: address computation only */
			id->len = 2;
			return (2);
		case 0x78:	/* unassigned */
			id->klass = ZK_ILL;
			return (1);
		case 0x79:	/* LDPS DA|X: privileged */
			id->len = 2;
			return (2);
		case 0x7F:
			id->op = ZOP_SC;
			return (1);
		default:
			/* 0x7A HALT, 0x7B IRET/M*, 0x7C DI/EI, 0x7D LDCTL,
			 * 0x7E unassigned: privileged / no data access */
			return (1);
		}

	case 0x8:
		if (hi == 0x8E || hi == 0x8F) {	/* EPA internal (fldctl...) */
			id->len = 2;
			return (2);
		}
		return (1);	/* register-register ALU / unary / flags */

	case 0x9:
	case 0xA:
		return (1);	/* register forms, PUSH/POP reg, RET, TCC */

	case 0xB:
		switch (hi) {
		case 0xB2:	/* byte shifts (2 words) vs rotates (1) */
			if (n3 == 0x1 || n3 == 0x3 || n3 == 0x9 || n3 == 0xB)
				id->len = 2;
			return (id->len);
		case 0xB3:	/* word/long shifts (2 words) vs rotates */
			if (n3 & 1)
				id->len = 2;
			return (id->len);
		case 0xB8:	/* TRxB/TRTxB translate: even lo bytes only;
				 * (lo>>1) bit0 = TRT (test), bit1 = repeat,
				 * bit2 = decrement */
			id->len = 2;
			if (w0 & 1) {
				id->klass = ZK_ILL;
				return (2);
			}
			id->klass = ZK_DATA; id->op = ZOP_TRANS;
			id->width = ZW_B;
			if ((w0 >> 1) & 0x4) id->blk |= ZB_DECR;
			if ((w0 >> 1) & 0x2) id->blk |= ZB_REPT;
			id->aop = ((w0 >> 1) & 1) ? A_TEST : 0;
			return (2);
		case 0xBA: case 0xBB:	/* block compare/transfer/string:
				 * nib3 selects the family (>= 8 decrements);
				 * for LDx, word-1 bit 3 clear = repeat form */
			id->len = 2;
			id->width = (hi == 0xBB) ? ZW_W : ZW_B;
			id->klass = ZK_DATA;
			if (n3 >= 0x8)
				id->blk |= ZB_DECR;
			switch (n3 & 7) {
			case 0x0:	/* CPI/CPD */
				id->op = ZOP_BLKC;
				break;
			case 0x4:	/* CPIR/CPDR */
				id->op = ZOP_BLKC;
				id->blk |= ZB_REPT;
				break;
			case 0x1:	/* LDIR/LDI (LDDR/LDD) */
				id->op = ZOP_BLKT;
				if ((w1 & 0x08) == 0)
					id->blk |= ZB_REPT;
				break;
			case 0x2:	/* CPSI/CPSD */
				id->op = ZOP_BLKS;
				break;
			case 0x6:	/* CPSIR/CPSDR */
				id->op = ZOP_BLKS;
				id->blk |= ZB_REPT;
				break;
			default:
				id->klass = ZK_ILL;
				break;
			}
			return (2);
		default:
			/* B0 DAB, B1 EXTS, B4-B7 ADC/SBC, B9 unassigned,
			 * BC/BE RRDB/RLDB, BD LDK: register only */
			if (hi == 0xB9)
				id->klass = ZK_ILL;
			return (1);
		}

	default:
		/* 0xC0-0xCF LDB imm8; 0xD0-0xDF CALR; 0xE0-0xEF JR;
		 * 0xF0-0xFF DJNZ: no data-space memory operand */
		return (1);
	}
}

/*
 * Fast-classify table for the scanner, indexed by the instruction's high
 * byte (ZQ_xxx encoding in zsplit.h): fixed skip lengths for the
 * no-reference families, direct ZK_DATA patch actions for the fixed-shape
 * data families, nibble-2-gated variants for the immediate/IR and
 * LDR/base-address pairs, and ZQ_FULL where only zdecode() can tell.
 * (Derived from the zdecode() cases above; splitchk.c cross-verifies
 * the two agree for every opcode.)
 */
char zqk[256] = {
/* 00 */ 6,6,6,6, 6,6,6,6, 6,6,6,6, 0,0,1,1,
/* 10 */ 7,4,7,4, 7,4,7,4, 7,6,7,6, 0,4,1,1,
/* 20 */ 6,6,6,6, 6,6,6,6, 4,4,4,4, 4,4,4,4,
/* 30 */ 8,8,8,8, 2,8,0,8, 0,1,2,2, 1,1,1,1,
/* 40 */ 5,5,5,5, 5,5,5,5, 5,5,5,5, 0,0,2,2,
/* 50 */ 5,5,5,5, 5,5,5,5, 5,5,5,5, 0,5,2,2,
/* 60 */ 5,5,5,5, 5,5,5,5, 5,5,5,5, 5,5,5,5,
/* 70 */ 5,5,5,5, 2,5,2,5, 0,2,1,1, 1,1,1,1,
/* 80 */ 1,1,1,1, 1,1,1,1, 1,1,1,1, 1,1,2,2,
/* 90 */ 1,1,1,1, 1,1,1,1, 1,1,1,1, 1,1,1,1,
/* A0 */ 1,1,1,1, 1,1,1,1, 1,1,1,1, 1,1,1,1,
/* B0 */ 1,1,0,0, 1,1,1,1, 0,0,0,0, 1,1,1,0,
/* C0 */ 1,1,1,1, 1,1,1,1, 1,1,1,1, 1,1,1,1,
/* D0 */ 1,1,1,1, 1,1,1,1, 1,1,1,1, 1,1,1,1,
/* E0 */ 1,1,1,1, 1,1,1,1, 1,1,1,1, 1,1,1,1,
/* F0 */ 1,1,1,1, 1,1,1,1, 1,1,1,1, 1,1,1,1,
};

/*
 * Single-pass scan: walk the text linearly.  Every patchable (ZK_DATA)
 * instruction is reported through fn(arg, off, w0, idp) as it is met;
 * every word an LDR-family instruction references (a program-space
 * constant embedded in text) is marked in cp->mark.  Because an LDR may
 * point backward OR forward, a marked word may already have been
 * patched when the mark lands -- the caller runs zsfix() afterwards to
 * revert any patch on a marked word.  Returns the number of ZK_ILL
 * encodings met (nonzero = the walk desynchronized somewhere; the
 * offline harness investigates).
 *
 * Most instructions classify through the zqk table alone; a ZK_DATA
 * site reported from the table path carries an id with only len and
 * klass valid (see the ZQ_xxx note in zsplit.h).  zdecode() runs only
 * for the ZQ_FULL families and the n2 == 0 LDR members of ZQ_B2.
 */
int zscan(cp, fn, arg)
struct zsctx *cp;
int (*fn)();
char *arg;
{
	struct zid in;
	register zw off, nw;
	register short ill;
	register zw *tx;
	register zw t;
	register short q;
	long tgt;

	ill = 0;
	off = 0;
	nw = cp->nw;
	tx = cp->text;
	while (off < nw) {
		t = tx[off];
		q = zqk[(t >> 8) & 0xff];
		if (q >= ZQ_D1) {	/* table-classified ZK_DATA family */
			if (q == ZQ_D2)
				in.len = 2;
			else if (q == ZQ_D1)
				in.len = 1;
			else if (q == ZQ_B2) {
				if ((t & 0x00F0) == 0)
					goto full;	/* LDR: ZK_PROG */
				in.len = 2;
			} else {	/* ZQ_I2D1 / ZQ_I3D1 */
				if ((t & 0x00F0) == 0) {
					off += (q == ZQ_I2D1) ? 2 : 3;
					continue;
				}
				in.len = 1;
			}
			in.klass = ZK_DATA;
			if ((cp->mark[off >> 3] & (1 << (off & 7))) == 0)
				(*fn)(arg, off, t, &in);
			off += in.len;
			continue;
		}
		if (q) {		/* no reference: skip q words */
			off += q;
			continue;
		}
full:
		zdecode(t, (zw)(off + 1 < nw ? tx[off + 1] : 0), &in);
		if (in.klass == ZK_ILL)
			ill++;
		else if (in.klass == ZK_PROG) {
			/* target = byte address of insn + 4 + disp16 */
			tgt = ((long)off << 1) + 4 + (long)(short)tx[off + 1];
			if (tgt >= 0 && (tgt >> 1) < (long)nw) {
				zw tw;
				tw = (zw)(tgt >> 1);
				cp->mark[tw >> 3] |= 1 << (tw & 7);
				if (in.width == ZW_L && tw + 1 < nw)
					cp->mark[(tw + 1) >> 3] |=
						1 << ((tw + 1) & 7);
			}
		} else if (in.klass == ZK_DATA &&
		    (cp->mark[off >> 3] & (1 << (off & 7))) == 0)
			(*fn)(arg, off, t, &in);
		off += in.len;
	}
	return (ill);
}

/*
 * Post-pass: report every marked (program-space data) word through
 * ufn(arg, off) so the caller can revert a patch that landed on it
 * before the mark did.  Walks the bitmap bytewise (cheap).
 */
int zsfix(cp, ufn, arg)
struct zsctx *cp;
int (*ufn)();
char *arg;
{
	register zw i, nb;
	register short b;

	nb = (cp->nw + 7) >> 3;
	for (i = 0; i < nb; i++) {
		if (cp->mark[i] == 0)
			continue;
		for (b = 0; b < 8; b++)
			if (cp->mark[i] & (1 << b))
				(*ufn)(arg, (zw)((i << 3) | b));
	}
	return (0);
}
