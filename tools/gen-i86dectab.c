/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/*
 * gen-i86dectab.c -- emit the tables src/shim/i86deca.s reads.
 *
 * The records come from the reference decoder's own grid and the group
 * and mod r/m tables from running it, so renumbering I_* in i86.h means
 * re-running this and pasting the output over the tables that end
 * i86deca.s.
 *
 *	cc -std=gnu89 -w -DHOSTCC -o build/gen-i86dectab tools/gen-i86dectab.c
 *	build/gen-i86dectab
 *
 * Before printing anything it decodes a sweep through a model of the
 * assembly built on these tables and through i86dec(), and refuses if
 * any field differs.
 */

#include <stdio.h>
#include <string.h>

#include "../src/shim/i86dec.c"

/* First-level handlers, the continuations after a mod r/m byte, and the
 * mod r/m paths, named as the assembly's labels. */
enum { H_NONE, H_P, H_M, H_IB, H_IW, H_J8, H_JW, H_DA, H_FP,
       C_DONE, C_X, C_X1, C_XIB, C_XIW, C_XSB, C_IB, C_IW, C_SR, C_G3,
       C_FE, C_FF,
       T_REG, T_0DS, T_0SS, T_DIR, T_8DS, T_8SS, T_16DS, T_16SS };
static char *lab[] = { "h0", "hP", "hM", "hIB", "hIW", "hJ8", "hJW", "hDA",
	"hFP", "cdone", "cX", "cX1", "cXIB", "cXIW", "cXSB", "cIB", "cIW",
	"cSR", "cG3", "cFE", "cFF", "mreg", "m0ds", "m0ss", "mdir", "m8ds",
	"m8ss", "m16ds", "m16ss" };

struct rec {
	int len, op, fl, w, x, mod, reg, rm, seg, pad, cont, imm, h;
};
static struct rec R[256];
static int Mrr[256], Mt[256];
static int fe[8][2], ff[8][2], g3[8][2];

static int isz[8] = { 0, 1, 2, 1, 1, 2, 2, 4 };

static unsigned char mem[65536];

/* Decode bytes b[0..n) placed at offset 0 of a zeroed buffer. */
static void dec(unsigned char *b, int n, struct i86in *in)
{
	memset(mem, 0, 16);
	memcpy(mem, b, n);
	i86dec((char *) mem, 0, in);
}

/* Prefix info: the fl bit, and for a segment override the segment in
 * the top two bits. */
static int pinfo(int o)
{
	switch (o) {
	case 0x26: case 0x2e: case 0x36: case 0x3e:
		return (IN_SEGOVR | (((o >> 3) & 3) << 6));
	case 0xf0: case 0xf1: return (IN_LOCK);
	case 0xf2: return (IN_REPNE);
	case 0xf3: return (IN_REP);
	}
	return (0);
}

static int cont(int o, int f)
{
	struct i86in in;
	unsigned char b[2];

	if (f & D_O) {
		switch (o) {
		case 0xfe: return (C_FE);
		case 0xff: return (C_FF);
		case 0xf6: case 0xf7: return (C_G3);
		case 0x8c: case 0x8e: return (C_SR);
		case 0xd0: case 0xd1: case 0xd2: case 0xd3:
			b[0] = o, b[1] = 0xc0;
			dec(b, 2, &in);
			return (in.imm2 ? C_X1 : C_X);
		}
	} else if (f & D_X) {
		switch (f & D_IMM) {
		case 0: return (C_X);
		case D_IB: return (C_XIB);
		case D_IW: return (C_XIW);
		case D_SB: return (C_XSB);
		}
	} else {
		switch (f & D_IMM) {
		case 0: return (C_DONE);
		case D_IB: return (C_IB);
		case D_IW: return (C_IW);
		}
	}
	fprintf(stderr, "opcode %02X: no continuation for form %04X\n", o, f);
	return (-1);
}

static int build(void)
{
	static int hk[8] = { H_NONE, H_IB, H_IW, -1, H_J8, H_JW, H_DA, H_FP };
	struct i86in in;
	unsigned char b[2];
	int o, f, k, r;

	for (o = 0; o < 256; o++) {
		struct rec *p = &R[o];

		f = bform[o];
		k = f & D_IMM;
		memset(p, 0, sizeof *p);
		p->op = bop[o];
		p->x = bx[o];
		p->seg = S_DS;
		if (ispfx[o]) {
			p->len = 1;
			p->pad = pinfo(o);
			p->h = H_P;
			continue;
		}
		p->len = 1 + isz[k];
		p->w = (f >> 10) & 1;
		p->fl = (f >> 2) & (IN_IMM | IN_DIR);
		if ((f & D_M) && (f & (D_3 | D_L))) {
			fprintf(stderr, "opcode %02X: mod r/m and a register\n", o);
			return (0);
		}
		if (f & D_3)
			p->mod = 3;
		if (f & D_L)
			p->rm = o & 7;
		if (f & D_M) {
			p->h = H_M;
			p->cont = cont(o, f);
			if (p->cont < 0)
				return (0);
			continue;
		}
		p->h = hk[k];
		if (p->h < 0) {
			fprintf(stderr, "opcode %02X: SB without mod r/m\n", o);
			return (0);
		}
		if (k == D_DA) {
			p->fl |= IN_MODRM | IN_MEM;
			p->rm = 6;
		}
		if (f & D_O) {		/* a constant the tail supplies */
			b[0] = o;
			dec(b, 1, &in);
			p->imm = in.imm;
		}
	}

	for (o = 0; o < 256; o++) {
		b[0] = 0x88, b[1] = o;
		dec(b, 2, &in);
		Mrr[o] = in.reg << 8 | in.rm;
		if (in.mod == 3)
			Mt[o] = T_REG;
		else if (in.mod == 0 && in.rm == 6)
			Mt[o] = T_DIR;
		else
			Mt[o] = (in.mod == 0 ? T_0DS : in.mod == 1 ? T_8DS : T_16DS)
				+ (in.seg == S_SS);
	}

	for (r = 0; r < 8; r++) {
		b[1] = 0xc0 | r << 3;
		b[0] = 0xfe, dec(b, 2, &in), fe[r][0] = in.op, fe[r][1] = in.x;
		b[0] = 0xff, dec(b, 2, &in), ff[r][0] = in.op, ff[r][1] = in.x;
		b[0] = 0xf6, dec(b, 2, &in), g3[r][0] = in.op, g3[r][1] = in.x;
	}
	return (1);
}

/* ---- the assembly, in C: every step below is one of its paths. */

static int fbm(i16 p)
{
	return (mem[p] & 0xff);
}

static int model(i16 ip, struct i86in *in)
{
	struct rec r;
	i16 p, t;
	int left, info, acc, sinfo, c, g;

	p = ip;
	r = R[fbm(p)];
	if (r.h == H_P) {
		acc = sinfo = 0;
		for (left = I86MAXPFX; left > 0; left--) {
			info = R[fbm(p)].pad;
			if (!info)
				break;
			acc |= info;
			if (info & IN_SEGOVR)
				sinfo = info;
			p++;
		}
		r = R[fbm(p)];
		r.fl |= acc & (IN_SEGOVR | IN_REP | IN_REPNE | IN_LOCK);
		if (acc & IN_SEGOVR)
			r.seg = sinfo >> 6;
		r.len += (i16)(p - ip);
		if (r.h == H_P)
			r.h = H_NONE;
	}
	in->disp = 0;
	in->imm = r.imm;
	in->imm2 = 0;
	switch (r.h) {
	case H_NONE:
		break;
	case H_IB:
		in->imm = fbm(p + 1);
		break;
	case H_IW:
		in->imm = fbm(p + 1) | fbm(p + 2) << 8;
		break;
	case H_J8:
		in->disp = sx(fbm(p + 1)) + p + 2;
		break;
	case H_JW:
		in->disp = (fbm(p + 1) | fbm(p + 2) << 8) + p + 3;
		break;
	case H_DA:
		in->disp = fbm(p + 1) | fbm(p + 2) << 8;
		break;
	case H_FP:
		in->imm = fbm(p + 1) | fbm(p + 2) << 8;
		in->imm2 = fbm(p + 3) | fbm(p + 4) << 8;
		break;
	case H_M:
		g = fbm(p + 1);
		r.reg = Mrr[g] >> 8;
		r.rm = Mrr[g] & 0xff;
		t = Mt[g];
		r.len += t == T_REG || t == T_0DS || t == T_0SS ? 1 :
			t == T_8DS || t == T_8SS ? 2 : 3;
		r.fl |= t == T_REG ? IN_MODRM : IN_MODRM | IN_MEM;
		r.mod = t == T_REG ? 3 : t == T_8DS || t == T_8SS ? 1 :
			t == T_16DS || t == T_16SS ? 2 : 0;
		if ((t == T_0SS || t == T_8SS || t == T_16SS)
		    && !(r.fl & IN_SEGOVR))
			r.seg = S_SS;
		if (t == T_8DS || t == T_8SS) {
			in->disp = sx(fbm(p + 2));
			p += 1;
		} else if (t == T_DIR || t == T_16DS || t == T_16SS) {
			in->disp = fbm(p + 2) | fbm(p + 3) << 8;
			p += 2;
		}
		c = r.cont;
		switch (c) {
		case C_DONE:
			break;
		case C_X:
			r.x = r.reg;
			break;
		case C_X1:
			r.x = r.reg;
			in->imm2 = 1;
			break;
		case C_XIB:
			r.x = r.reg;
			/* FALLTHROUGH */
		case C_IB:
			in->imm = fbm(p + 2);
			break;
		case C_XIW:
			r.x = r.reg;
			/* FALLTHROUGH */
		case C_IW:
			in->imm = fbm(p + 2) | fbm(p + 3) << 8;
			break;
		case C_XSB:
			r.x = r.reg;
			in->imm = sx(fbm(p + 2));
			break;
		case C_SR:
			r.x = r.reg & 3;
			break;
		case C_FE:
			r.op = fe[r.reg][0], r.x = fe[r.reg][1];
			break;
		case C_FF:
			r.op = ff[r.reg][0], r.x = ff[r.reg][1];
			break;
		case C_G3:
			r.op = g3[r.reg][0], r.x = g3[r.reg][1];
			if (r.reg <= 1) {
				r.fl |= IN_IMM;
				in->imm = fbm(p + 2);
				r.len++;
				if (r.w) {
					in->imm |= fbm(p + 3) << 8;
					r.len++;
				}
			}
			break;
		}
		break;
	}
	in->len = r.len;
	in->op = r.op;
	in->fl = r.fl;
	in->w = r.w;
	in->x = r.x;
	in->mod = r.mod;
	in->reg = r.reg;
	in->rm = r.rm;
	in->seg = r.seg;
	return (r.len);
}

static int same(struct i86in *a, struct i86in *c)
{
	return (a->len == c->len && a->op == c->op && a->fl == c->fl
		&& a->w == c->w && a->x == c->x && a->mod == c->mod
		&& a->reg == c->reg && a->rm == c->rm && a->seg == c->seg
		&& a->disp == c->disp && a->imm == c->imm && a->imm2 == c->imm2);
}

/* Prefix runs, as a count of bytes then the bytes; 15 and 16 cross the
 * I86MAXPFX cap. */
static unsigned char pfx[][17] = {
	{ 0 }, { 1, 0x26 }, { 1, 0x2e }, { 1, 0x36 }, { 1, 0x3e },
	{ 1, 0xf0 }, { 1, 0xf1 }, { 1, 0xf2 }, { 1, 0xf3 },
	{ 2, 0xf3, 0x26 }, { 3, 0x36, 0xf2, 0x2e },
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
static unsigned char tail[3][4] = {
	{ 0x00, 0x00, 0x00, 0x00 },
	{ 0xff, 0xff, 0xff, 0xff },
	{ 0x80, 0x7f, 0x01, 0xfe }
};
static i16 ips[4] = { 0x0000, 0x1234, 0xfff0, 0xfffd };

static int check(void)
{
	struct i86in a, c;
	int np = sizeof pfx / sizeof pfx[0];
	int p, t, k, o, g, j, n, ra, rc;
	i16 ip, q;
	long nd = 0;

	for (k = 0; k < 4; k++)
	for (p = 0; p < np; p++)
	for (t = 0; t < 3; t++)
	for (o = 0; o < 256; o++)
	for (g = 0; g < 256; g++) {
		ip = ips[k];
		q = ip;
		n = pfx[p][0];
		for (j = 0; j < n; j++)
			mem[q++] = pfx[p][1 + j];
		mem[q++] = o;
		mem[q++] = g;
		for (j = 0; j < 4; j++)
			mem[q++] = tail[t][j];
		memset(&a, 0, sizeof a);
		memset(&c, 0, sizeof c);
		ra = model(ip, &a);
		rc = i86dec((char *) mem, ip, &c);
		if (ra != rc || !same(&a, &c)) {
			fprintf(stderr, "differ: ip %04X prefixes %d op %02X %02X tail %d\n",
				ip, p, o, g, t);
			return (0);
		}
		nd++;
	}
	fprintf(stderr, "%ld decodes agree\n", nd);
	return (1);
}

static void grp(char *name, int t[8][2])
{
	int r;

	printf("%s:\t.byte\t", name);
	for (r = 0; r < 8; r++)
		printf("%d,%d%s", t[r][0], t[r][1], r < 7 ? ", " : "\n");
}

static void emit(void)
{
	int o;
	struct rec *p;

	printf("\t.even\nrtab:\n");
	for (o = 0; o < 256; o++) {
		p = &R[o];
		printf("\tR(%d,%d,%d,%d,%d,%d,%d,%d,%s,%d,%s)\t/ %02X\n",
			p->len, p->op, p->fl, p->w, p->x, p->mod, p->rm,
			p->pad, lab[p->cont], p->imm, lab[p->h], o);
	}
	printf("mtab:\n");
	for (o = 0; o < 256; o += 4)
		printf("\tM(0x%04x,%s)\tM(0x%04x,%s)\tM(0x%04x,%s)\tM(0x%04x,%s)"
			"\t/ %02X\n", Mrr[o], lab[Mt[o]], Mrr[o + 1],
			lab[Mt[o + 1]], Mrr[o + 2], lab[Mt[o + 2]], Mrr[o + 3],
			lab[Mt[o + 3]], o);
	grp("fetab", fe);
	grp("fftab", ff);
	grp("g3tab", g3);
}

int main()
{
	if (!build() || !check())
		return (1);
	emit();
	return (0);
}
