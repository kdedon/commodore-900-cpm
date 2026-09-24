/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/*
 * gsxt.c - GSX parameter blocks against pictures and tables worked out
 * by hand.
 *
 * Each scene starts from a cleared workstation, draws through gsx() or
 * gsxdrv(), and compares every byte of the surface, split mid-raster
 * across two buffers, with an image built pixel by pixel here, plus
 * guard bytes past each buffer.  intout and ptsout are checked against
 * literal values.
 */

#include "cpm.h"
#include "gfx.h"
#include "gsx.h"

extern unsigned char	gsxfont[];

#define	GUARD	4
#define	SENT	0xA5
#define	MAXB	320

static unsigned char	b0[MAXB + GUARD];
static unsigned char	b1[MAXB + GUARD];
static unsigned char	img[MAXB];

static struct gfxsurf	surf;
static struct gsxws	ws;
static int		sw, sh, sbpp, sstr, total, split;

static int	contrl[12], intin[64], ptsin[64], intout[64], ptsout[16];
static struct gsxpb	pb = { contrl, intin, ptsin, intout, ptsout };

static int	bad;
static long	checks;

static VOID pint(v)
int v;
{
	if (v < 0) {
		conout('-');
		v = -v;
	}
	putdec(v);
}

static VOID fail(what, n, a, b)
char *what;
int n, a, b;
{
	cputs("GSXT: FAIL ");
	cputs(what);
	cputs(" bpp=");
	pint(sbpp);
	cputs(" split=");
	pint(split);
	cputs(" at=");
	pint(n);
	cputs(" got=");
	pint(a);
	cputs(" want=");
	pint(b);
	cputs("\r\n");
	bad++;
}

static VOID want(what, n, a, b)
char *what;
int n, a, b;
{
	checks++;
	if (a != b)
		fail(what, n, a, b);
}

/* ---- the expected image, rasters y down ---- */

static VOID eset(x, y, v)
int x, y, v;
{
	register unsigned char	*p;
	int			sh4;

	if (x < 0 || y < 0 || x >= sw || y >= sh)
		return;
	if (sbpp == 1) {
		p = &img[y * sstr + x / 8];
		if (v)
			*p |= 0x80 >> (x % 8);
		else
			*p &= ~(0x80 >> (x % 8));
	} else {
		p = &img[y * sstr + x / 2];
		sh4 = x % 2 ? 0 : 4;
		*p = (*p & ~(15 << sh4)) | (v << sh4);
	}
}

static VOID eall(v)
int v;
{
	register int	x, y;

	for (y = 0; y < sh; y++)
		for (x = 0; x < sw; x++)
			eset(x, y, v);
}

static VOID erow(x0, x1, y, v)
int x0, x1, y, v;
{
	for (; x0 <= x1; x0++)
		eset(x0, y, v);
}

static int sbyte(k)
int k;
{
	return (k < split ? b0[k] : b1[k - split]);
}

static VOID compare(what)
char *what;
{
	register int	k;

	for (k = 0; k < total; k++) {
		checks++;
		if (sbyte(k) != img[k]) {
			fail(what, k, sbyte(k), img[k]);
			return;
		}
	}
	for (k = 0; k < GUARD; k++)
		if (b0[split + k] != SENT || b1[total - split + k] != SENT) {
			fail("guard", k, b0[split + k], b1[total - split + k]);
			return;
		}
}

/* ---- calling the entries ---- */

static int call(ndc, op, nv, ni, id)
int ndc, op, nv, ni, id;
{
	contrl[0] = op;
	contrl[1] = nv;
	contrl[2] = 99;
	contrl[3] = ni;
	contrl[4] = 99;
	contrl[5] = id;
	return (ndc ? gsx(&ws, &pb) : gsxdrv(&ws, &pb));
}

static VOID pts(n, a, b, c, d)
int n, a, b, c, d;
{
	ptsin[2 * n] = a;
	ptsin[2 * n + 1] = b;
	ptsin[2 * n + 2] = c;
	ptsin[2 * n + 3] = d;
}

static VOID set1(op, v)
int op, v;
{
	intin[0] = v;
	call(0, op, 0, 1, 0);
}

static VOID text(x, y, s)
int x, y;
char *s;
{
	register int	n;

	for (n = 0; s[n]; n++)
		intin[n] = s[n];
	pts(0, x, y, 0, 0);
	call(0, 8, 1, n, 0);
}

static VOID esc(id, ni)
int id, ni;
{
	call(0, 5, 0, ni, id);
}

static VOID clear()
{
	call(0, 3, 0, 0, 0);
	eall(ws.pix[0]);
}

/* Glyph bits of ch at raster (x, y) under v, k times the size. */
static VOID eglyph(ch, x, y, k, v, bgv)
int ch, x, y, k, v, bgv;
{
	register int	r, c;
	int		i, j, bit;

	for (r = 0; r < 8; r++)
		for (c = 0; c < 8; c++) {
			bit = (gsxfont[8 * ch + r] >> (7 - c)) & 1;
			for (i = 0; i < k; i++)
				for (j = 0; j < k; j++)
					eset(x + k * c + j, y + k * r + i,
					    bit ? v : bgv);
		}
}

static VOID surface(bpp, w, h, str, sp)
int bpp, w, h, str, sp;
{
	register int	k;

	sbpp = bpp;
	sw = w;
	sh = h;
	sstr = str;
	total = h * str;
	split = sp;
	for (k = 0; k < MAXB + GUARD; k++)
		b0[k] = b1[k] = SENT;
	for (k = 0; k < total; k++)
		if (k < split)
			b0[k] = 0x5A;
		else
			b1[k - split] = 0x5A;
	gfx_ram(&surf, w, h, bpp, (unsigned) str, b0, b1, (long) sp);
	gsxinit(&ws, &surf);
}

/* ---- Open Workstation ---- */

static int	owmono[45] = {
	63, 23, 1, 4000, 8000, 8, 8, 1, 8, 1, 1, 8, 8, 2, 1,
	1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	3, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	0, 1, 1, 0, 2, 0, 0, 0, 0, 0
};
static int	owclr[45] = {
	31, 19, 1, 8000, 9600, 8, 8, 1, 8, 1, 1, 8, 8, 16, 1,
	1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	3, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	1, 1, 1, 0, 16, 0, 0, 0, 0, 0
};

/* sizes in device units (7, 7), (56, 56), (1, 0), (1, 0), (0, 7),
   (0, 7) scaled back by GDOS: ((d << 16) + 2n - 1) / n / 2 */
static int	psmono[12] = {
	3584, 9558, 28672, 32767, 512, 0, 512, 0, 0, 9558, 0, 9558
};
static int	psclr[12] = {
	7168, 11469, 32767, 32767, 1024, 0, 1024, 0, 0, 11469, 0, 11469
};

static VOID openws(ow, ps)
int *ow, *ps;
{
	register int	i;

	for (i = 0; i < 10; i++)
		intin[i] = 1;
	intin[7] = 0;
	want("open status", 0, call(1, 1, 0, 10, 0), GSX_OK);
	want("open ptsout count", 0, contrl[2], 6);
	want("open intout count", 0, contrl[4], 45);
	for (i = 0; i < 45; i++)
		want("open intout", i, intout[i], ow[i]);
	for (i = 0; i < 12; i++)
		want("open ptsout", i, ptsout[i], ps[i]);
	eall(0);
	compare("open clears");
}

/* ---- mono, 64 x 24 ---- */

static VOID mono(sp)
int sp;
{
	register int	i, y;

	surface(1, 64, 24, 8, sp);
	openws(owmono, psmono);

	/* x = v / 512; y = 2v * 24 >> 16, so 15360 is device 11, raster 12 */
	clear();
	pts(0, 0, 15360, 32767, 15360);
	call(1, 6, 2, 0, 0);
	erow(0, 63, 12, 1);
	pts(0, 8192, 0, 8192, 32767);
	call(1, 6, 2, 0, 0);
	for (y = 0; y < 24; y++)
		eset(16, y, 1);
	compare("ndc lines");

	/* dashed, 10 on 6 off, clear pixels to background: x 0..19 */
	clear();
	set1(15, 2);
	pts(0, 0, 32767, 9728, 32767);
	call(1, 6, 2, 0, 0);
	erow(0, 9, 0, 1);
	erow(16, 19, 0, 1);
	compare("dashed");
	set1(15, 1);

	/* a solid triangle: row y fills x 8 .. 8 + y */
	clear();
	set1(23, 1);
	pts(0, 8, 23, 8, 16);
	pts(2, 15, 16, 8, 23);
	call(0, 9, 3, 0, 0);
	for (y = 0; y < 8; y++)
		erow(8, 8 + y, y, 1);
	compare("triangle");

	/* a bar in the eight-level dither, outlined */
	clear();
	set1(23, 2);
	set1(24, 4);
	pts(0, 0, 23, 15, 20);
	call(0, 11, 2, 0, 1);
	erow(0, 15, 0, 1);
	erow(0, 15, 3, 1);
	for (i = 0; i < 16; i += 2) {
		eset(i, 1, 1);
		eset(i + 1, 2, 1);
	}
	eset(15, 1, 1);
	eset(0, 2, 1);
	compare("dither bar");
	set1(23, 0);

	/* a hollow square in XOR: its first corner is not undone */
	clear();
	set1(32, 3);
	pts(0, 2, 2, 6, 2);
	pts(2, 6, 6, 2, 6);
	call(0, 9, 4, 0, 0);
	erow(2, 6, 17, 1);
	erow(2, 6, 21, 1);
	for (y = 18; y < 21; y++) {
		eset(2, y, 1);
		eset(6, y, 1);
	}
	compare("xor square");
	call(0, 9, 4, 0, 0);
	eall(0);
	compare("xor square twice");
	set1(32, 1);

	/* text: rows land at raster y + 9; "AB" across the split */
	clear();
	text(36, 8, "AB");
	eglyph('A', 36, 9, 1, 1, 0);
	eglyph('B', 44, 9, 1, 1, 0);
	compare("text");

	/* twice the size: rows 2r and 2r + 1 from raster 10 */
	clear();
	pts(0, 0, 14, 0, 0);
	call(0, 12, 1, 0, 0);
	want("height ptsout", 0, ptsout[1], 14);
	want("height ptsout", 1, ptsout[3], 16);
	want("height count", 0, contrl[2], 2);
	text(0, 0, "T");
	eglyph('T', 0, 10, 2, 1, 0);
	compare("text x2");
	pts(0, 0, 7, 0, 0);
	call(0, 12, 1, 0, 0);

	/* up vector 90: glyph (c, r) at raster (24 + r, 21 - c) */
	clear();
	set1(13, 1000);
	want("up vector", 0, intout[0], 900);
	text(30, 2, "L");
	for (y = 0; y < 8; y++)
		for (i = 0; i < 8; i++)
			eset(24 + y, 21 - i,
			    (gsxfont[8 * 'L' + y] >> (7 - i)) & 1);
	compare("text 90");
	set1(13, 0);

	/* the writing modes on a lit bar */
	clear();
	set1(23, 1);
	pts(0, 0, 23, 63, 0);
	call(0, 11, 2, 0, 1);
	set1(22, 0);
	set1(32, 2);
	text(0, 14, "A");
	eall(1);
	eglyph('A', 0, 3, 1, 0, 1);
	compare("transparent");
	set1(22, 1);
	set1(32, 4);
	text(16, 14, "A");
	eglyph('A', 16, 3, 1, 0, 1);
	compare("erase");
	set1(32, 3);
	text(16, 14, "A");
	eglyph('A', 16, 3, 1, 1, 1);
	compare("complement");
	set1(32, 1);
	set1(22, 0);
	text(32, 14, "A");
	erow(32, 39, 3 + 0, 0);
	for (y = 0; y < 8; y++)
		erow(32, 39, 3 + y, 0);
	compare("replace in 0");
	set1(22, 1);
	set1(23, 0);

	/* a '+' marker centred on device (20, 11): rows 9.., byte 2 */
	clear();
	set1(18, 2);
	pts(0, 20, 11, 0, 0);
	call(0, 7, 1, 0, 0);
	for (y = 0; y < 7; y++)
		for (i = 0; i < 8; i++)
			eset(16 + i, 9 + y,
			    (gsxfont[8 * 17 + y] >> (7 - i)) & 1);
	compare("marker");

	/* the alpha screen: 3 rows of 8 cells */
	clear();
	esc(1, 0);
	want("esc 1 rows", 0, intout[0], 3);
	want("esc 1 cols", 0, intout[1], 8);
	intin[0] = 2;
	intin[1] = 6;
	esc(11, 2);
	intin[0] = 'H';
	intin[1] = 'i';
	esc(12, 2);
	esc(15, 0);
	want("esc 15 row", 0, intout[0], 2);
	want("esc 15 col", 0, intout[1], 8);
	esc(13, 0);
	intin[0] = '!';
	esc(12, 1);
	esc(14, 0);
	esc(15, 0);
	want("esc 15 wrap", 0, intout[0] * 10 + intout[1], 31);
	eglyph('H', 40, 8, 1, 1, 0);
	eglyph('i', 48, 8, 1, 1, 0);
	eglyph('!', 56, 8, 1, 0, 1);
	compare("alpha text");
	esc(4, 0);
	esc(7, 0);
	esc(7, 0);
	esc(5, 0);
	esc(6, 0);
	esc(15, 0);
	want("esc motion", 0, intout[0] * 10 + intout[1], 32);
	esc(8, 0);
	esc(15, 0);
	want("esc home", 0, intout[0] * 10 + intout[1], 11);

	/* erase to end of screen from (2, 1), to end of line from (1, 5) */
	set1(23, 1);
	pts(0, 0, 23, 63, 0);
	call(0, 11, 2, 0, 1);
	set1(23, 0);
	intin[0] = 2;
	intin[1] = 1;
	esc(11, 2);
	esc(9, 0);
	intin[0] = 1;
	intin[1] = 5;
	esc(11, 2);
	esc(10, 0);
	eall(0);
	for (y = 0; y < 8; y++)
		erow(0, 31, y, 1);
	compare("erase escapes");

	/* the graphic cursor at device (20, 11), raster (20, 12) */
	clear();
	pts(0, 20, 11, 0, 0);
	esc(18, 0);
	erow(12, 28, 12, 1);
	for (y = 4; y <= 20; y++)
		eset(20, y, 1);
	compare("graphic cursor");
	esc(19, 0);
	eall(0);
	compare("cursor removed");

	want("esc 16", 0, (esc(16, 0), intout[0]), 0);
	want("esc 17", 0, call(0, 5, 0, 0, 17), GSX_NA);
	want("esc 20", 0, call(0, 5, 0, 0, 20), GSX_NA);

	/* colour representation, by luminance */
	intin[0] = 1;
	intin[1] = intin[2] = intin[3] = 200;
	call(0, 14, 0, 4, 0);
	want("rep count", 0, contrl[4], 4);
	want("rep chose", 0, intout[1] + intout[2] + intout[3], 0);
	intin[0] = 1;
	intin[1] = 1;
	call(0, 26, 0, 2, 0);
	want("inq realized", 0, intout[1] + intout[2] + intout[3], 0);
	intin[1] = 0;
	call(0, 26, 0, 2, 0);
	want("inq asked", 0, intout[1] + intout[2] + intout[3], 600);
	intin[0] = 0;
	intin[1] = 1000;
	intin[2] = 900;
	intin[3] = 0;
	call(0, 14, 0, 4, 0);
	want("rep white", 0, intout[1] + intout[2] + intout[3], 3000);
	clear();
	eall(1);
	compare("clear in white");
	intin[0] = 2;
	want("rep index 2", 0, call(0, 14, 0, 4, 0), GSX_NA);
	intin[0] = 2;
	call(0, 26, 0, 2, 0);
	want("inq index 2", 0, intout[0], -1);

	/* attributes: what was asked for, and what was set */
	set1(15, 9);
	want("line type 9", 0, intout[0], 1);
	set1(15, 5);
	want("line type 5", 0, intout[0], 5);
	set1(18, 0);
	want("marker 0", 0, intout[0], 3);
	set1(17, 5);
	want("line colour 5", 0, intout[0], 1);
	set1(20, 0);
	want("marker colour 0", 0, intout[0], 0);
	set1(32, 7);
	want("mode 7", 0, intout[0], 1);
	set1(23, 5);
	want("fill style 5", 0, intout[0], 0);
	set1(24, 9);
	want("fill index 9", 0, intout[0], 1);
	set1(13, 3200);
	want("up 3200", 0, intout[0], 0);
	set1(13, 2000);
	want("up 2000", 0, intout[0], 1800);
	set1(21, 4);
	want("font", 0, intout[0], 1);
	want("font count", 0, contrl[4], 1);
	call(1, 16, 1, 0, 0);
	want("line width", 0, ptsout[0], 512);
	want("line width", 1, ptsout[1], 0);

	/* character height through GDOS: 19200 is device 14, scale 2 */
	pts(0, 0, 19200, 0, 0);
	call(1, 12, 1, 0, 0);
	want("ndc height", 0, ptsout[0], 7168);
	want("ndc height", 1, ptsout[1], 19115);
	want("ndc height", 2, ptsout[2], 8192);
	want("ndc height", 3, ptsout[3], 21846);

	for (i = 27; i <= 33; i++)
		if (i != 32) {
			want("input", i, call(0, i, 0, 0, 0), GSX_NA);
			want("input counts", i, contrl[2] + contrl[4], 0);
		}
	want("gdp 4", 0, call(0, 11, 1, 0, 4), GSX_NA);
	want("opcode 40", 0, call(0, 40, 0, 0, 0), GSX_NA);
}

/* ---- colour, 32 x 20 ---- */

/* GSX's first eight, then the other RGBI colours, at CGA levels */
static int	defrgb[48] = {
	0, 0, 0,	667, 0, 0,	0, 667, 0,	0, 0, 667,
	0, 667, 667,	667, 667, 0,	667, 0, 667,	1000, 1000, 1000,
	333, 333, 333,	1000, 333, 333,	333, 1000, 333,	333, 333, 1000,
	333, 1000, 1000, 1000, 1000, 333, 1000, 333, 1000, 667, 667, 667
};

static VOID colour(sp)
int sp;
{
	register int	i, y;

	surface(4, 32, 20, 16, sp);
	openws(owclr, psclr);
	for (i = 0; i < 16; i++) {
		intin[0] = i;
		intin[1] = 1;
		call(0, 26, 0, 2, 0);
		for (y = 0; y < 3; y++)
			want("default colour", i * 10 + y, intout[y + 1],
			    defrgb[3 * i + y]);
	}

	/* index 1 is red, IRGB 4; 15000 is device 9, raster 10 */
	clear();
	pts(0, 0, 15000, 32767, 15000);
	call(1, 6, 2, 0, 0);
	erow(0, 31, 10, 4);
	compare("ndc line");

	/* a blue bar, x 2..5, rasters 0..2 */
	clear();
	set1(23, 1);
	set1(25, 3);
	pts(0, 2, 19, 5, 17);
	call(0, 11, 2, 0, 1);
	for (y = 0; y < 3; y++)
		erow(2, 5, y, 1);
	compare("blue bar");

	/* the nearest RGBI colour */
	intin[0] = 2;
	intin[1] = 900;
	intin[2] = 100;
	intin[3] = 100;
	call(0, 14, 0, 4, 0);
	want("rep red", 0, intout[1], 667);
	want("rep red", 1, intout[2] + intout[3], 0);
	intin[0] = 2;
	intin[1] = 1000;
	intin[2] = 1000;
	intin[3] = 0;
	call(0, 14, 0, 4, 0);
	want("rep yellow", 0, intout[1] + intout[2], 2000);
	want("rep yellow", 1, intout[3], 333);
	set1(17, 2);
	pts(0, 3, 0, 3, 0);
	call(0, 6, 2, 0, 0);
	eset(3, 19, 14);
	compare("yellow pixel");
	intin[0] = 2;
	intin[1] = 0;
	call(0, 26, 0, 2, 0);
	want("inq asked", 0, intout[1] + intout[2] + intout[3], 2000);

	/* text in 5, yellow, IRGB 6 */
	clear();
	set1(22, 5);
	text(8, 12, "A");
	eglyph('A', 8, 1, 1, 6, 0);
	compare("colour text");

	/* the first hatch, x = 0 mod 4, on rasters 13 and 14 of a bar */
	clear();
	set1(23, 3);
	set1(24, 1);
	set1(25, 1);
	pts(0, 0, 7, 15, 4);
	call(0, 11, 2, 0, 1);
	erow(0, 15, 12, 4);
	erow(0, 15, 15, 4);
	for (y = 13; y < 15; y++)
		for (i = 0; i < 16; i++)
			eset(i, y, i % 4 == 0 || i == 15 ? 4 : 0);
	compare("hatch bar");
	set1(23, 0);

	/* a magenta square in XOR over it */
	set1(32, 3);
	set1(25, 6);
	pts(0, 0, 7, 3, 7);
	pts(2, 3, 4, 0, 4);
	call(0, 9, 4, 0, 0);
	erow(0, 3, 12, 4 ^ 5);
	erow(0, 3, 15, 4 ^ 5);
	eset(0, 13, 4 ^ 5);
	eset(0, 14, 4 ^ 5);
	eset(3, 13, 5);
	eset(3, 14, 5);
	compare("xor magenta");
	set1(32, 1);

	/* alpha and cross hair in white */
	clear();
	intin[0] = 'H';
	esc(12, 1);
	eglyph('H', 0, 0, 1, 15, 0);
	pts(0, 20, 9, 0, 0);
	esc(18, 0);
	erow(12, 28, 10, 15);
	for (y = 2; y <= 18; y++)
		if (y != 10)
			eset(20, y, 15);
	compare("colour alpha, cursor");

	set1(17, 16);
	want("colour 16", 0, intout[0], 1);
	set1(20, 15);
	want("colour 15", 0, intout[0], 15);
}

static int	msplit[] = { 101, 13, -1 };
static int	csplit[] = { 171, 40, -1 };

int main(argc, argv)
int argc;
char *argv[];
{
	register int	i;

	cputs("GSXT: start\r\n");
	for (i = 0; msplit[i] >= 0 && !bad; i++)
		mono(msplit[i]);
	for (i = 0; csplit[i] >= 0 && !bad; i++)
		colour(csplit[i]);
	if (bad) {
		cputs("GSXT: failed\r\n");
		return (1);
	}
	cputs("GSXT: ok ");
	putdec((int) checks);
	cputs(" checks\r\n");
	return (0);
}
