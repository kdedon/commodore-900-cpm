/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/*
 * gfxt.c - the drawing library against a model of what it should draw.
 *
 * Every operation is done twice: by the library into a surface split
 * across two buffers, and here into an array of one value per pixel.
 * The array is then packed into the bytes the surface should hold and
 * the two compared, along with guard bytes past each buffer's end and
 * the padding pixels beyond the surface's width.  Each surface runs at
 * several split points, most of them inside a raster.
 *
 * Then, given two free 64 KB segments, the HR geometries: mono raster
 * 511 | 512 and colour raster 204 across the real segment boundary.
 */

#include "cpm.h"
#include "gfx.h"

#define	GUARD	4
#define	SENT	0xA5
#define	MAXB	256		/* surface bytes			*/

/* mono: 76 x 20 in 80-pixel rasters; colour: 39 x 12 in 40 */
#define	MW	76
#define	MH	20
#define	MSTR	10
#define	CW	39
#define	CH	12
#define	CSTR	20

#define	OPIX	1
#define	OSPAN	2
#define	OLINE	3
#define	OFILL	4
#define	OBOX	5
#define	OBLIT	6
#define	OPLINE	7

struct op {
	int	kind;
	int	a, b, c, d;	/* x, y, w, h -- or x0, y0, x1, y1	*/
	int	e;		/* blit: source x			*/
	int	col;
	int	mode;
};

static struct op ops[] = {
	{ OFILL, 3, 2, 30, 9, 0, 13, GFX_REPLACE },
	{ OSPAN, -5, 9, 100, 0, 0, 7, GFX_XOR },
	{ OSPAN, 13, 4, 2, 0, 0, 5, GFX_REPLACE },
	{ OBOX, 1, 1, 36, 10, 0, 9, GFX_XOR },
	{ OLINE, 0, 0, 38, 11, 0, 5, GFX_XOR },
	{ OLINE, 37, 19, 2, 0, 0, 3, GFX_REPLACE },
	{ OLINE, 10, 0, 13, 19, 0, 11, GFX_XOR },
	{ OLINE, -10, 5, 90, 8, 0, 1, GFX_XOR },
	{ OLINE, 20, 3, 20, 15, 0, 6, GFX_REPLACE },
	{ OLINE, 30, 10, 26, 6, 0, 15, GFX_XOR },
	{ OPIX, 5, 9, 0, 0, 0, 15, GFX_REPLACE },
	{ OPIX, 6, 9, 0, 0, 0, 15, GFX_ERASE },
	{ OPIX, -1, 3, 0, 0, 0, 15, GFX_REPLACE },
	{ OFILL, 30, 8, 60, 4, 0, 0, GFX_ERASE },
	{ OFILL, 20, 5, 9, 7, 0, 10, GFX_REPLACE },
	{ OBLIT, 3, 1, 21, 5, 0, 1, GFX_REPLACE },
	{ OBLIT, 40, 7, 19, 4, 5, 9, GFX_XOR },
	{ OBLIT, -2, 8, 24, 5, 1, 12, GFX_REPLACE },
	{ OBLIT, 29, 9, 13, 3, 3, 1, GFX_ERASE },
	{ OBLIT, 60, -2, 25, 5, 7, 3, GFX_XOR },
	{ OBOX, 36, 9, 1, 1, 0, 3, GFX_XOR },
	{ OBOX, 12, 12, 1, 3, 0, 5, GFX_XOR },
	{ OBOX, 50, 14, 7, 2, 0, 7, GFX_XOR },
	/* polylines: a points from pair b, background c, style e */
	{ OPLINE, 4, 0, -1, 0, 0xFFFF, 7, GFX_XOR },
	{ OPLINE, 5, 4, 2, 0, 0xFC30, 13, GFX_REPLACE },
	{ OPLINE, 3, 9, -1, 0, 0xF0F0, 6, GFX_XOR },
	{ OPLINE, 2, 12, -1, 0, 0xFFFF, 9, GFX_XOR },
	{ OPLINE, 3, 14, 0, 0, 0x8888, 11, GFX_ERASE },
	{ OFILL, -3, -3, 200, 200, 0, 15, GFX_XOR },
	{ 0 }
};

static int ptab[] = {
	2, 2, 30, 4, 25, 11, 2, 2,
	0, 10, 20, 0, 20, 6, -5, 6, 40, 15,
	10, 11, 10, 11, 35, 1,
	5, 5, 5, 5,
	3, 0, 3, 11, 3, 0
};

/* 32 x 5 source image for the blits */
#define	SSTR	4
static unsigned char simg[5 * SSTR] = {
	0xF0, 0x0F, 0xAA, 0x55,
	0x81, 0x42, 0x24, 0x18,
	0xFF, 0x00, 0xC3, 0x3C,
	0x13, 0x57, 0x9B, 0xDF,
	0x01, 0x80, 0x7E, 0xE7
};

static int	msplit[] = { 0, 1, 95, 100, 137, 199, 200, -1 };
static int	csplit[] = { 0, 55, 60, 61, 131, 240, -1 };

static unsigned char	b0[MAXB + GUARD];
static unsigned char	b1[MAXB + GUARD];
static unsigned char	model[MAXB * 8];
static unsigned char	expb[MAXB];

static int	bad;
static long	checks;

/* the surface under test */
static int	sw, sh, sbpp, sstr, total, mw, split;
static struct gfxsurf	surf;

static char hexd[] = "0123456789ABCDEF";

static VOID phex2(v)
int v;
{
	conout(hexd[(v >> 4) & 15]);
	conout(hexd[v & 15]);
}

static VOID fail(what, n, a, b)
char *what;
int n, a, b;
{
	cputs("GFXT: FAIL ");
	cputs(what);
	cputs(" bpp=");
	putdec(sbpp);
	cputs(" split=");
	putdec(split);
	cputs(" at=");
	putdec(n);
	cputs(" got=");
	phex2(a);
	cputs(" want=");
	phex2(b);
	cputs("\r\n");
	bad++;
}

static VOID mset(x, y, c, mode)
int x, y, c, mode;
{
	register unsigned char	*m;

	if (x < 0 || y < 0 || x >= sw || y >= sh)
		return;
	m = &model[y * mw + x];
	c &= sbpp == 1 ? 1 : 15;
	if (mode == GFX_REPLACE)
		*m = c;
	else if (mode == GFX_XOR)
		*m ^= c;
	else
		*m = 0;
}

static int iabs(v)
int v;
{
	return (v < 0 ? -v : v);
}

/* Minor coordinate at step i: the ideal line rounded, halves away from
   the start. */
static int along(i, dmaj, dmin)
int i, dmaj, dmin;
{
	int	q;

	q = (int) ((2L * iabs(dmin) * i + dmaj) / (2L * dmaj));
	return (dmin < 0 ? -q : q);
}

static VOID mline(x0, y0, x1, y1, c, mode)
int x0, y0, x1, y1, c, mode;
{
	register int	i;
	int		dx, dy, sx, sy;

	dx = x1 - x0;
	dy = y1 - y0;
	sx = dx < 0 ? -1 : 1;
	sy = dy < 0 ? -1 : 1;
	if (dy == 0) {
		for (i = 0; i <= iabs(dx); i++)
			mset(x0 + i * sx, y0, c, mode);
	} else if (iabs(dx) >= iabs(dy)) {
		for (i = 0; i <= iabs(dx); i++)
			mset(x0 + i * sx, y0 + along(i, iabs(dx), dy), c, mode);
	} else {
		for (i = 0; i <= iabs(dy); i++)
			mset(x0 + along(i, iabs(dy), dx), y0 + i * sy, c, mode);
	}
}

/* Segment i's pixels from the second on, or from the first for segment
   0; the closing pixel left out when the path ends where it began. */
static VOID mpline(p, n, c, bg, mode, pat)
int *p;
int n, c, bg, mode;
unsigned pat;
{
	register int	i, j;
	int		dx, dy, len, x, y, k, last;

	k = 0;
	last = n > 2 && p[0] == p[2 * n - 2] && p[1] == p[2 * n - 1];
	for (i = 0; i < n - 1; i++) {
		dx = p[2 * i + 2] - p[2 * i];
		dy = p[2 * i + 3] - p[2 * i + 1];
		len = iabs(dx) >= iabs(dy) ? iabs(dx) : iabs(dy);
		for (j = i ? 1 : 0; j <= len; j++) {
			if (last && i == n - 2 && j == len)
				break;
			if (iabs(dx) >= iabs(dy)) {
				x = p[2 * i] + (dx < 0 ? -j : j);
				y = p[2 * i + 1] + (dx ? along(j, iabs(dx), dy) : 0);
			} else {
				x = p[2 * i] + along(j, iabs(dy), dx);
				y = p[2 * i + 1] + (dy < 0 ? -j : j);
			}
			if ((pat >> (15 - k % 16)) & 1)
				mset(x, y, c, mode);
			else if (bg >= 0)
				mset(x, y, bg, GFX_REPLACE);
			k++;
		}
	}
}

static VOID mdo(o)
register struct op *o;
{
	register int	x, y;
	int		x0, x1, bit;

	switch (o->kind) {
	case OPIX:
		mset(o->a, o->b, o->col, o->mode);
		break;
	case OSPAN:
		x0 = o->a < o->c ? o->a : o->c;
		x1 = o->a < o->c ? o->c : o->a;
		for (x = x0; x <= x1; x++)
			mset(x, o->b, o->col, o->mode);
		break;
	case OLINE:
		mline(o->a, o->b, o->c, o->d, o->col, o->mode);
		break;
	case OFILL:
		for (y = o->b; y < o->b + o->d; y++)
			for (x = o->a; x < o->a + o->c; x++)
				mset(x, y, o->col, o->mode);
		break;
	case OBOX:
		for (y = o->b; y < o->b + o->d; y++)
			for (x = o->a; x < o->a + o->c; x++)
				if (x == o->a || x == o->a + o->c - 1
				    || y == o->b || y == o->b + o->d - 1)
					mset(x, y, o->col, o->mode);
		break;
	case OBLIT:
		for (y = 0; y < o->d; y++)
			for (x = 0; x < o->c; x++) {
				bit = (simg[y * SSTR + (o->e + x) / 8]
				    >> (7 - (o->e + x) % 8)) & 1;
				if (bit)
					mset(o->a + x, o->b + y, o->col, o->mode);
				else if (o->mode == GFX_REPLACE)
					mset(o->a + x, o->b + y, 0, o->mode);
			}
		break;
	case OPLINE:
		mpline(&ptab[2 * o->b], o->a, o->col, o->c, o->mode,
		    (unsigned) o->e);
		break;
	}
}

static int ldo(o)
register struct op *o;
{
	switch (o->kind) {
	case OPIX:
		return (gfx_pixel(&surf, o->a, o->b, o->col, o->mode));
	case OSPAN:
		return (gfx_span(&surf, o->a, o->c, o->b, o->col, o->mode));
	case OLINE:
		return (gfx_line(&surf, o->a, o->b, o->c, o->d, o->col, o->mode));
	case OFILL:
		return (gfx_fill(&surf, o->a, o->b, o->c, o->d, o->col, o->mode));
	case OBOX:
		return (gfx_box(&surf, o->a, o->b, o->c, o->d, o->col, o->mode));
	case OBLIT:
		return (gfx_blit(&surf, simg, SSTR, o->e, o->a, o->b, o->c, o->d,
		    o->col, o->mode));
	case OPLINE:
		return (gfx_pline(&surf, &ptab[2 * o->b], o->a, o->col, o->c,
		    o->mode, (unsigned) o->e));
	}
	return (GFX_NOTSUP);
}

static VOID pack()
{
	register int	x, y, v;

	for (x = 0; x < total; x++)
		expb[x] = 0;
	for (y = 0; y < sh; y++)
		for (x = 0; x < mw; x++) {
			v = model[y * mw + x];
			if (sbpp == 1)
				expb[y * sstr + x / 8] |= v << (7 - x % 8);
			else
				expb[y * sstr + x / 2] |= v << (x % 2 ? 0 : 4);
		}
}

static int sbyte(k)
int k;
{
	return (k < split ? b0[k] : b1[k - split]);
}

static VOID compare(what, n)
char *what;
int n;
{
	register int	k;

	pack();
	for (k = 0; k < total; k++) {
		checks++;
		if (sbyte(k) != expb[k]) {
			fail(what, n * 1000 + k, sbyte(k), expb[k]);
			return;
		}
	}
	for (k = 0; k < GUARD; k++)
		if (b0[split + k] != SENT || b1[total - split + k] != SENT) {
			fail("guard", n, b0[split + k], b1[total - split + k]);
			return;
		}
}

static VOID run(bpp, w, h, str, sp)
int bpp, w, h, str, sp;
{
	register int		k, x;
	register struct op	*o;
	int			y, r, n;

	sbpp = bpp;
	sw = w;
	sh = h;
	sstr = str;
	total = h * str;
	mw = str * 8 / bpp;
	split = sp;
	for (k = 0; k < MAXB + GUARD; k++)
		b0[k] = b1[k] = SENT;

	/* a starting picture, padding included, so REPLACE and ERASE have
	   something to overwrite */
	for (y = 0; y < h; y++)
		for (x = 0; x < mw; x++)
			model[y * mw + x] = (x * 5 + y * 3 + (x >> 2)) & (bpp == 1 ? 1 : 15);
	pack();
	for (k = 0; k < total; k++)
		if (k < split)
			b0[k] = expb[k];
		else
			b1[k - split] = expb[k];

	gfx_ram(&surf, w, h, bpp, (unsigned) str, b0, b1, (long) sp);
	for (n = 0, o = ops; o->kind != 0; o++, n++) {
		if ((r = ldo(o)) != GFX_OK) {
			fail("status", n, r, GFX_OK);
			return;
		}
		mdo(o);
		compare("draw", n);
		if (bad)
			return;
	}
	for (y = 0; y < h; y++)
		for (x = 0; x < w; x++) {
			checks++;
			if (gfx_get(&surf, x, y) != model[y * mw + x]) {
				fail("get", y * 1000 + x, gfx_get(&surf, x, y),
				    model[y * mw + x]);
				return;
			}
		}
	if (gfx_get(&surf, w, 0) != -1 || gfx_get(&surf, 0, -1) != -1)
		fail("get outside", 0, 0, 0);
}

/* ---- the HR geometries, across a real segment boundary ---- */

static struct biospb pb;

static long segcall(p1, p2)
long p1, p2;
{
	pb.code = BIOS_SEGMENT;
	pb.p1 = p1;
	pb.p2 = p2;
	return (__bdosl(BDOS_BIOSCALL, (long) &pb));
}

static unsigned char	*hs0, *hs1;

static VOID hclear()
{
	register unsigned	k;

	k = 0;
	do {
		hs0[k] = 0;
		hs1[k] = 0;
	} while (++k != 0);
}

/* want[] lists (segment, offset, byte); every other byte must be 0 */
struct hb {
	int		seg;
	unsigned	off;
	int		v;
};

static VOID hcheck(what, want, n)
char *what;
struct hb *want;
int n;
{
	register unsigned	k;
	register int		i;
	int			s, found, v;

	for (s = 0; s < 2; s++) {
		k = 0;
		do {
			v = (s ? hs1 : hs0)[k];
			found = 0;
			for (i = 0; i < n; i++)
				if (want[i].seg == s && want[i].off == k) {
					found = 1;
					checks++;
					if (v != want[i].v)
						fail(what, i, v, want[i].v);
				}
			if (!found && v != 0) {
				fail(what, (int) k, v, 0);
				return;
			}
		} while (++k != 0);
	}
}

/* mono: a vertical line at x 9 over rasters 510..513 and a span over
   raster 511, 128 bytes a raster */
static struct hb hmono[] = {
	{ 0, 510 * 128 + 1, 0x40 },
	{ 0, 511 * 128 + 0, 0x7F }, { 0, 511 * 128 + 1, 0xBF },
	{ 0, 511 * 128 + 2, 0xC0 },
	{ 1, 0 * 128 + 1, 0x40 },
	{ 1, 1 * 128 + 1, 0x40 },
	{ 1, 287 * 128 + 127, 0x01 }
};

/* colour: raster 204 is bytes 65280..65599, so x 500..520 is 65530..65540
   and the boundary falls at x 512 */
static struct hb hclr[] = {
	{ 0, 65530, 0xAA }, { 0, 65531, 0xAA }, { 0, 65532, 0xAA },
	{ 0, 65533, 0xAA }, { 0, 65534, 0xAA }, { 0, 65535, 0xAA },
	{ 1, 0, 0xAA }, { 1, 1, 0xAA }, { 1, 2, 0xAA }, { 1, 3, 0xAA },
	{ 1, 4, 0xA0 },
	{ 1, 62463, 0x0C }
};

static VOID hr()
{
	int	a, b;
	long	xa;

	a = (int) segcall(SEG_GET, 0L);
	b = (int) segcall(SEG_GET, 0L);
	if (a == 0 || b == 0) {
		cputs("GFXT: FAIL two 64 KB segments not available\r\n");
		bad++;
		return;
	}
	xa = SEGBASE(a);
	hs0 = (unsigned char *) xa;
	xa = SEGBASE(b);
	hs1 = (unsigned char *) xa;

	sbpp = 1;
	split = 0;
	gfx_hrmono(&surf, hs0, hs1);
	if (surf.width != 1024 || surf.height != 800 || surf.stride != 128
	    || (long) 512 * surf.stride != surf.split)
		fail("hr mono geometry", 0, 0, 0);
	hclear();
	gfx_line(&surf, 9, 510, 9, 513, 1, GFX_REPLACE);
	gfx_span(&surf, 1, 17, 511, 1, GFX_XOR);
	gfx_pixel(&surf, 1023, 799, 1, GFX_REPLACE);
	hcheck("hr mono", hmono, sizeof(hmono) / sizeof(hmono[0]));
	if (gfx_get(&surf, 9, 512) != 1 || gfx_get(&surf, 9, 511) != 0)
		fail("hr mono get", 0, 0, 0);

	sbpp = 4;
	gfx_hrclr(&surf, hs0, hs1);
	if (surf.width != 640 || surf.height != 400 || surf.stride != 320
	    || (long) 204 * surf.stride >= surf.split
	    || (long) 205 * surf.stride <= surf.split)
		fail("hr colour geometry", 0, 0, 0);
	hclear();
	gfx_span(&surf, 520, 500, 204, 10, GFX_REPLACE);
	gfx_pixel(&surf, 639, 399, 12, GFX_REPLACE);
	hcheck("hr colour", hclr, sizeof(hclr) / sizeof(hclr[0]));
	if (gfx_get(&surf, 511, 204) != 10 || gfx_get(&surf, 512, 204) != 10
	    || gfx_get(&surf, 521, 204) != 0)
		fail("hr colour get", 0, 0, 0);

	segcall(SEG_PUT, (long) b);
	segcall(SEG_PUT, (long) a);
}

int main(argc, argv)
int argc;
char *argv[];
{
	register int	i;

	cputs("GFXT: start\r\n");
	for (i = 0; msplit[i] >= 0 && !bad; i++)
		run(1, MW, MH, MSTR, msplit[i]);
	for (i = 0; csplit[i] >= 0 && !bad; i++)
		run(4, CW, CH, CSTR, csplit[i]);

	gfx_ram(&surf, 8, 8, 2, 2, b0, b1, 8L);
	if (gfx_pixel(&surf, 0, 0, 1, GFX_REPLACE) != GFX_NOTSUP
	    || gfx_span(&surf, 0, 3, 0, 1, GFX_REPLACE) != GFX_NOTSUP)
		fail("2 bpp not refused", 0, 0, 0);
	gfx_ram(&surf, 8, 8, 1, 1, b0, b1, 8L);
	if (gfx_line(&surf, 0, 0, 3, 3, 1, 9) != GFX_NOTSUP)
		fail("mode 9 not refused", 0, 0, 0);

	if (!bad)
		hr();
	if (bad) {
		cputs("GFXT: failed\r\n");
		return (1);
	}
	cputs("GFXT: ok ");
	putdec((int) (checks / 100));
	cputs("00 checks\r\n");
	return (0);
}
