/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/* 8080/Z80 decoder shared by execution and instruction inspection.
 * Prefix groups retain their complete lengths even when execution is
 * unsupported. Guest instruction fetch wraps at 16 bits. */

#include "z80.h"

/* Fetch the k'th byte of the instruction at pc.  The (z16) cast is the
 * wrap: an instruction straddling 0xFFFF continues at 0. */
static int fb(m, pc, k)
char *m;
z16 pc;
int k;
{
	return (m[(z16)(pc + k)] & 0xff);
}

/* Sign-extend a byte without assuming char's signedness -- MWC and the
 * host disagree about it and this file must not. */
static z16 sx(b)
int b;
{
	b &= 0xff;
	return ((z16)(b & 0x80 ? (0xff00 | b) : b));
}

static z16 iw(m, pc, k)
char *m;
z16 pc;
int k;
{
	return ((z16)(fb(m, pc, k) | (fb(m, pc, k + 1) << 8)));
}

/*
 * Base-map lengths, transcribed from the encoding rather than from a
 * table someone else built.  Three rules cover all 256:
 *
 *   3 bytes -- an instruction carrying a 16-bit address or datum:
 *		LXI (x1), the four direct loads/stores (22 2A 32 3A),
 *		JMP/Jcc (xx010 xx011), CALL/Ccc (xx100 CD).
 *   2 bytes -- an 8-bit immediate: MVI (xx110), the ALU-immediate eight
 *		(11xxx110), IN, OUT, and the Z80 relative branches
 *		(10 18 20 28 30 38), whose operand is a displacement.
 *   1 byte  -- everything else.
 */
static int baselen(op)
int op;
{
	switch (op) {
	case 0x01: case 0x11: case 0x21: case 0x31:	/* LXI rp,nn	*/
	case 0x22: case 0x2a: case 0x32: case 0x3a:	/* SHLD LHLD STA LDA */
	case 0xc3: case 0xcd:				/* JMP, CALL	*/
	case 0xc2: case 0xca: case 0xd2: case 0xda:	/* Jcc		*/
	case 0xe2: case 0xea: case 0xf2: case 0xfa:
	case 0xc4: case 0xcc: case 0xd4: case 0xdc:	/* Ccc		*/
	case 0xe4: case 0xec: case 0xf4: case 0xfc:
		return (3);
	case 0x06: case 0x0e: case 0x16: case 0x1e:	/* MVI r,n	*/
	case 0x26: case 0x2e: case 0x36: case 0x3e:
	case 0xc6: case 0xce: case 0xd6: case 0xde:	/* ALU A,n	*/
	case 0xe6: case 0xee: case 0xf6: case 0xfe:
	case 0xd3: case 0xdb:				/* OUT n, IN n	*/
	case 0x10:					/* DJNZ e	*/
	case 0x18: case 0x20: case 0x28: case 0x30: case 0x38:	/* JR	*/
		return (2);
	}
	return (1);
}

/*
 * Does this base opcode name the memory byte at (HL)?
 *
 * It matters twice.  Here, because under a DD/FD prefix every one of
 * these grows a signed displacement byte -- (HL) becomes (IX+d) -- and
 * that is the ONLY thing the prefix does to the length.  And in the
 * executor, because r = 6 is a memory reference and not a register.
 *
 * The set is: INR/DCR/MVI with r = 6 (34 35 36), MOV r,M (x1xxx110 with
 * the source field 6: 46 4E 56 5E 66 6E 7E), MOV M,r (70-77 less 76,
 * which is HLT and is not a move at all), and the ALU eight against M
 * (86 8E 96 9E A6 AE B6 BE).
 */
static int usesm(op)
int op;
{
	if (op == 0x34 || op == 0x35 || op == 0x36)
		return (1);
	if (op >= 0x70 && op <= 0x77)
		return (op != 0x76);		/* 76 is HLT		*/
	if (op >= 0x40 && op <= 0x7f)
		return ((op & 7) == 6);
	if (op >= 0x80 && op <= 0xbf)
		return ((op & 7) == 6);
	return (0);
}

/*
 * ED-group lengths.  Two bytes, except the four LD (nn),dd and four
 * LD dd,(nn) forms, which carry a 16-bit address: ED 43/53/63/73 store
 * and ED 4B/5B/6B/7B load.  Everything else in the group -- the block
 * moves, the 16-bit ADC/SBC, NEG, RETN, IM, the I/R transfers, and every
 * undefined second byte -- is two.
 */
static int edlen(sub)
int sub;
{
	switch (sub) {
	case 0x43: case 0x4b: case 0x53: case 0x5b:
	case 0x63: case 0x6b: case 0x73: case 0x7b:
		return (4);
	}
	return (2);
}

/*
 * z80dec -- decode the instruction at m[pc] into *in.
 *
 * Returns the instruction length in bytes.  On a byte that cannot begin
 * an instruction, sets in->op = Z_BAD and returns the length consumed so
 * far (at least 1) so that a linear sweep can step past it -- a sweep
 * that stopped dead at the first table byte would report one finding and
 * hide the rest.
 *
 * On the base map there is no such byte: all 256 decode.  Z_BAD is
 * reachable only through the ED escape, where our own hook occupies a
 * second byte the hardware leaves undefined.
 */
int z80dec(m, pc, in)
char *m;
z16 pc;
struct z80in *in;
{
	register int op, sub;
	register int n;

	in->op = Z_BAD;
	in->fl = 0;
	in->x = 0;
	in->y = 0;
	in->pfx = 0;
	in->sub = 0;
	in->imm = 0;
	in->disp = 0;

	op = fb(m, pc, 0);
	n = baselen(op);

	/* ---- the four prefixes, first, because everything they cover
	 * is a different opcode space with a different length rule. */
	if (op == 0xcb) {
		/* CB is the one prefix group with a SINGLE regular
		 * encoding: two bits of group, three of operation or bit
		 * number, three of register, always two bytes.  So the
		 * fields are pulled apart here rather than in the
		 * executor, on the rule the head comment states -- one
		 * decode serves three consumers, and a second copy of
		 * this shift-and-mask in z80exec.c would be a second
		 * thing to get wrong.
		 *
		 *	.x   0 rotate/shift, 1 BIT, 2 RES, 3 SET
		 *	.y   the register, 6 meaning the byte at (HL)
		 *	.imm the rotate operation 0..7, or the bit 0..7 */
		sub = fb(m, pc, 1);
		in->op = Z_CB;
		in->fl |= ZF_PFX;
		in->pfx = (z8)op;
		in->sub = (z8)sub;
		in->x = (z8)((sub >> 6) & 3);
		in->y = (z8)(sub & 7);
		in->imm = (z16)((sub >> 3) & 7);
		if (in->y == R_M)
			in->fl |= ZF_MEM;
		n = 2;
		goto done;
	}
	if (op == 0xed) {
		sub = fb(m, pc, 1);
		in->fl |= ZF_PFX;
		in->pfx = (z8)op;
		in->sub = (z8)sub;
		/* OUR escape.  ED FE is undefined on the part -- the Z80
		 * executes an undefined ED pair as two NOPs and no
		 * assembler emits one -- so it can be spent on the shim's
		 * BDOS and BIOS entry points without taking an encoding
		 * away from the guest.  See the HOOK_* comment in z80.h
		 * for why the study's choice of an 8080-undefined byte
		 * does not survive contact with the Z80. */
		if (sub == 0xfe) {
			in->op = Z_HOOK;
			in->x = (z8)fb(m, pc, 2);
			n = 3;
			if (in->x > HOOK_MAX)
				in->op = Z_BAD;
			goto done;
		}
		in->op = Z_ED;
		n = edlen(sub);
		/* The eight LD (nn),dd and LD dd,(nn) forms are the only
		 * members of the group that carry an operand, and they
		 * carry the same one the base map's SHLD/LHLD do.  .x is
		 * the register pair in the ordinary rp encoding, so
		 * ED 7B is `LD SP,(nn)' with .x = P_SP and needs no
		 * special case in the executor. */
		if (n == 4) {
			in->x = (z8)((sub >> 4) & 3);
			in->imm = iw(m, pc, 2);
			in->fl |= ZF_IMM | ZF_ADDR | ZF_MEM;
		}
		goto done;
	}
	if (op == 0xdd || op == 0xfd) {
		sub = fb(m, pc, 1);
		in->fl |= ZF_PFX;
		in->pfx = (z8)op;
		in->sub = (z8)sub;
		if (sub == 0xdd || sub == 0xfd || sub == 0xed) {
			/* A prefix followed by another prefix is DISCARDED
			 * by the hardware: DD DD 21 nn nn is `LD IX,nn'
			 * with the first DD costing four T-states and
			 * nothing else, and DD ED B0 is a plain LDIR.  So
			 * the length here is ONE, not two -- the next
			 * decode has to start on the second prefix or the
			 * stream desynchronises.  This is the one length
			 * rule in the architecture that a table of
			 * "prefix plus opcode" gets wrong, and getting it
			 * wrong is silent: DD ED B0 read as a two-byte
			 * instruction leaves B0 to decode as ORA B. */
			in->op = Z_IX;
			n = 1;
			goto done;
		}
		if (sub == 0xcb) {
			/* DD CB d op: the displacement comes BEFORE the
			 * operation byte, which is the one length rule in
			 * the architecture that reads backwards. */
			in->op = Z_IXCB;
			in->disp = sx(fb(m, pc, 2));
			n = 4;
			goto done;
		}
		in->op = Z_IX;
		n = 1 + baselen(sub) + (usesm(sub) ? 1 : 0);
		if (usesm(sub))
			in->disp = sx(fb(m, pc, 2));
		goto done;
	}

	/* ---- 40-7F: MOV, and HLT where MOV M,M would be. */
	if (op >= 0x40 && op <= 0x7f) {
		if (op == 0x76) {
			in->op = Z_HLT;
			goto done;
		}
		in->op = Z_LDRR;
		in->x = (z8)((op >> 3) & 7);
		in->y = (z8)(op & 7);
		if (in->x == R_M || in->y == R_M)
			in->fl |= ZF_MEM;
		goto done;
	}

	/* ---- 80-BF: the ALU eight against a register or (HL). */
	if (op >= 0x80 && op <= 0xbf) {
		in->op = Z_ALU;
		in->x = (z8)((op >> 3) & 7);
		in->y = (z8)(op & 7);
		if (in->y == R_M)
			in->fl |= ZF_MEM;
		goto done;
	}

	switch (op) {

	case 0x00: in->op = Z_NOP; break;

	/* ---- 01/11/21/31 and the register-pair one-byte forms. */
	case 0x01: case 0x11: case 0x21: case 0x31:
		in->op = Z_LXI;
		in->x = (z8)((op >> 4) & 3);
		in->imm = iw(m, pc, 1);
		in->fl |= ZF_IMM;
		break;
	case 0x09: case 0x19: case 0x29: case 0x39:
		in->op = Z_DAD; in->x = (z8)((op >> 4) & 3);
		break;
	case 0x03: case 0x13: case 0x23: case 0x33:
		in->op = Z_INX; in->x = (z8)((op >> 4) & 3);
		break;
	case 0x0b: case 0x1b: case 0x2b: case 0x3b:
		in->op = Z_DCX; in->x = (z8)((op >> 4) & 3);
		break;

	case 0x02: case 0x12:
		in->op = Z_STAX; in->x = (z8)((op >> 4) & 1);
		in->fl |= ZF_MEM;
		break;
	case 0x0a: case 0x1a:
		in->op = Z_LDAX; in->x = (z8)((op >> 4) & 1);
		in->fl |= ZF_MEM;
		break;
	case 0x22: in->op = Z_SHLD; goto addr;
	case 0x2a: in->op = Z_LHLD; goto addr;
	case 0x32: in->op = Z_STA;  goto addr;
	case 0x3a: in->op = Z_LDA;
	addr:
		in->imm = iw(m, pc, 1);
		in->fl |= ZF_IMM | ZF_ADDR | ZF_MEM;
		break;

	/* ---- INR/DCR/MVI: the destination is the middle field. */
	case 0x04: case 0x0c: case 0x14: case 0x1c:
	case 0x24: case 0x2c: case 0x34: case 0x3c:
		in->op = Z_INR;
		in->x = (z8)((op >> 3) & 7);
		if (in->x == R_M)
			in->fl |= ZF_MEM;
		break;
	case 0x05: case 0x0d: case 0x15: case 0x1d:
	case 0x25: case 0x2d: case 0x35: case 0x3d:
		in->op = Z_DCR;
		in->x = (z8)((op >> 3) & 7);
		if (in->x == R_M)
			in->fl |= ZF_MEM;
		break;
	case 0x06: case 0x0e: case 0x16: case 0x1e:
	case 0x26: case 0x2e: case 0x36: case 0x3e:
		in->op = Z_LDRI;
		in->x = (z8)((op >> 3) & 7);
		in->imm = (z16)fb(m, pc, 1);
		in->fl |= ZF_IMM;
		if (in->x == R_M)
			in->fl |= ZF_MEM;
		break;

	case 0x07: case 0x0f: case 0x17: case 0x1f:
		in->op = Z_ROT;
		in->x = (z8)((op >> 3) & 3);
		break;

	case 0x27: in->op = Z_DAA; break;
	case 0x2f: in->op = Z_CMA; break;
	case 0x37: in->op = Z_STC; break;
	case 0x3f: in->op = Z_CMC; break;

	/* ---- the Z80 base-map opcodes an 8080 leaves undefined.
	 * Z80-STAGE-ONE.md §1.2 measures that DRI's own CP/M 3 corpus
	 * reaches these and nothing else in the Z80 -- 395 JR, one DJNZ,
	 * two EX AF,AF' and no EXX across 50,800 statically reachable
	 * instructions in 22 binaries, and zero CB, ED, DD or FD.  That
	 * measurement is the reason they are executable here and the
	 * prefix groups are not. */
	case 0x08: in->op = Z_EXAF; break;
	case 0xd9: in->op = Z_EXX; break;
	case 0x10:
		in->op = Z_DJNZ;
		in->imm = (z16)(sx(fb(m, pc, 1)) + pc + 2);
		break;
	case 0x18:
		in->op = Z_JR; in->x = 4;		/* unconditional */
		in->imm = (z16)(sx(fb(m, pc, 1)) + pc + 2);
		break;
	case 0x20: case 0x28: case 0x30: case 0x38:
		in->op = Z_JR;
		in->x = (z8)((op >> 3) & 3);		/* NZ Z NC C	*/
		in->imm = (z16)(sx(fb(m, pc, 1)) + pc + 2);
		break;

	/* ---- C0-FF: the control-flow block, plus the odds and ends. */
	case 0xc0: case 0xc8: case 0xd0: case 0xd8:
	case 0xe0: case 0xe8: case 0xf0: case 0xf8:
		in->op = Z_RCC; in->x = (z8)((op >> 3) & 7);
		break;
	case 0xc9: in->op = Z_RET; break;
	case 0xc1: case 0xd1: case 0xe1: case 0xf1:
		in->op = Z_POP; in->x = (z8)((op >> 4) & 3);
		break;
	case 0xc5: case 0xd5: case 0xe5: case 0xf5:
		in->op = Z_PUSH; in->x = (z8)((op >> 4) & 3);
		break;
	case 0xc2: case 0xca: case 0xd2: case 0xda:
	case 0xe2: case 0xea: case 0xf2: case 0xfa:
		in->op = Z_JCC; in->x = (z8)((op >> 3) & 7);
		in->imm = iw(m, pc, 1);
		in->fl |= ZF_ADDR;
		break;
	case 0xc3:
		in->op = Z_JMP; in->imm = iw(m, pc, 1); in->fl |= ZF_ADDR;
		break;
	case 0xc4: case 0xcc: case 0xd4: case 0xdc:
	case 0xe4: case 0xec: case 0xf4: case 0xfc:
		in->op = Z_CCC; in->x = (z8)((op >> 3) & 7);
		in->imm = iw(m, pc, 1);
		in->fl |= ZF_ADDR;
		break;
	case 0xcd:
		in->op = Z_CALL; in->imm = iw(m, pc, 1); in->fl |= ZF_ADDR;
		break;
	case 0xc6: case 0xce: case 0xd6: case 0xde:
	case 0xe6: case 0xee: case 0xf6: case 0xfe:
		in->op = Z_ALU;
		in->x = (z8)((op >> 3) & 7);
		in->imm = (z16)fb(m, pc, 1);
		in->fl |= ZF_IMM;
		break;
	case 0xc7: case 0xcf: case 0xd7: case 0xdf:
	case 0xe7: case 0xef: case 0xf7: case 0xff:
		in->op = Z_RST; in->x = (z8)((op >> 3) & 7);
		break;

	case 0xd3: in->op = Z_OUT; in->imm = (z16)fb(m, pc, 1); break;
	case 0xdb: in->op = Z_IN;  in->imm = (z16)fb(m, pc, 1); break;

	case 0xe3: in->op = Z_XTHL; in->fl |= ZF_MEM; break;
	case 0xe9: in->op = Z_PCHL; break;
	case 0xeb: in->op = Z_XCHG; break;
	case 0xf9: in->op = Z_SPHL; break;
	case 0xf3: in->op = Z_DI; break;
	case 0xfb: in->op = Z_EI; break;

	default:
		/* Unreachable: baselen() and the ranges above between
		 * them account for all 256 base opcodes.  Kept so that a
		 * future edit that removes a case fails loudly here
		 * instead of falling through to Z_NOP. */
		in->op = Z_BAD;
		break;
	}

done:
	if (n < 1)
		n = 1;
	in->len = (z8)n;
	return (n);
}

/*
 * The ED members that have a name here.  The group is sparse and its
 * regularity is in three separate places at once -- the 16-bit
 * arithmetic and the two indirect loads share the 0x40-0x7F block with
 * NEG and the interrupt forms, and the block operations sit in a 4x4
 * grid at 0xA0-0xBB -- so it is written out rather than computed.
 *
 * `cpi' here is the Z80's BLOCK COMPARE, not the 8080's compare
 * immediate: this file spells the latter "cmp" (alu[7]) after the 8080
 * convention the head comment states, so the two never collide in
 * output even though the assembler mnemonics do.
 *
 * Anything not named is "ed-group": decoded for its length, refused by
 * the executor, and reported as a worklist entry rather than run.
 */
static char *edmnem(sub)
int sub;
{
	static char *blk[16] = {
		"ldi",  "cpi",  "ini",  "outi",
		"ldd",  "cpd",  "ind",  "outd",
		"ldir", "cpir", "inir", "otir",
		"lddr", "cpdr", "indr", "otdr"
	};

	if (sub >= 0xa0 && sub <= 0xbb && (sub & 7) < 4)
		return (blk[(((sub - 0xa0) >> 3) << 2) | (sub & 3)]);
	if (sub >= 0x40 && sub <= 0x7f) {
		switch (sub & 15) {
		case 0x02: case 0x0a:
			return ((sub & 8) ? "adc hl" : "sbc hl");
		case 0x03: case 0x0b:
			return ((sub & 8) ? "ld rp,(nn)" : "ld (nn),rp");
		case 0x04: case 0x0c:
			return ("neg");
		}
	}
	return ("ed-group");
}

/*
 * z80mnem -- the mnemonic for a decoded instruction, for the corpus
 * sweep and for a refusal message.  Not on any execution path; kept here
 * so the names sit next to the encodings that produce them.
 *
 * The 8080 spelling is used, not the Zilog one, because that is what
 * every source in this tree is written in and what the study quotes --
 * `mov' not `ld', `jz' not `jp z'.  The four Z80 base opcodes have no
 * 8080 spelling and keep Zilog's.
 */
char *z80mnem(in)
struct z80in *in;
{
	static char *alu[8] = {
		"add", "adc", "sub", "sbb", "ana", "xra", "ora", "cmp"
	};
	static char *cc[8] = {
		"nz", "z", "nc", "c", "po", "pe", "p", "m"
	};
	static char *rot[4] = { "rlc", "rrc", "ral", "rar" };
	static char *jcc[8] = {
		"jnz", "jz", "jnc", "jc", "jpo", "jpe", "jp", "jm"
	};
	static char *ccc[8] = {
		"cnz", "cz", "cnc", "cc", "cpo", "cpe", "cp", "cm"
	};
	static char *rcc[8] = {
		"rnz", "rz", "rnc", "rc", "rpo", "rpe", "rp", "rm"
	};
	static char *jr[5] = { "jrnz", "jrz", "jrnc", "jrc", "jr" };
	static char *cbrot[8] = {
		"rlc", "rrc", "rl", "rr", "sla", "sra", "sll", "srl"
	};
	static char *cbgrp[4] = { "", "bit", "res", "set" };

	switch (in->op) {
	case Z_NOP:	return ("nop");
	case Z_LDRR:	return ("mov");
	case Z_LDRI:	return ("mvi");
	case Z_ALU:	return (alu[in->x & 7]);
	case Z_INR:	return ("inr");
	case Z_DCR:	return ("dcr");
	case Z_LXI:	return ("lxi");
	case Z_DAD:	return ("dad");
	case Z_INX:	return ("inx");
	case Z_DCX:	return ("dcx");
	case Z_LDAX:	return ("ldax");
	case Z_STAX:	return ("stax");
	case Z_LDA:	return ("lda");
	case Z_STA:	return ("sta");
	case Z_LHLD:	return ("lhld");
	case Z_SHLD:	return ("shld");
	case Z_ROT:	return (rot[in->x & 3]);
	case Z_DAA:	return ("daa");
	case Z_CMA:	return ("cma");
	case Z_STC:	return ("stc");
	case Z_CMC:	return ("cmc");
	case Z_JMP:	return ("jmp");
	case Z_JCC:	return (jcc[in->x & 7]);
	case Z_CALL:	return ("call");
	case Z_CCC:	return (ccc[in->x & 7]);
	case Z_RET:	return ("ret");
	case Z_RCC:	return (rcc[in->x & 7]);
	case Z_RST:	return ("rst");
	case Z_PCHL:	return ("pchl");
	case Z_SPHL:	return ("sphl");
	case Z_XTHL:	return ("xthl");
	case Z_XCHG:	return ("xchg");
	case Z_PUSH:	return ("push");
	case Z_POP:	return ("pop");
	case Z_IN:	return ("in");
	case Z_OUT:	return ("out");
	case Z_EI:	return ("ei");
	case Z_DI:	return ("di");
	case Z_HLT:	return ("hlt");
	case Z_JR:	return (jr[in->x < 5 ? in->x : 4]);
	case Z_DJNZ:	return ("djnz");
	case Z_EXAF:	return ("ex af,af'");
	case Z_EXX:	return ("exx");
	case Z_CB:
		/* Zilog's spelling, because the CB group has no 8080
		 * one at all: these encodings do not exist on an 8080.
		 * The four rotate names collide with the base map's
		 * (0x07 is also "rlc") and that is correct -- CB 07 is
		 * `RLC A' and does the same thing to A that 0x07 does,
		 * differing only in the flags. */
		if (in->x == 0)
			return (cbrot[in->imm & 7]);
		return (cbgrp[in->x & 3]);
	case Z_ED:
		return (edmnem((int)in->sub));
	case Z_IX:	return (in->pfx == 0xdd ? "ix-group" : "iy-group");
	case Z_IXCB:	return (in->pfx == 0xdd ? "ixcb-group" : "iycb-group");
	case Z_HOOK:	return ("hook");
	}
	return ("(bad)");
}

/* The condition codes, in the encoding order the opcode's middle field
 * gives: NZ Z NC C PO PE P M.  Exported because the executor's Jcc, Ccc
 * and Rcc all need it and the JR forms need its first four; a second
 * copy would be a second thing to get wrong. */
int z80cond(f, cc)
int f, cc;
{
	register int t;

	switch (cc >> 1) {
	case 0:  t = (f & F_ZE) != 0; break;		/* Z	*/
	case 1:  t = (f & F_CY) != 0; break;		/* CY	*/
	case 2:  t = (f & F_PA) != 0; break;		/* PE	*/
	default: t = (f & F_SI) != 0; break;		/* M	*/
	}
	return ((cc & 1) ? t : !t);
}
