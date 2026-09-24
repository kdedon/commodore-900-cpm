/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/*
 * crshr.c - the HR console's character cells, in the graphics module.
 *
 * The console's parser keeps the cursor and calls these with cell
 * coordinates.  A cell row is h whole rasters, so scrolling moves raster
 * bytes and never looks at pixels.  The cursor is the cell XORed with
 * every plane: each call takes it off, draws, and puts it back.
 */

#include "../lib/gfx.h"

/*
 * A font: w x h glyphs, stride bytes a row, 1 bpp with the leftmost pixel
 * in the top bit.  n glyphs, for 0x20..0x7F then 0xA0..0xFF.
 */
struct hcfont {
	int		w, h;
	unsigned	stride;
	int		n;
	unsigned char	*bits;
};

extern unsigned short	hrfont[][25];
extern unsigned char	gsxfont[];

static struct hcfont	fmono = { 12, 25, 2, 192, (unsigned char *)hrfont };
static struct hcfont	fclr = { 8, 8, 1, 96, gsxfont + 0x20 * 8 };

/* the console's cell attributes */
#define	A_MODE		0x77
#define	A_UNDERL	0x01
#define	A_REVERSE	0x70
#define	A_INTENSE	0x08

struct gfxsurf		hrsurf;
struct gfxsurf		*hs = &hrsurf;	/* the surface drawn on */
static struct hcfont	*f;
static int		rows, cols;
static int		ink;		/* text colour			*/
static int		cr, cc;		/* cursor cell			*/
static int		vis;		/* the cursor should show	*/
static int		con;		/* the cursor is drawn		*/
static unsigned char	rbuf[128];	/* one 1 bpp raster		*/

/* Bytes from off that stay in one segment, at most n. */
static unsigned run(off, n)
long off, n;
{
	register struct gfxsurf *s;
	long	k;

	s = hs;
	k = off < s->split ? s->split - off : n;
	if (k > n)
		k = n;
	if (k > 0x4000L)
		k = 0x4000L;
	return ((unsigned) k);
}

static VOID bmove(d, p, n)
register unsigned char *d, *p;
register unsigned n;
{
	while (n-- > 0)
		*d++ = *p++;
}

static VOID bzero(d, n)
register unsigned char *d;
register unsigned n;
{
	while (n-- > 0)
		*d++ = 0;
}

/* Surface bytes from to to, lowest first: safe when to is below from. */
static VOID rcopy(to, from, n)
long to, from, n;
{
	register unsigned k;

	while (n > 0) {
		k = run(from, (long) run(to, n));
		bmove(gfx_addr(hs, to), gfx_addr(hs, from), k);
		to += k;
		from += k;
		n -= k;
	}
}

static VOID rclear(off, n)
long off, n;
{
	register unsigned k;

	while (n > 0) {
		k = run(off, n);
		bzero(gfx_addr(hs, off), k);
		off += k;
		n -= k;
	}
}

/* n raster bytes from surface offset off into rbuf */
static VOID rget(off, n)
long off;
unsigned n;
{
	register unsigned k, i;

	for (i = 0; i < n; i += k) {
		k = run(off + i, (long) (n - i));
		bmove(rbuf + i, gfx_addr(hs, off + i), k);
	}
}

/* Surface offset of the first raster of cell row r */
static long rowoff(r)
int r;
{
	return ((long) r * f->h * hs->stride);
}

static VOID cur()
{
	gfx_fill(hs, cc * f->w, cr * f->h, f->w, f->h, (1 << hs->bpp) - 1,
	    GFX_XOR);
}

static VOID hide()
{
	if (con)
		cur();
	con = 0;
}

static VOID done()
{
	if (vis && !con) {
		cur();
		con = 1;
	}
}

/* Glyph c with attribute a, its top-left corner at pixel (x, y). */
static VOID glyph(x, y, c, a)
int x, y, c, a;
{
	register struct hcfont *p;
	register unsigned char *g;
	int	k;

	p = f;
	c &= 0xff;
	if (c >= 0xa0)
		c -= 0x20;
	c -= 0x20;
	if (c < 0 || c >= p->n)
		c = 0;
	g = p->bits + (long) c * p->h * p->stride;
	k = hs->bpp == 4 && (a & A_INTENSE) ? 15 : ink;
	if ((a & A_MODE) == A_REVERSE) {
		gfx_fill(hs, x, y, p->w, p->h, k, GFX_REPLACE);
		gfx_blit(hs, g, p->stride, 0, x, y, p->w, p->h, k, GFX_XOR);
		return;
	}
	gfx_blit(hs, g, p->stride, 0, x, y, p->w, p->h, k, GFX_REPLACE);
	if ((a & A_MODE) == A_UNDERL)
		gfx_span(hs, x, x + p->w - 1, y + p->h - 1, k, GFX_REPLACE);
}

/* Glyph c with its bottom-left corner at pixel (x, y). */
hcat(x, y, c)
int x, y, c;
{
	y -= f->h - 1;
	glyph(x, y < 0 ? 0 : y, c, 0);
}

/*
 * Draw on s from now on, cleared and with the cursor off; returns the
 * cell rows << 8 | columns.
 */
hcram(s)
struct gfxsurf *s;
{
	hs = s;
	f = s->bpp == 1 ? &fmono : &fclr;
	ink = s->bpp == 1 ? 1 : 7;
	rows = s->height / f->h;
	cols = s->width / f->w;
	rclear(0L, (long) s->height * s->stride);
	cr = 0;
	cc = 0;
	vis = 0;
	con = 0;
	return ((rows << 8) | cols);
}

/* The HR bitmap, colour when m is 1. */
hcinit(m)
int m;
{
	if (m == 1)
		gfx_hrclr(&hrsurf, (unsigned char *)0x3a000000L,
		    (unsigned char *)0x3b000000L);
	else
		gfx_hrmono(&hrsurf, (unsigned char *)0x3a000000L,
		    (unsigned char *)0x3b000000L);
	return (hcram(&hrsurf));
}

hcpark(r, c)
int r, c;
{
	hide();
	cr = r;
	cc = c;
	vis = 1;
	done();
}

/* Take the cursor away until the next park. */
hcoff()
{
	hide();
	vis = 0;
}

/* n cells of s at (r, c), on one row */
hcput(r, c, s, n, a)
int r, c;
register char *s;
register int n;
int a;
{
	hide();
	for (; n > 0; n--, c++)
		glyph(c * f->w, r * f->h, *s++, a);
	done();
}

/* Blank n cells from (r, c), wrapping by rows. */
hcblank(r, c, n)
register int r, c, n;
{
	register int k;

	hide();
	while (n > 0 && r < rows) {
		if (c == 0 && n >= cols) {
			k = n / cols;
			if (k > rows - r)
				k = rows - r;
			rclear(rowoff(r), rowoff(k));
			r += k;
			n -= k * cols;
			continue;
		}
		k = cols - c;
		if (k > n)
			k = n;
		gfx_fill(hs, c * f->w, r * f->h, k * f->w, f->h, 0,
		    GFX_REPLACE);
		n -= k;
		c = 0;
		r++;
	}
	done();
}

/*
 * Roll rows top..bot up by n, or down by -n, blanking the rows uncovered.
 * Down goes a raster at a time from the bottom, so no copy overlaps.
 */
hcscroll(top, bot, n)
int top, bot, n;
{
	register long y, d;
	long	rs;

	hide();
	rs = hs->stride;
	if (n > 0) {
		if (bot - top + 1 > n)
			rcopy(rowoff(top), rowoff(top + n),
			    rowoff(bot - top + 1 - n));
		rclear(rowoff(bot + 1 - n), rowoff(n));
	} else {
		n = -n;
		d = rowoff(n);
		for (y = rowoff(bot + 1) - rs; y >= rowoff(top) + d; y -= rs)
			rcopy(y, y - d, rs);
		rclear(rowoff(top), d);
	}
	done();
}

/* Delete the cell at (r, c), pulling the rest of the row left. */
hcdelch(r, c)
int r, c;
{
	register int i, w;
	long	off;

	hide();
	w = f->w;
	for (i = 0; i < f->h; i++) {
		off = (long) (r * f->h + i) * hs->stride;
		if (hs->bpp != 1)
			rcopy(off + (c * w * hs->bpp >> 3),
			    off + ((c + 1) * w * hs->bpp >> 3),
			    (long) ((cols - 1 - c) * w * hs->bpp >> 3));
		else if (hs->stride <= sizeof (rbuf)) {
			rget(off, hs->stride);
			gfx_blit(hs, rbuf, hs->stride, (c + 1) * w, c * w,
			    r * f->h + i, (cols - 1 - c) * w, 1, 1,
			    GFX_REPLACE);
		}
	}
	gfx_fill(hs, (cols - 1) * w, r * f->h, w, f->h, 0, GFX_REPLACE);
	done();
}
