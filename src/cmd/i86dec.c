/* 8086 decoder shared by execution and instruction inspection.
 * Decode register-independent operands and lengths; effective addresses
 * are resolved by the executor. Guest instruction fetch wraps at 16 bits. */

#include "i86.h"

/* The architectural maximum instruction length is fifteen bytes, opcode
 * included (Intel SDM Vol. 2, general instruction format), so fourteen
 * is every prefix byte an instruction can carry. */
#define I86MAXPFX	14

/* Fetch the k'th byte of the instruction at cs:ip.  The (i16) cast is
 * the segment wrap: an instruction straddling 0xFFFF continues at 0. */
static int fb(cs, ip, k)
char *cs;
i16 ip;
int k;
{
	return (cs[(i16)(ip + k)] & 0xff);
}

/* Sign-extend a byte to 16 bits without assuming char's signedness --
 * MWC and the host disagree about it and this file must not. */
static i16 sx(b)
int b;
{
	b &= 0xff;
	return ((i16)(b & 0x80 ? (0xff00 | b) : b));
}

/* ALU sub-op names live in the opcode's own bits, so nothing maps them. */

/* The default segment for a mod r/m memory operand.  BP as a base means
 * the stack segment; everything else means the data segment.  rm = 6
 * with mod = 0 is not BP at all -- it is a direct address. */
static int defseg(mod, rm)
int mod, rm;
{
	if (rm == 2 || rm == 3)
		return (S_SS);		/* [BP+SI], [BP+DI]		*/
	if (rm == 6 && mod != 0)
		return (S_SS);		/* [BP+disp]			*/
	return (S_DS);
}

/*
 * Consume a mod r/m byte and whatever displacement it implies.  `n' is
 * the offset of the mod r/m byte within the instruction; returns the
 * offset just past the displacement.  Sets mod/reg/rm, IN_MEM, .disp
 * and .seg -- the last only when no explicit prefix already set it.
 */
static int modrm(cs, ip, n, in)
char *cs;
i16 ip;
int n;
struct i86in *in;
{
	register int b;

	b = fb(cs, ip, n);
	n++;
	in->mod = (i8)((b >> 6) & 3);
	in->reg = (i8)((b >> 3) & 7);
	in->rm = (i8)(b & 7);
	in->fl |= IN_MODRM;
	in->disp = 0;
	if (in->mod == 3)
		return (n);		/* a register, not memory	*/
	in->fl |= IN_MEM;
	if (in->mod == 0 && in->rm == 6) {
		in->disp = (i16)(fb(cs, ip, n) | (fb(cs, ip, n + 1) << 8));
		n += 2;
	} else if (in->mod == 1) {
		in->disp = sx(fb(cs, ip, n));
		n++;
	} else if (in->mod == 2) {
		in->disp = (i16)(fb(cs, ip, n) | (fb(cs, ip, n + 1) << 8));
		n += 2;
	}
	if (!(in->fl & IN_SEGOVR))
		in->seg = (i8)defseg(in->mod, in->rm);
	return (n);
}

/* Read a 16-bit immediate at offset n. */
static i16 iw(cs, ip, n)
char *cs;
i16 ip;
int n;
{
	return ((i16)(fb(cs, ip, n) | (fb(cs, ip, n + 1) << 8)));
}

/*
 * i86dec -- decode the instruction at cs:ip into *in.
 *
 * Returns the instruction length in bytes.  On a byte that is not an
 * 8086 opcode, sets in->op = I_BAD and returns the length consumed so
 * far (at least 1) so that a linear sweep can step past it -- a sweep
 * that stopped dead at the first table byte would report one finding
 * and hide the rest.
 */
int i86dec(cs, ip, in)
char *cs;
i16 ip;
struct i86in *in;
{
	register int op, n;

	in->op = I_BAD;
	in->fl = 0;
	in->w = 0;
	in->x = 0;
	in->mod = 0;
	in->reg = 0;
	in->rm = 0;
	in->seg = S_DS;
	in->disp = 0;
	in->imm = 0;
	in->imm2 = 0;
	n = 0;

	/* ---- prefixes.  A real 8086 accepts any number of them and only
	 * the last segment override counts; an interrupt between two
	 * prefixes loses all but the last, which is the famous bug and
	 * not something we need to reproduce.
	 *
	 * "Any number" is the one part that cannot be reproduced here.
	 * fb() WRAPS at sixteen bits, deliberately -- an instruction
	 * straddling 0xFFFF continues at 0 -- so a code segment filled
	 * with prefix bytes never leaves this loop, and the hang is
	 * inside one i86dec() call where the executor's step limit never
	 * gets a turn.  The bound is the architectural maximum
	 * instruction length, fifteen bytes including the opcode (Intel
	 * SDM Vol. 2, general instruction format: a longer encoding is
	 * #UD from the 386 on), so at most I86MAXPFX prefixes are
	 * consumed.  Nothing legal comes near it -- an 8086 instruction
	 * has a use for three, one segment override, one repeat and one
	 * LOCK -- so no legal instruction decodes differently for this.
	 * A sixteenth byte that is still a prefix falls out of the loop
	 * and reaches the switch as an opcode, where it is I_BAD. */
	for (;;) {
		if (n >= I86MAXPFX) {
			op = fb(cs, ip, n);
			break;
		}
		op = fb(cs, ip, n);
		if (op == 0x26 || op == 0x2e || op == 0x36 || op == 0x3e) {
			in->seg = (i8)((op >> 3) & 3);
			in->fl |= IN_SEGOVR;
			n++;
			continue;
		}
		if (op == 0xf0 || op == 0xf1) {	/* LOCK, and its alias	*/
			in->fl |= IN_LOCK;
			n++;
			continue;
		}
		if (op == 0xf2) {
			in->fl |= IN_REPNE;
			n++;
			continue;
		}
		if (op == 0xf3) {
			in->fl |= IN_REP;
			n++;
			continue;
		}
		break;
	}
	n++;					/* the opcode byte itself */

	/* 0x60-0x6F have no encoding of their own on an 8086: the top
	 * bits are ignored and they execute as 0x70-0x7F.  Fold them
	 * here so both the length and the mnemonic are right. */
	if (op >= 0x60 && op <= 0x6f)
		op += 0x10;

	switch (op) {

	/* ---- 00-3F: the ALU eight, in six forms each, with a segment
	 * prefix or a BCD adjust in place of the two that would follow. */
	case 0x00: case 0x01: case 0x02: case 0x03: case 0x04: case 0x05:
	case 0x08: case 0x09: case 0x0a: case 0x0b: case 0x0c: case 0x0d:
	case 0x10: case 0x11: case 0x12: case 0x13: case 0x14: case 0x15:
	case 0x18: case 0x19: case 0x1a: case 0x1b: case 0x1c: case 0x1d:
	case 0x20: case 0x21: case 0x22: case 0x23: case 0x24: case 0x25:
	case 0x28: case 0x29: case 0x2a: case 0x2b: case 0x2c: case 0x2d:
	case 0x30: case 0x31: case 0x32: case 0x33: case 0x34: case 0x35:
	case 0x38: case 0x39: case 0x3a: case 0x3b: case 0x3c: case 0x3d:
		in->op = I_ALU;
		in->x = (i8)((op >> 3) & 7);
		in->w = (i8)(op & 1);
		if ((op & 7) < 4) {		/* Eb,Gb / Ev,Gv / Gb,Eb / Gv,Ev */
			if (op & 2)
				in->fl |= IN_DIR;
			n = modrm(cs, ip, n, in);
		} else {			/* AL,Ib / AX,Iv	*/
			in->fl |= IN_IMM;
			in->mod = 3;
			in->rm = R_AX;
			in->reg = R_AX;
			if (in->w) {
				in->imm = iw(cs, ip, n);
				n += 2;
			} else {
				in->imm = (i16)fb(cs, ip, n);
				n++;
			}
		}
		break;

	case 0x06: case 0x0e: case 0x16: case 0x1e:
		in->op = I_PUSHSR;
		in->x = (i8)((op >> 3) & 3);
		break;
	case 0x07: case 0x0f: case 0x17: case 0x1f:
		in->op = I_POPSR;	/* 0x0F really is POP CS here	*/
		in->x = (i8)((op >> 3) & 3);
		break;

	case 0x27: in->op = I_DAA; break;
	case 0x2f: in->op = I_DAS; break;
	case 0x37: in->op = I_AAA; break;
	case 0x3f: in->op = I_AAS; break;

	/* ---- 40-5F: the register-direct one-byte forms. */
	case 0x40: case 0x41: case 0x42: case 0x43:
	case 0x44: case 0x45: case 0x46: case 0x47:
		in->op = I_INC; in->w = 1; in->mod = 3; in->rm = (i8)(op & 7);
		break;
	case 0x48: case 0x49: case 0x4a: case 0x4b:
	case 0x4c: case 0x4d: case 0x4e: case 0x4f:
		in->op = I_DEC; in->w = 1; in->mod = 3; in->rm = (i8)(op & 7);
		break;
	case 0x50: case 0x51: case 0x52: case 0x53:
	case 0x54: case 0x55: case 0x56: case 0x57:
		in->op = I_PUSH; in->w = 1; in->mod = 3; in->rm = (i8)(op & 7);
		break;
	case 0x58: case 0x59: case 0x5a: case 0x5b:
	case 0x5c: case 0x5d: case 0x5e: case 0x5f:
		in->op = I_POP; in->w = 1; in->mod = 3; in->rm = (i8)(op & 7);
		break;

	/* ---- 70-7F: the sixteen short conditionals. */
	case 0x70: case 0x71: case 0x72: case 0x73:
	case 0x74: case 0x75: case 0x76: case 0x77:
	case 0x78: case 0x79: case 0x7a: case 0x7b:
	case 0x7c: case 0x7d: case 0x7e: case 0x7f:
		in->op = I_JCC;
		in->x = (i8)(op & 15);
		in->disp = sx(fb(cs, ip, n));
		n++;
		in->disp = (i16)(in->disp + ip + n);
		break;

	/* ---- 80-83: the ALU eight against an immediate. */
	case 0x80: case 0x81: case 0x82: case 0x83:
		in->op = I_ALU;
		in->w = (i8)(op & 1);
		n = modrm(cs, ip, n, in);
		in->x = in->reg;
		in->fl |= IN_IMM;
		if (op == 0x81) {
			in->imm = iw(cs, ip, n);
			n += 2;
		} else if (op == 0x83) {
			in->imm = sx(fb(cs, ip, n));	/* sign-extended */
			n++;
		} else {
			in->imm = (i16)fb(cs, ip, n);
			n++;
		}
		break;

	case 0x84: case 0x85:
		in->op = I_TEST; in->w = (i8)(op & 1);
		n = modrm(cs, ip, n, in);
		break;
	case 0x86: case 0x87:
		in->op = I_XCHG; in->w = (i8)(op & 1);
		n = modrm(cs, ip, n, in);
		break;
	case 0x88: case 0x89: case 0x8a: case 0x8b:
		in->op = I_MOV; in->w = (i8)(op & 1);
		if (op & 2)
			in->fl |= IN_DIR;
		n = modrm(cs, ip, n, in);
		break;
	case 0x8c: case 0x8e:
		in->op = I_MOVSR; in->w = 1;
		if (op & 2)
			in->fl |= IN_DIR;	/* 8E: Sreg <- Ew	*/
		n = modrm(cs, ip, n, in);
		in->x = (i8)(in->reg & 3);
		break;
	case 0x8d:
		in->op = I_LEA; in->w = 1;
		n = modrm(cs, ip, n, in);
		break;
	case 0x8f:
		in->op = I_POP; in->w = 1;
		n = modrm(cs, ip, n, in);
		break;

	case 0x90:
		in->op = I_NOP;
		break;
	case 0x91: case 0x92: case 0x93:
	case 0x94: case 0x95: case 0x96: case 0x97:
		in->op = I_XCHG; in->w = 1;
		in->mod = 3; in->reg = R_AX; in->rm = (i8)(op & 7);
		break;

	case 0x98: in->op = I_CBW; break;
	case 0x99: in->op = I_CWD; break;
	case 0x9a:
		in->op = I_CALLF;
		in->imm = iw(cs, ip, n);
		in->imm2 = iw(cs, ip, n + 2);
		n += 4;
		break;
	case 0x9b: in->op = I_WAIT; break;
	case 0x9c: in->op = I_PUSHF; break;
	case 0x9d: in->op = I_POPF; break;
	case 0x9e: in->op = I_SAHF; break;
	case 0x9f: in->op = I_LAHF; break;

	/* ---- A0-A3: accumulator to and from a direct address.  These
	 * carry no mod r/m, so the segment default is DS with no BP rule
	 * to consider; a prefix still overrides it. */
	case 0xa0: case 0xa1: case 0xa2: case 0xa3:
		in->op = I_MOV;
		in->w = (i8)(op & 1);
		in->mod = 0; in->rm = 6; in->reg = R_AX;
		in->fl |= IN_MODRM | IN_MEM;
		if (!(op & 2))
			in->fl |= IN_DIR;	/* A0/A1: AL/AX <- mem	*/
		in->disp = iw(cs, ip, n);
		n += 2;
		break;

	case 0xa4: case 0xa5: case 0xa6: case 0xa7:
	case 0xaa: case 0xab: case 0xac: case 0xad:
	case 0xae: case 0xaf:
		in->op = I_STRING;
		in->w = (i8)(op & 1);
		if (op < 0xa8)
			in->x = (i8)((op >> 1) & 1);	/* 0 MOVS, 1 CMPS */
		else
			in->x = (i8)(2 + ((op - 0xaa) >> 1)); /* STOS LODS SCAS */
		break;
	case 0xa8: case 0xa9:
		in->op = I_TEST;
		in->w = (i8)(op & 1);
		in->mod = 3; in->rm = R_AX; in->reg = R_AX;
		in->fl |= IN_IMM;
		if (in->w) {
			in->imm = iw(cs, ip, n);
			n += 2;
		} else {
			in->imm = (i16)fb(cs, ip, n);
			n++;
		}
		break;

	/* ---- B0-BF: immediate into a register. */
	case 0xb0: case 0xb1: case 0xb2: case 0xb3:
	case 0xb4: case 0xb5: case 0xb6: case 0xb7:
		in->op = I_MOV; in->w = 0;
		in->mod = 3; in->rm = (i8)(op & 7);
		in->fl |= IN_IMM;
		in->imm = (i16)fb(cs, ip, n);
		n++;
		break;
	case 0xb8: case 0xb9: case 0xba: case 0xbb:
	case 0xbc: case 0xbd: case 0xbe: case 0xbf:
		in->op = I_MOV; in->w = 1;
		in->mod = 3; in->rm = (i8)(op & 7);
		in->fl |= IN_IMM;
		in->imm = iw(cs, ip, n);
		n += 2;
		break;

	case 0xc0: case 0xc2:			/* C0 aliases C2	*/
		in->op = I_RET;
		in->imm = iw(cs, ip, n);
		in->fl |= IN_IMM;
		n += 2;
		break;
	case 0xc1: case 0xc3:
		in->op = I_RET;
		break;
	case 0xc4: case 0xc5:
		in->op = I_LXS; in->w = 1;
		in->x = (i8)(op == 0xc4 ? S_ES : S_DS);
		n = modrm(cs, ip, n, in);
		break;
	case 0xc6: case 0xc7:
		in->op = I_MOV;
		in->w = (i8)(op & 1);
		n = modrm(cs, ip, n, in);
		in->fl |= IN_IMM;
		if (in->w) {
			in->imm = iw(cs, ip, n);
			n += 2;
		} else {
			in->imm = (i16)fb(cs, ip, n);
			n++;
		}
		break;
	case 0xc8: case 0xca:			/* C8 aliases CA	*/
		in->op = I_RETF;
		in->imm = iw(cs, ip, n);
		in->fl |= IN_IMM;
		n += 2;
		break;
	case 0xc9: case 0xcb:
		in->op = I_RETF;
		break;
	case 0xcc:
		in->op = I_INT; in->imm = 3;
		break;
	case 0xcd:
		in->op = I_INT;
		in->imm = (i16)fb(cs, ip, n);
		n++;
		break;
	case 0xce: in->op = I_INTO; break;
	case 0xcf: in->op = I_IRET; break;

	/* ---- D0-D3: the shift and rotate eight. */
	case 0xd0: case 0xd1: case 0xd2: case 0xd3:
		in->op = I_SHIFT;
		in->w = (i8)(op & 1);
		n = modrm(cs, ip, n, in);
		in->x = in->reg;
		/* imm2 records where the count comes from: 0 = the
		 * literal 1, 1 = CL.  The 8086 does NOT mask that count
		 * -- masking to 5 bits arrived with the 186 -- so a CL
		 * of 200 really is 200 iterations. */
		in->imm2 = (i16)(op & 2 ? 1 : 0);
		break;

	case 0xd4:
		in->op = I_AAM; in->imm = (i16)fb(cs, ip, n); n++;
		break;
	case 0xd5:
		in->op = I_AAD; in->imm = (i16)fb(cs, ip, n); n++;
		break;
	case 0xd6:
		in->op = I_BAD;		/* SALC: undocumented, refused	*/
		break;
	case 0xd7:
		in->op = I_XLAT;
		if (!(in->fl & IN_SEGOVR))
			in->seg = S_DS;
		break;

	case 0xd8: case 0xd9: case 0xda: case 0xdb:
	case 0xdc: case 0xdd: case 0xde: case 0xdf:
		in->op = I_ESC;		/* 8087; there is not one here	*/
		n = modrm(cs, ip, n, in);
		break;

	case 0xe0: case 0xe1: case 0xe2: case 0xe3:
		in->op = I_LOOP;
		in->x = (i8)(op & 3);
		in->disp = sx(fb(cs, ip, n));
		n++;
		in->disp = (i16)(in->disp + ip + n);
		break;

	case 0xe4: case 0xe5: case 0xe6: case 0xe7:
		in->op = I_IO; in->w = (i8)(op & 1);
		in->x = (i8)((op >> 1) & 1);	/* 0 = IN, 1 = OUT	*/
		in->imm = (i16)fb(cs, ip, n);
		n++;
		break;
	case 0xec: case 0xed: case 0xee: case 0xef:
		in->op = I_IO; in->w = (i8)(op & 1);
		in->x = (i8)(((op >> 1) & 1) | 2);	/* | 2 = via DX	*/
		break;

	case 0xe8:
		in->op = I_CALL;
		in->disp = iw(cs, ip, n);
		n += 2;
		in->disp = (i16)(in->disp + ip + n);
		break;
	case 0xe9:
		in->op = I_JMP;
		in->disp = iw(cs, ip, n);
		n += 2;
		in->disp = (i16)(in->disp + ip + n);
		break;
	case 0xea:
		in->op = I_JMPF;
		in->imm = iw(cs, ip, n);
		in->imm2 = iw(cs, ip, n + 2);
		n += 4;
		break;
	case 0xeb:
		in->op = I_JMP;
		in->disp = sx(fb(cs, ip, n));
		n++;
		in->disp = (i16)(in->disp + ip + n);
		break;

	case 0xf4: in->op = I_HLT; break;
	case 0xf5: in->op = I_FLAG; in->imm = F_CF; in->x = 2; break;

	/* ---- F6/F7: TEST-immediate, the two unaries, and the four
	 * widening multiply/divide forms, all under one mod r/m. */
	case 0xf6: case 0xf7:
		in->w = (i8)(op & 1);
		n = modrm(cs, ip, n, in);
		switch (in->reg) {
		case 0: case 1:			/* /1 aliases /0	*/
			in->op = I_TEST;
			in->fl |= IN_IMM;
			if (in->w) {
				in->imm = iw(cs, ip, n);
				n += 2;
			} else {
				in->imm = (i16)fb(cs, ip, n);
				n++;
			}
			break;
		case 2: in->op = I_NOT; break;
		case 3: in->op = I_NEG; break;
		default:
			in->op = I_MULDIV;
			in->x = in->reg;
			break;
		}
		break;

	case 0xf8: in->op = I_FLAG; in->imm = F_CF; in->x = 0; break;
	case 0xf9: in->op = I_FLAG; in->imm = F_CF; in->x = 1; break;
	case 0xfa: in->op = I_FLAG; in->imm = F_IF; in->x = 0; break;
	case 0xfb: in->op = I_FLAG; in->imm = F_IF; in->x = 1; break;
	case 0xfc: in->op = I_FLAG; in->imm = F_DF; in->x = 0; break;
	case 0xfd: in->op = I_FLAG; in->imm = F_DF; in->x = 1; break;

	case 0xfe: case 0xff:
		in->w = (i8)(op & 1);
		n = modrm(cs, ip, n, in);
		switch (in->reg) {
		case 0: in->op = I_INC; break;
		case 1: in->op = I_DEC; break;
		case 2: if (op == 0xff) { in->op = I_CALLI; in->x = 0; }
			break;
		case 3: if (op == 0xff) { in->op = I_CALLI; in->x = 1; }
			break;
		case 4: if (op == 0xff) { in->op = I_JMPI; in->x = 0; }
			break;
		case 5: if (op == 0xff) { in->op = I_JMPI; in->x = 1; }
			break;
		case 6: if (op == 0xff) in->op = I_PUSH;
			break;
		default:
			break;			/* /7 -- and FE /2..7	*/
		}
		break;

	default:
		in->op = I_BAD;			/* 0xF1 reached as an	*/
		break;				/* opcode, and nothing else */
	}

	if (n < 1)
		n = 1;
	in->len = (i8)n;
	return (n);
}

/*
 * i86mnem -- the mnemonic for a decoded instruction, for the corpus
 * sweep and for a refusal message.  Not on any execution path; kept
 * here so the names sit next to the encodings that produce them.
 */
char *i86mnem(in)
struct i86in *in;
{
	static char *alu[8] = {
		"add", "or", "adc", "sbb", "and", "sub", "xor", "cmp"
	};
	static char *sh[8] = {
		"rol", "ror", "rcl", "rcr", "shl", "shr", "shl", "sar"
	};
	static char *md[8] = {
		"?", "?", "?", "?", "mul", "imul", "div", "idiv"
	};
	static char *cc[16] = {
		"jo", "jno", "jb", "jae", "je", "jne", "jbe", "ja",
		"js", "jns", "jp", "jnp", "jl", "jge", "jle", "jg"
	};
	static char *lp[4] = { "loopne", "loope", "loop", "jcxz" };
	static char *st[5] = { "movs", "cmps", "stos", "lods", "scas" };

	switch (in->op) {
	case I_ALU:	return (alu[in->x & 7]);
	case I_SHIFT:	return (sh[in->x & 7]);
	case I_MULDIV:	return (md[in->x & 7]);
	case I_JCC:	return (cc[in->x & 15]);
	case I_LOOP:	return (lp[in->x & 3]);
	case I_STRING:	return (st[in->x < 5 ? in->x : 0]);
	case I_MOV: case I_MOVSR:	return ("mov");
	case I_LEA:	return ("lea");
	case I_LXS:	return (in->x == S_ES ? "les" : "lds");
	case I_XCHG:	return ("xchg");
	case I_TEST:	return ("test");
	case I_INC:	return ("inc");
	case I_DEC:	return ("dec");
	case I_NOT:	return ("not");
	case I_NEG:	return ("neg");
	case I_PUSH: case I_PUSHSR:	return ("push");
	case I_POP: case I_POPSR:	return ("pop");
	case I_PUSHF:	return ("pushf");
	case I_POPF:	return ("popf");
	case I_JMP: case I_JMPI: case I_JMPF:	return ("jmp");
	case I_CALL: case I_CALLI: case I_CALLF:	return ("call");
	case I_RET:	return ("ret");
	case I_RETF:	return ("retf");
	case I_INT:	return ("int");
	case I_INTO:	return ("into");
	case I_IRET:	return ("iret");
	case I_XLAT:	return ("xlat");
	case I_CBW:	return ("cbw");
	case I_CWD:	return ("cwd");
	case I_LAHF:	return ("lahf");
	case I_SAHF:	return ("sahf");
	case I_FLAG:
		if (in->imm == F_CF)
			return (in->x == 0 ? "clc" : in->x == 1 ? "stc" : "cmc");
		if (in->imm == F_IF)
			return (in->x ? "sti" : "cli");
		return (in->x ? "std" : "cld");
	case I_DAA:	return ("daa");
	case I_DAS:	return ("das");
	case I_AAA:	return ("aaa");
	case I_AAS:	return ("aas");
	case I_AAM:	return ("aam");
	case I_AAD:	return ("aad");
	case I_NOP:	return ("nop");
	case I_HLT:	return ("hlt");
	case I_WAIT:	return ("wait");
	case I_ESC:	return ("esc");
	case I_IO:	return ((in->x & 1) ? "out" : "in");
	}
	return ("(bad)");
}
