/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/*
 * zdect.c -- the two 8080/Z80 decoders, side by side on the machine.
 *
 * The target build runs the assembly decoder (z80deca.s); z80dec.c stays
 * the portable reference and is what the host suite compiles.  This
 * program links BOTH -- the C one under the name z80decc -- and sweeps
 * the opcode space through each, comparing the return value and every
 * field of the decode.  A fix made to one and not the other fails here.
 *
 * The sweep is every first byte against several operand patterns at
 * several program counters, two of which put the instruction across
 * 0xFFFF: the fetch wraps there, and the two decoders reach that wrap by
 * different routes -- a cast in the C, segment offset arithmetic in the
 * assembly.  It needs a whole 64 KB segment to be a real wrap, so this
 * asks the BIOS for one exactly as the shim does.
 */

#include "cpm.h"
#include "z80.h"

extern int z80decc();

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

/* The bytes after the opcode.  Beyond the plain fills they are chosen to
 * reach the encodings whose length rule is their own: ED 43 nn nn and
 * ED FE nn, DD CB d op, a prefix on a prefix, and the (HL) forms that
 * grow a displacement under DD/FD. */
static char pat[10][3] = {
	{ 0x00, 0x00, 0x00 },
	{ 0xff, 0xff, 0xff },
	{ 0x80, 0x7f, 0x01 },
	{ 0xfe, 0x40, 0x63 },
	{ 0xfe, 0x01, 0x05 },
	{ 0x43, 0x34, 0x12 },
	{ 0xcb, 0x05, 0x77 },
	{ 0x36, 0x12, 0x34 },
	{ 0x7e, 0x11, 0x22 },
	{ 0xdd, 0x21, 0x00 }
};

static z16 pcs[5] = { 0x0100, 0x0000, 0x1234, 0xfffd, 0xffff };

static struct z80in a, c;

static VOID bad(what, av, cv)
char *what;
unsigned av, cv;
{
	cputs("ZDECT: FAIL ");
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
	if (a.x != c.x) {
		bad("x", a.x, c.x);
		return (0);
	}
	if (a.y != c.y) {
		bad("y", a.y, c.y);
		return (0);
	}
	if (a.pfx != c.pfx) {
		bad("pfx", a.pfx, c.pfx);
		return (0);
	}
	if (a.sub != c.sub) {
		bad("sub", a.sub, c.sub);
		return (0);
	}
	if (a.imm != c.imm) {
		bad("imm", a.imm, c.imm);
		return (0);
	}
	if (a.disp != c.disp) {
		bad("disp", a.disp, c.disp);
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
	int seg, op, p, k, ra, rc;
	unsigned n;
	z16 pc;

	cputs("ZDECT: start\r\n");

	seg = (int) segcall(SEG_GET, 0L);
	if (seg == 0) {
		cputs("ZDECT: no 64 KB segment to decode in\r\n");
		return (1);
	}
	xa = SEGBASE(seg);
	m = (char *) xa;

	n = 0;
	for (k = 0; k < 5; k++) {
		pc = pcs[k];
		for (p = 0; p < 10; p++) {
			for (op = 0; op < 256; op++) {
				m[(z16)(pc)] = (char) op;
				m[(z16)(pc + 1)] = pat[p][0];
				m[(z16)(pc + 2)] = pat[p][1];
				m[(z16)(pc + 3)] = pat[p][2];
				ra = z80dec(m, pc, &a);
				rc = z80decc(m, pc, &c);
				if (!same(ra, rc)) {
					cputs("ZDECT:      at pc=");
					phex4((unsigned) pc);
					cputs(" op=");
					phex2(op);
					cputs(" pat=");
					putdec((unsigned) p);
					cputs("\r\n");
					segcall(SEG_PUT, (long) seg);
					return (1);
				}
				n++;
			}
		}
	}

	segcall(SEG_PUT, (long) seg);
	cputs("ZDECT: ");
	putdec(n);
	cputs(" decodes agree\r\n");
	cputs("ZDECT: ok\r\n");
	return (0);
}
