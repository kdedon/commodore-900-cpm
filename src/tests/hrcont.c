/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/*
 * hrcont.c - the HR console, through BIOS function 102, on split
 * surfaces of both depths.
 *
 * Each H19 stream has a worked answer: the characters, attributes and
 * cursor cell it leaves, or for Tek alpha the glyphs at the beam.  The
 * answer is painted pixel by pixel from the font tables and compared with
 * the surface's bytes, guards included.
 */

#include "cpm.h"
#include "gfx.h"

#define	GUARD	4
#define	SENT	0xA5
#define	MAXB	1900

#define	ESC	"\033"
#define	GS	0x1d
#define	US	0x1f

#define	HY(y)	(0x20 | ((y) >> 5))
#define	LY(y)	(0x60 | ((y) & 31))
#define	HX(x)	(0x20 | ((x) >> 5))
#define	LX(x)	(0x40 | ((x) & 31))

extern unsigned short	hrfont[][25];
extern unsigned char	gsxfont[];

/*
 * mono 128 x 110, 12 x 25 cells: 10 x 4 and margins
 * colour 84 x 44, 8 x 8 cells: 10 x 5 and margins
 */
#define	DM	1
#define	DC	2

struct want {
	int	depth;		/* DM, DC or both		*/
	char	*in;
	char	*row[5];
	char	*attr[5];	/* r reverse, u underline	*/
	int	cr, cc;		/* cursor, -1 when off		*/
};

static struct want cases[] = {
	{ DM | DC, "ABC\r\nDEF",
	    { "ABC", "DEF" }, { 0 }, 1, 3 },
	{ DM | DC, "0123456789AB\r\nabcdefghij\r\nk",
	    { "0123456789", "AB", "abcdefghij", "k" }, { 0 }, 3, 1 },
	{ DM, "1\r\n2\r\n3\r\n4\r\n5\r\n6\r\n7",
	    { "4", "5", "6", "7" }, { 0 }, 3, 1 },
	{ DC, "1\r\n2\r\n3\r\n4\r\n5\r\n6\r\n7",
	    { "3", "4", "5", "6", "7" }, { 0 }, 4, 1 },
	{ DM | DC, "XYZ\r\n" ESC "EQ",
	    { "Q" }, { 0 }, 0, 1 },
	{ DM | DC, ESC "Y\"%R" ESC "=!#S" ESC "H",
	    { "", "   S", "     R" }, { 0 }, 0, 0 },
	{ DM | DC, "a" ESC "pbc" ESC "qd" ESC "he" ESC "i",
	    { "abcde" }, { ".rr.u" }, 0, 5 },
	{ DM | DC, "HELLO\r\nWORLD" ESC "Y  " ESC "N" ESC "L" ESC "Y!\"" ESC "K",
	    { "", "EL", "WORLD" }, { 0 }, 1, 2 },
	{ DM, "\r\n" ESC "pX" ESC "q\r\n\r\n\r\nY",
	    { "X", "", "", "Y" }, { "r" }, 3, 1 },
	{ DC, "\r\n" ESC "pX" ESC "q\r\n\r\n\r\nY",
	    { "", "X", "", "", "Y" }, { "", "r" }, 4, 1 },
	{ 0 }
};

/* Tek alpha: a dark move to (200, 500), then text at the beam */
static char tekin[] = { GS, HY(500), LY(500), HX(200), LX(200), US,
			'A', 'B', 0 };

static int	msplit[] = { 0, 7, 601, 803, 1759, -1 };
static int	csplit[] = { 0, 341, 861, 1847, -1 };

static unsigned char	b0[MAXB + GUARD], b1[MAXB + GUARD];
static unsigned char	model[128 * 110];
static struct gfxsurf	s;
static int		fw, fh, ink, bad, runs;

static char hexd[] = "0123456789ABCDEF";

static VOID phex(v)
int v;
{
	char	t[5];
	int	i;

	for (i = 3; i >= 0; i--, v >>= 4)
		t[i] = hexd[v & 15];
	t[4] = 0;
	cputs(t);
}

static VOID fail(what, n, split, x, y)
char *what;
int n, split, x, y;
{
	cputs("HRCONT: FAIL ");
	cputs(what);
	cputs(" case=");
	phex(n);
	cputs(" bpp=");
	phex(s.bpp);
	cputs(" split=");
	phex(split);
	cputs(" x=");
	phex(x);
	cputs(" y=");
	phex(y);
	cputs("\r\n");
	bad = 1;
}

/* Bit (gx, gy) of the glyph for ch, read straight from the tables. */
static int fbit(ch, gx, gy)
int ch, gx, gy;
{
	if (s.bpp == 1)
		return ((hrfont[ch - 0x20][gy] >> (15 - gx)) & 1);
	return ((gsxfont[ch * 8 + gy] >> (7 - gx)) & 1);
}

/* Glyph ch painted at (x, y) as the console paints it, clipped. */
static VOID paint(ch, x, y, a, cursor)
int ch, x, y, a, cursor;
{
	register int gx, gy, v;

	for (gy = 0; gy < fh; gy++)
		for (gx = 0; gx < fw; gx++) {
			if (x + gx >= s.width || y + gy >= s.height)
				continue;
			v = fbit(ch, gx, gy) ? ink : 0;
			if (a == 'r')
				v ^= ink;
			if (a == 'u' && gy == fh - 1)
				v = ink;
			if (cursor)
				v ^= (1 << s.bpp) - 1;
			model[(y + gy) * s.width + x + gx] = v;
		}
}

static int pixel(split, x, y)
int split, x, y;
{
	register int	off, v;

	off = y * s.stride + ((x * s.bpp) >> 3);
	v = off < split ? b0[off] : b1[off - split];
	if (s.bpp == 1)
		return ((v >> (7 - (x & 7))) & 1);
	return ((x & 1) ? v & 15 : v >> 4);
}

/* p[c], or 0 past its end */
static int at(p, c)
register char *p;
register int c;
{
	if (p == 0)
		return (0);
	for (; c > 0 && *p; c--)
		p++;
	return (*p);
}

static VOID answer(w)
register struct want *w;
{
	register int	r, c, ch, a;

	for (r = 0; r < s.height * s.width; r++)
		model[r] = 0;
	if (w == 0) {
		/* the beam's pixel, x = (tx * (w - 1) + 511) / 1023 and
		   y = h - 1 - (ty * (h - 1) + 389) / 779, is the glyph's
		   bottom-left corner: (25, 39) then (27, 39) mono, (16, 15)
		   then (17, 15) colour */
		paint('A', s.bpp == 1 ? 25 : 16, s.bpp == 1 ? 15 : 8, 0, 0);
		paint('B', s.bpp == 1 ? 27 : 17, s.bpp == 1 ? 15 : 8, 0, 0);
		return;
	}
	for (r = 0; r < s.height / fh; r++)
		for (c = 0; c < s.width / fw; c++) {
			ch = r < 5 ? at(w->row[r], c) : 0;
			a = r < 5 ? at(w->attr[r], c) : 0;
			if (ch == 0)
				ch = ' ';
			paint(ch, c * fw, r * fh, a, r == w->cr && c == w->cc);
		}
}

static VOID run(n, w, in, bpp, split)
int n;
struct want *w;
char *in;
int bpp, split;
{
	register int	i, x, y;
	int		total;

	if (bpp == 1) {
		gfx_ram(&s, 128, 110, 1, 16, b0, b1, (long) split);
		fw = 12;
		fh = 25;
		ink = 1;
	} else {
		gfx_ram(&s, 84, 44, 4, 42, b0, b1, (long) split);
		fw = 8;
		fh = 8;
		ink = 7;
	}
	total = s.height * s.stride;
	for (i = 0; i < MAXB + GUARD; i++)
		b0[i] = b1[i] = SENT;
	__bios(BIOS_HRRAM, (long) &s, (long) in);
	answer(w);

	for (y = 0; y < s.height; y++)
		for (x = 0; x < s.width; x++)
			if (pixel(split, x, y) != model[y * s.width + x]) {
				fail("pixel", n, split, x, y);
				return;
			}
	for (i = split; i < MAXB + GUARD; i++)
		if (b0[i] != SENT) {
			fail("seg0 guard", n, split, i, 0);
			return;
		}
	for (i = total - split; i < MAXB + GUARD; i++)
		if (i >= 0 && b1[i] != SENT) {
			fail("seg1 guard", n, split, i, 0);
			return;
		}
	runs++;
}

int main(argc, argv)
int argc;
char *argv[];
{
	register int	n, i;
	register struct want *w;

	cputs("HRCONT: start\r\n");
	for (n = 0, w = cases; w->in && !bad; n++, w++) {
		for (i = 0; (w->depth & DM) && msplit[i] >= 0 && !bad; i++)
			run(n, w, w->in, 1, msplit[i]);
		for (i = 0; (w->depth & DC) && csplit[i] >= 0 && !bad; i++)
			run(n, w, w->in, 4, csplit[i]);
	}
	for (i = 0; msplit[i] >= 0 && !bad; i++)
		run(n, (struct want *) 0, tekin, 1, msplit[i]);
	for (i = 0; csplit[i] >= 0 && !bad; i++)
		run(n, (struct want *) 0, tekin, 4, csplit[i]);
	if (bad) {
		cputs("HRCONT: failed\r\n");
		return (1);
	}
	cputs("HRCONT: ok ");
	phex(runs);
	cputs(" runs\r\n");
	return (0);
}
