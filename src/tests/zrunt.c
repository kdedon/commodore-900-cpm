/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/*
 * zrunt.c -- the two 8080/Z80 run loops, in lockstep on the machine.
 *
 * The target runs z80runa.s, which executes the frequent classes itself.
 * This program also links the C executor and run loop, renamed with a
 * leading c, over the C decoder, and runs both from the same machine
 * states over two guest segments holding the same bytes.  It compares
 * the answer, the count, every register, the lazy record, the counters
 * and the memory.
 *
 * First every opcode, one instruction at a time from random states:
 * operands, SP and PC near 0xFFFF, pending records of every class, and
 * DCR r planted as a delay loop.  Then runs of 64 instructions through
 * random memory.
 */

#include "cpm.h"
#include "z80.h"

extern int cz80run();
extern z32 cz80ninsn, cz80nflag, cz80nskip;
extern int cz80fast, cz80hookno;
extern int (*cz80wait)();

#define NSTATE	96		/* states per opcode			*/
#define NSEQ	96		/* runs through random memory		*/
#define SEQLEN	64L		/* instructions a run may take		*/

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

static VOID phex4(v)
unsigned v;
{
	conout(hexd[(v >> 12) & 15]);
	conout(hexd[(v >> 8) & 15]);
	conout(hexd[(v >> 4) & 15]);
	conout(hexd[v & 15]);
}

static unsigned rs = 0xace1;

static unsigned rnd()
{
	rs ^= rs << 7;
	rs ^= rs >> 9;
	rs ^= rs << 8;
	return (rs);
}

static char *ma, *mc;			/* the two guest segments	*/
static struct z80 S, A, C;		/* the start, asm, reference	*/
static unsigned ncase;

static VOID bad(what, av, cv)
char *what;
unsigned av, cv;
{
	cputs("ZRUNT: FAIL ");
	cputs(what);
	cputs(" asm=");
	phex4(av);
	cputs(" c=");
	phex4(cv);
	cputs(" case ");
	putdec(ncase);
	cputs(" from pc=");
	phex4((unsigned) S.pc);
	cputs(" op=");
	phex4((unsigned) (ma[S.pc] & 0xff));
	cputs("\r\n");
}

/* Both segments get byte v at a. */
static VOID poke(a, v)
z16 a;
int v;
{
	ma[a] = (char) v;
	mc[a] = (char) v;
}

static int memsame(a)
z16 a;
{
	if (ma[a] == mc[a])
		return (1);
	bad("mem", (unsigned) a, 0);
	cputs("ZRUNT:      asm=");
	phex4((unsigned) (ma[a] & 0xff));
	cputs(" c=");
	phex4((unsigned) (mc[a] & 0xff));
	cputs("\r\n");
	return (0);
}

static int allsame()
{
	z16 a;

	a = 0;
	do {
		if (ma[a] != mc[a])
			return (memsame(a));
	} while (++a != 0);
	return (1);
}

/* The bytes an instruction from state s can have written. */
static int near(s)
struct z80 *s;
{
	z16 a;
	int i;

	for (i = 0; i < 4; i++)
		if (!memsame((z16) (s->pc + i)))
			return (0);
	for (i = -3; i < 3; i++)
		if (!memsame((z16) (s->rp[P_SP] + i)))
			return (0);
	for (i = 0; i < 3; i++)
		if (!memsame((z16) (s->rp[i])) || !memsame((z16) (s->rp[i] + 1)))
			return (0);
	a = (z16) ((ma[(z16) (s->pc + 1)] & 0xff)
		   | ((ma[(z16) (s->pc + 2)] & 0xff) << 8));
	return (memsame(a) && memsame((z16) (a + 1)));
}

/* A byte, half the time one where carries, signs and zero change. */
static char edge[10] = { 0, 1, 2, 0x0f, 0x10, 0x7f, 0x80, 0x81, 0xfe, 0xff };

static int sv()
{
	unsigned r;

	r = rnd();
	if (r & 0x100)
		return (edge[(r >> 9) % 10] & 0xff);
	return (r & 0xff);
}

/* A pending record: usually one an instruction could have left. */
static VOID mkrec()
{
	int a, b, c;

	S.lz = (z8) (rnd() % 11);
	S.lc = (z8) (rnd() & 1);
	a = sv();
	b = sv();
	c = S.lc;
	S.la = (z16) a;
	S.lb = (z16) b;
	S.lr = (z16) sv();
	if (rnd() & 3) {
		switch (S.lz) {
		case LZ_ADD:
			S.lr = (z16) ((a + b + c) & 0xff);
			break;
		case LZ_SUB:
			S.lr = (z16) ((a - b - c) & 0xff);
			break;
		case LZ_AND:
			S.lr = (z16) (a & b);
			break;
		case LZ_LOG:
			S.lr = (z16) (a ^ b);
			break;
		case LZ_INR:
			S.lb = 1;
			S.lr = (z16) ((a + 1) & 0xff);
			break;
		case LZ_DCR:
			S.lb = 1;
			S.lr = (z16) ((a - 1) & 0xff);
			break;
		}
	}
}

static VOID mkstate()
{
	int i;

	for (i = 0; i < 4; i++)
		S.rp[i] = (z16) ((sv() << 8) | sv());
	for (i = 0; i < 3; i++)
		S.arp[i] = (z16) rnd();
	S.ix = (z16) rnd();
	S.iy = (z16) rnd();
	S.a = (z8) sv();
	S.f = (z8) rnd();
	S.aa = (z8) rnd();
	S.af = (z8) rnd();
	S.iff = (z8) (rnd() & 1);
	S.halt = 0;
	S.pc = (z16) rnd();
	switch (rnd() & 7) {
	case 0:
		S.pc = (z16) (0xfffc + (rnd() & 3));
		break;
	case 1:
		S.rp[P_SP] = (z16) (rnd() & 3);
		break;
	case 2:
		S.rp[P_HL] = 0xffff;
		break;
	}
	mkrec();
}

/* Plant op at S.pc with random operands, and fresh bytes wherever it
 * can read. */
static VOID plant(op)
int op;
{
	int b1, b2, b3, i;
	z16 a;

	b1 = sv();
	b2 = sv();
	b3 = rnd() & 0xff;
	if ((op & 0xc7) == 0x05 && op != 0x35 && (rnd() & 1)) {
		/* a DCR r delay loop: JNZ or JR NZ back to it */
		if (rnd() & 1) {
			b1 = 0xc2;
			b2 = S.pc & 0xff;
			b3 = (S.pc >> 8) & 0xff;
		} else {
			b1 = 0x20;
			b2 = 0xfd;
		}
		if (op == 0x3d)
			S.a &= 0x0f;
		else if (rnd() & 1)
			S.rp[(op >> 4) & 3] &= (op & 8) ? 0xff0f : 0x0fff;
	}
	if (op == 0xed && (rnd() & 3) == 0)
		b1 = 0xfe;
	for (i = -3; i < 3; i++)
		poke((z16) (S.rp[P_SP] + i), rnd());
	for (i = 0; i < 3; i++) {
		poke(S.rp[i], sv());
		poke((z16) (S.rp[i] + 1), sv());
	}
	a = (z16) (b1 | (b2 << 8));
	poke(a, rnd());
	poke((z16) (a + 1), rnd());
	poke(S.pc, op);
	poke((z16) (S.pc + 1), b1);
	poke((z16) (S.pc + 2), b2);
	poke((z16) (S.pc + 3), b3);
}

/* Run both from S for n instructions; 0 on the first difference. */
static int step(n)
long n;
{
	long ka, kc;
	int ra, rc, i;

	A = S;
	A.m = ma;
	C = S;
	C.m = mc;
	z80ninsn = z80nflag = z80nskip = 0;
	cz80ninsn = cz80nflag = cz80nskip = 0;
	z80fast = cz80fast = (rnd() & 7) != 0;
	z80wait = cz80wait = 0;
	ka = kc = 0;
	ra = z80run(&A, n, &ka);
	rc = cz80run(&C, n, &kc);
	ncase++;
	if (ra != rc) {
		bad("answer", ra, rc);
		return (0);
	}
	if (ka != kc) {
		bad("count", (unsigned) ka, (unsigned) kc);
		return (0);
	}
	if (ra == X_HOOK && z80hookno != cz80hookno) {
		bad("hook", z80hookno, cz80hookno);
		return (0);
	}
	if (z80ninsn != cz80ninsn) {
		bad("ninsn", (unsigned) z80ninsn, (unsigned) cz80ninsn);
		return (0);
	}
	if (z80nflag != cz80nflag) {
		bad("nflag", (unsigned) z80nflag, (unsigned) cz80nflag);
		return (0);
	}
	if (z80nskip != cz80nskip) {
		bad("nskip", (unsigned) z80nskip, (unsigned) cz80nskip);
		return (0);
	}
	for (i = 0; i < 4; i++)
		if (A.rp[i] != C.rp[i]) {
			bad("pair", A.rp[i], C.rp[i]);
			return (0);
		}
	for (i = 0; i < 3; i++)
		if (A.arp[i] != C.arp[i]) {
			bad("alt pair", A.arp[i], C.arp[i]);
			return (0);
		}
	if (A.a != C.a || A.f != C.f) {
		bad("af", (A.a << 8) | A.f, (C.a << 8) | C.f);
		return (0);
	}
	if (A.aa != C.aa || A.af != C.af) {
		bad("af'", (A.aa << 8) | A.af, (C.aa << 8) | C.af);
		return (0);
	}
	if (A.pc != C.pc) {
		bad("pc", A.pc, C.pc);
		return (0);
	}
	if (A.ix != C.ix || A.iy != C.iy) {
		bad("ix", A.ix, C.ix);
		return (0);
	}
	if (A.iff != C.iff || A.halt != C.halt) {
		bad("iff/halt", (A.iff << 8) | A.halt, (C.iff << 8) | C.halt);
		return (0);
	}
	if (A.lz != C.lz || A.lc != C.lc) {
		bad("lz/lc", (A.lz << 8) | A.lc, (C.lz << 8) | C.lc);
		return (0);
	}
	if (A.lz != LZ_NONE && (A.la != C.la || A.lb != C.lb || A.lr != C.lr)) {
		bad("record", A.la, C.la);
		return (0);
	}
	return (1);
}

int main(argc, argv)
int argc;
char *argv[];
{
	long xa;
	int sa, sc, op, k;
	z16 a;

	cputs("ZRUNT: start\r\n");
	sa = (int) segcall(SEG_GET, 0L);
	sc = (int) segcall(SEG_GET, 0L);
	if (sa == 0 || sc == 0) {
		cputs("ZRUNT: no two 64 KB segments to run in\r\n");
		return (1);
	}
	xa = SEGBASE(sa);
	ma = (char *) xa;
	xa = SEGBASE(sc);
	mc = (char *) xa;
	a = 0;
	do {
		poke(a, rnd());
	} while (++a != 0);

	ncase = 0;
	for (op = 0; op < 256; op++) {
		for (k = 0; k < NSTATE; k++) {
			mkstate();
			plant(op);
			if (!step(1L) || !near(&S) || !near(&C))
				goto fail;
		}
		if ((op & 31) == 31 && !allsame())
			goto fail;
	}
	/* A count of zero runs nothing. */
	mkstate();
	if (!step(0L) || A.pc != S.pc)
		goto fail;
	for (k = 0; k < NSEQ; k++) {
		mkstate();
		if (!step(SEQLEN) || !allsame())
			goto fail;
	}

	segcall(SEG_PUT, (long) sc);
	segcall(SEG_PUT, (long) sa);
	cputs("ZRUNT: ");
	putdec(ncase);
	cputs(" runs agree\r\n");
	cputs("ZRUNT: ok\r\n");
	return (0);
fail:
	segcall(SEG_PUT, (long) sc);
	segcall(SEG_PUT, (long) sa);
	return (1);
}
