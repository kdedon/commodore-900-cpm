/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/*
 * irunt.c -- the two 8086 run loops, side by side on the machine.
 *
 * The target build runs i86runa() (i86runa.s), which executes the common
 * classes itself; i86run() in i86exec.c stays the portable reference.
 * From random machine states, each over its own copy of one random 64 KB
 * memory image, both run the same instructions, and this compares the
 * return value, every field of the machine, the counters and the memory.
 *
 * The instructions are mostly the classes the assembly executes, in
 * every mod r/m form, with and without segment overrides, and with
 * registers and IP pushed to the edges so that addresses wrap at 0xFFFF.
 * Half the lazy records are ones an instruction could have left.
 */

#include "cpm.h"
#include "i86.h"

#define NTEST	40000
#define NCMP	16/* tests between whole-image compares	*/

extern int i86nseg;

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

static i32 seed = 1;

static unsigned rnd()
{
	seed = seed * 1103515245L + 12345L;
	return ((unsigned) (seed >> 12) & 0xffff);
}

/* Opcodes of the classes i86runa.s executes. */
static unsigned char hot[] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x08, 0x09, 0x0a, 0x0b,
	0x0c, 0x0d, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x18, 0x19,
	0x1a, 0x1b, 0x1c, 0x1d, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25,
	0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x30, 0x31, 0x32, 0x33,
	0x34, 0x35, 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x80, 0x81,
	0x82, 0x83, 0x80, 0x81, 0x83, 0x83,
	0x88, 0x89, 0x8a, 0x8b, 0x88, 0x89, 0x8a, 0x8b, 0xc6, 0xc7,
	0xa0, 0xa1, 0xa2, 0xa3, 0xb0, 0xb3, 0xb4, 0xb7, 0xb8, 0xbc,
	0x84, 0x85, 0xa8, 0xa9, 0xf6, 0xf7, 0xf6, 0xf7,
	0x86, 0x87, 0x91, 0x94, 0x97,
	0x40, 0x43, 0x44, 0x47, 0x48, 0x49, 0x4c, 0x4f, 0xfe, 0xff,
	0xfe, 0xff, 0x50, 0x53, 0x54, 0x57, 0x58, 0x5b, 0x5c, 0x5f,
	0x8f, 0xe8, 0xc3, 0xc2, 0xe9, 0xeb, 0xe0, 0xe1, 0xe2, 0xe3,
	0x8d, 0x8d, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77,
	0x78, 0x79, 0x7a, 0x7b, 0x7c, 0x7d, 0x7e, 0x7f, 0x70, 0x71,
	0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x7b,
	0x7c, 0x7d, 0x7e, 0x7f, 0xd0, 0xd1, 0xd2, 0xd3, 0xd0, 0xd1,
	0xd0, 0xd1, 0xd0, 0xd1, 0xd0, 0xd1, 0x90, 0x98, 0x99, 0x9e,
	0x9f, 0xf5, 0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd
};
#define NHOTOP	(sizeof hot / sizeof hot[0])

static i16 edge[] = {
	0, 1, 2, 0xffff, 0xfffe, 0xfffd, 0x7fff, 0x8000,
	0x007f, 0x0080, 0x00ff, 0x0100, 0x7ffe, 0x8001, 0xff7f, 0xff80
};

static struct i86 t, c, a;
static struct i86in inc, ina;
static char *ma, *mc;
static i16 soff[4];

static VOID copy(d, s, n)
register char *d, *s;
register int n;
{
	while (n-- > 0)
		*d++ = *s++;
}

static i16 rval()
{
	return ((rnd() & 3) == 0 ? edge[rnd() & 15] : (i16) rnd());
}

/* Instruction bytes at CS:p in both images; returns the next p. */
static i16 put(p)
i16 p;
{
	register int j, k;
	unsigned char b[10];
	int n;

	n = 0;
	k = rnd() & 31;
	if (k < 3)
		b[n++] = 0x26 + ((rnd() & 3) << 3);
	else if (k == 3)
		b[n++] = rnd() & 1 ? 0xf0 : 0xf3;
	k = rnd() & 31;
	if (k == 0) {
		/* a delay loop, or nearly one */
		j = rnd() & 7;
		if (rnd() & 1) {
			b[n++] = 0x48 + j;
			b[n++] = 0x75;
			b[n++] = rnd() & 3 ? 0xfd : rnd();
		} else {
			b[n++] = 0xfe + (rnd() & 1);
			b[n++] = 0xc8 + j;
			b[n++] = 0x75;
			b[n++] = rnd() & 3 ? 0xfc : rnd();
		}
	} else if (k == 1)
		b[n++] = rnd();
	else {
		b[n++] = hot[rnd() % NHOTOP];
		b[n] = rnd();
		/* a register mod r/m byte a quarter of the time */
		if ((rnd() & 3) == 0)
			b[n] |= 0xc0;
		n++;
	}
	while (n < 10)
		b[n++] = rnd();
	for (j = 0; j < 10; j++) {
		mc[(i16) (p + j)] = b[j];
		ma[(i16) (p + j)] = b[j];
	}
	i86dec(mc, p, &inc);
	return ((i16) (p + (inc.len ? inc.len : 1)));
}

static VOID gen()
{
	register int i;
	i16 p;
	int n;

	for (i = 0; i < 8; i++)
		t.r[i] = rval();
	if ((rnd() & 7) == 0)
		t.r[R_SP] = edge[rnd() & 7];
	for (i = 0; i < 4; i++) {
		t.sr[i] = rnd();
		t.so[i] = 0;
		soff[i] = i == S_CS ? 0 : rnd();
	}
	if ((rnd() & 15) == 0)
		t.so[(rnd() & 1) ? S_DS : (rnd() & 1) ? S_SS : S_ES] = rnd() | 1;
	t.ip = rnd() & 7 ? rnd() : 0xfff8 + (rnd() & 7);
	t.fl = rnd() & ~F_TF;
	if ((rnd() & 31) == 0)
		t.fl |= F_TF;
	t.lz = rnd() % 6;
	t.lw = rnd() & 1;
	t.lc = rnd() & 1;
	t.la = rval();
	t.lb = rval();
	t.lr = rval();
	/* half the time a record an instruction could have left, some
	 * with the result on the carry's edge */
	if ((rnd() & 7) == 0)
		t.lb = t.lz == LZ_SUB ? t.la : 0;
	if (rnd() & 1)
		switch (t.lz) {
		case LZ_ADD:
			t.lr = t.la + t.lb + t.lc;
			break;
		case LZ_SUB:
			t.lr = t.la - t.lb - t.lc;
			break;
		case LZ_LOG:
			t.lc = 0;
			t.lr = t.la & t.lb;
			break;
		case LZ_INC:
			t.lc = 0;
			t.lb = 1;
			t.lr = t.la + 1;
			break;
		case LZ_DEC:
			t.lc = 0;
			t.lb = 1;
			t.lr = t.la - 1;
			break;
		}
	t.halt = rnd() & 1;
	t.fault = rnd() & 1;
	t.foff = rnd();
	t.fseg = rnd() & 3;
	t.wseg = rnd();
	t.wset = (rnd() & 7) == 0;
	n = rnd() & 7 ? 1 : 1 + (rnd() & 7);
	p = t.ip;
	for (i = 0; i < n; i++)
		p = put(p);
	i86nrun = n;
}

static VOID bad(k, what, av, cv)
unsigned k;
char *what;
unsigned av, cv;
{
	register int j;

	cputs("IRUNT: FAIL test ");
	putdec(k);
	cputs(" ");
	cputs(what);
	cputs(" asm=");
	phex4(av);
	cputs(" c=");
	phex4(cv);
	cputs("\r\nIRUNT:      ip=");
	phex4(t.ip);
	cputs(" bytes");
	for (j = 0; j < 8; j++) {
		conout(' ');
		phex2(mc[(i16) (t.ip + j)] & 0xff);
	}
	cputs("\r\nIRUNT:      from r");
	for (j = 0; j < 8; j++) {
		conout(' ');
		phex4(t.r[j]);
	}
	cputs(" fl ");
	phex4(t.fl);
	cputs(" lz ");
	phex2(t.lz);
	phex2(t.lw);
	phex2(t.lc);
	conout(' ');
	phex4(t.la);
	conout(' ');
	phex4(t.lb);
	conout(' ');
	phex4(t.lr);
	cputs("\r\n");
}

#define CHK(f, s) if ((a.f) != (c.f)) { bad(k, s, (unsigned) (a.f), \
			(unsigned) (c.f)); return (0); }

static int same(k)
unsigned k;
{
	register int i;

	for (i = 0; i < 8; i++)
		CHK(r[i], "r");
	for (i = 0; i < 4; i++) {
		CHK(sr[i], "sr");
		CHK(so[i], "so");
		if ((i16) ((long) a.sb[i] - (long) ma)
		 != (i16) ((long) c.sb[i] - (long) mc)) {
			bad(k, "sb", i, i);
			return (0);
		}
	}
	CHK(ip, "ip");
	CHK(fl, "fl");
	CHK(lz, "lz");
	CHK(lw, "lw");
	CHK(lc, "lc");
	CHK(la, "la");
	CHK(lb, "lb");
	CHK(lr, "lr");
	CHK(halt, "halt");
	CHK(fault, "fault");
	CHK(foff, "foff");
	CHK(fseg, "fseg");
	CHK(wseg, "wseg");
	CHK(wset, "wset");
	return (1);
}

/* 0 if the images differ, and where. */
static int image(k)
unsigned k;
{
	register long *p, *q;
	register unsigned n;

	p = (long *) ma;
	q = (long *) mc;
	for (n = 0; n < 16384; n++)
		if (*p++ != *q++) {
			cputs("IRUNT: FAIL memory differs at ");
			phex4((n << 2));
			cputs(" after test ");
			putdec(k);
			cputs("\r\n");
			return (0);
		}
	return (1);
}

int main(argc, argv)
int argc;
char *argv[];
{
	register unsigned k;
	register int i;
	int sa, sc, ra, rc, no;
	i16 nr, n;
	i32 ni, nf, ns;
	long xa;

	cputs("IRUNT: start\r\n");
	sc = (int) segcall(SEG_GET, 0L);
	sa = (int) segcall(SEG_GET, 0L);
	if (sc == 0 || sa == 0) {
		cputs("IRUNT: no two 64 KB segments to run in\r\n");
		return (1);
	}
	xa = SEGBASE(sc);
	mc = (char *) xa;
	xa = SEGBASE(sa);
	ma = (char *) xa;
	i86nseg = 0;
	for (k = 0; k < 32768; k++) {
		i = rnd();
		mc[k] = i;
		ma[k] = i;
		mc[k + 32768] = i >> 8;
		ma[k + 32768] = i >> 8;
	}
	for (k = 1; k <= NTEST; k++) {
		gen();
		nr = i86nrun;
		copy((char *) &c, (char *) &t, sizeof t);
		copy((char *) &a, (char *) &t, sizeof t);
		for (i = 0; i < 4; i++) {
			c.sb[i] = mc + soff[i];
			a.sb[i] = ma + soff[i];
		}
		i86ninsn = i86nflag = i86nskip = 0;
		i86intno = 0;
		rc = i86run(&c, &inc);
		ni = i86ninsn;
		nf = i86nflag;
		ns = i86nskip;
		no = i86intno;
		n = i86nrun;
		i86ninsn = i86nflag = i86nskip = 0;
		i86intno = 0;
		i86nrun = nr;
		ra = i86runa(&a, &ina);
		if (ra != rc) {
			bad(k, "return", ra, rc);
			return (1);
		}
		if (i86nrun != n) {
			bad(k, "i86nrun", i86nrun, n);
			return (1);
		}
		if (i86ninsn != ni) {
			bad(k, "i86ninsn", (unsigned) i86ninsn, (unsigned) ni);
			return (1);
		}
		if (i86nflag != nf) {
			bad(k, "i86nflag", (unsigned) i86nflag, (unsigned) nf);
			return (1);
		}
		if (i86nskip != ns || i86intno != no) {
			bad(k, "i86nskip/intno", (unsigned) i86nskip,
				(unsigned) ns);
			return (1);
		}
		if (!same(k))
			return (1);
		if (k % NCMP == 0 && !image(k))
			return (1);
	}
	segcall(SEG_PUT, (long) sa);
	segcall(SEG_PUT, (long) sc);
	cputs("IRUNT: ");
	putdec((unsigned) NTEST);
	cputs(" states agree\r\n");
	cputs("IRUNT: ok\r\n");
	return (0);
}
