/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/*
 * idect.c -- the two 8086 decoders, side by side on the machine.
 *
 * The target build runs the assembly decoder (i86deca.s); i86dec.c stays
 * the portable reference and is what the host suite compiles.  This
 * program links BOTH -- the C one under the name i86decc -- and compares
 * the return value and every field of the decode.
 *
 * Each pass is the whole square of first byte against second byte, so
 * every opcode meets every mod/reg/rm.  The bytes after those two follow
 * the second byte, so a displacement or immediate takes every value
 * across a square.  Passes vary what comes before the opcode (nothing,
 * each prefix, mixed runs, runs either side of the prefix cap) and where
 * the instruction sits, including across 0xFFFF, where the fetch wraps.
 * A real wrap needs a whole 64 KB segment, taken from the BIOS exactly as
 * the shim takes one.
 */

#include "cpm.h"
#include "i86.h"

extern int i86decc();

static struct biospb pb;

static long segcall(p1, p2)
long p1, p2;
{
	pb.code = BIOS_SEGMENT;
	pb.p1 = p1;
	pb.p2 = p2;
	return (__bdosl(BDOS_BIOSCALL, (long) &pb));
}

static char hexd[] = "0123456789ABCDEF";

static VOID phex2(v)
int v;
{
	conout(hexd[(v >> 4) & 15]);
	conout(hexd[v & 15]);
}

static VOID phex4(v)
unsigned v;
{
	phex2((int) ((v >> 8) & 0xff));
	phex2((int) (v & 0xff));
}

/* Prefix runs: a count, then the bytes.  15 and 16 prefixes pass the
 * cap, so the fifteenth byte is the opcode and the square's two bytes
 * are never read; those passes sweep the first byte only. */
static char pfx[][17] = {
	{ 0 },
	{ 1, 0x26 }, { 1, 0x2e }, { 1, 0x36 }, { 1, 0x3e },
	{ 1, 0xf0 }, { 1, 0xf1 }, { 1, 0xf2 }, { 1, 0xf3 },
	{ 2, 0xf3, 0x26 },
	{ 3, 0x36, 0xf2, 0x2e },
	{ 4, 0xf0, 0x3e, 0xf3, 0x26 },
	{ 13, 0x26, 0x26, 0x26, 0x26, 0x26, 0x26, 0x26, 0x26, 0x26, 0x26,
	  0x26, 0x26, 0x36 },
	{ 14, 0x3e, 0x3e, 0x3e, 0x3e, 0x3e, 0x3e, 0x3e, 0x3e, 0x3e, 0x3e,
	  0x3e, 0x3e, 0x3e, 0x2e },
	{ 15, 0xf3, 0xf3, 0xf3, 0xf3, 0xf3, 0xf3, 0xf3, 0xf3, 0xf3, 0xf3,
	  0xf3, 0xf3, 0xf3, 0xf3, 0x26 },
	{ 16, 0xf2, 0xf2, 0xf2, 0xf2, 0xf2, 0xf2, 0xf2, 0xf2, 0xf2, 0xf2,
	  0xf2, 0xf2, 0xf2, 0xf2, 0x36, 0xf0 }
};
#define NPFX	(sizeof pfx / sizeof pfx[0])

/* The prefix run and where the opcode goes, per pass.  At 0x0002 the
 * prefixes straddle 0xFFFF; the last five put the opcode, the mod r/m
 * byte or an operand byte on it. */
static struct {
	int p;
	i16 ip;
} pass[] = {
	{ 0, 0x0100 },
	{ 1, 0x0002 }, { 2, 0x0100 }, { 3, 0x0002 }, { 4, 0x0100 },
	{ 5, 0x0002 }, { 6, 0x0100 }, { 7, 0x0002 }, { 8, 0x0100 },
	{ 9, 0x0002 }, { 10, 0x0100 }, { 11, 0x0002 },
	{ 12, 0x0002 }, { 13, 0x0100 }, { 14, 0x0002 }, { 15, 0x0100 },
	{ 0, 0xffff }, { 0, 0xfffe }, { 0, 0xfffd }, { 0, 0xfffc },
	{ 0, 0xfffb }
};
#define NPASS	(sizeof pass / sizeof pass[0])

static struct i86in a, c;

static VOID bad(what, av, cv)
char *what;
unsigned av, cv;
{
	cputs("IDECT: FAIL ");
	cputs(what);
	cputs(" asm=");
	phex4(av);
	cputs(" c=");
	phex4(cv);
	cputs("\r\n");
}

/* Compare one decode; 0 on the first field that differs. */
static int same(ra, rc)
int ra, rc;
{
	if (ra != rc) {
		bad("ret", (unsigned) ra, (unsigned) rc);
		return (0);
	}
	if (a.len != c.len) {
		bad("len", a.len, c.len);
		return (0);
	}
	if (a.op != c.op) {
		bad("op", a.op, c.op);
		return (0);
	}
	if (a.fl != c.fl) {
		bad("fl", a.fl, c.fl);
		return (0);
	}
	if (a.w != c.w) {
		bad("w", a.w, c.w);
		return (0);
	}
	if (a.x != c.x) {
		bad("x", a.x, c.x);
		return (0);
	}
	if (a.mod != c.mod) {
		bad("mod", a.mod, c.mod);
		return (0);
	}
	if (a.reg != c.reg) {
		bad("reg", a.reg, c.reg);
		return (0);
	}
	if (a.rm != c.rm) {
		bad("rm", a.rm, c.rm);
		return (0);
	}
	if (a.seg != c.seg) {
		bad("seg", a.seg, c.seg);
		return (0);
	}
	if (a.disp != c.disp) {
		bad("disp", a.disp, c.disp);
		return (0);
	}
	if (a.imm != c.imm) {
		bad("imm", a.imm, c.imm);
		return (0);
	}
	if (a.imm2 != c.imm2) {
		bad("imm2", a.imm2, c.imm2);
		return (0);
	}
	return (1);
}

int main(argc, argv)
int argc;
char *argv[];
{
	char *m;
	long xa;
	int seg, k, j, n, p, op, g, gmax, ra, rc;
	unsigned sq;
	i16 ip, q;

	cputs("IDECT: start\r\n");

	seg = (int) segcall(SEG_GET, 0L);
	if (seg == 0) {
		cputs("IDECT: no 64 KB segment to decode in\r\n");
		return (1);
	}
	xa = SEGBASE(seg);
	m = (char *) xa;

	sq = 0;
	for (k = 0; k < NPASS; k++) {
		p = pass[k].p;
		n = pfx[p][0];
		ip = pass[k].ip - n;
		for (j = 0; j < n; j++)
			m[(i16)(ip + j)] = pfx[p][1 + j];
		q = ip + n;
		gmax = n > 14 ? 1 : 256;
		for (op = 0; op < 256; op++) {
			m[q] = (char) op;
			for (g = 0; g < gmax; g++) {
				m[(i16)(q + 1)] = (char) g;
				m[(i16)(q + 2)] = (char) (g ^ 0x80);
				m[(i16)(q + 3)] = (char) ~g;
				m[(i16)(q + 4)] = (char) (g + 0x7f);
				m[(i16)(q + 5)] = (char) (g ^ 0x5a);
				ra = i86dec(m, ip, &a);
				rc = i86decc(m, ip, &c);
				if (!same(ra, rc)) {
					cputs("IDECT:      at ip=");
					phex4((unsigned) ip);
					cputs(" prefixes=");
					putdec((unsigned) n);
					cputs(" op=");
					phex2(op);
					cputs(" next=");
					phex2(g);
					cputs(" pass=");
					putdec((unsigned) k);
					cputs("\r\n");
					segcall(SEG_PUT, (long) seg);
					return (1);
				}
			}
		}
		sq++;
	}

	segcall(SEG_PUT, (long) seg);
	cputs("IDECT: ");
	putdec(sq);
	cputs(" passes agree\r\n");
	cputs("IDECT: ok\r\n");
	return (0);
}
