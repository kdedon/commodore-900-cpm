/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/*
 * gfxc4.c - the 4 bpp back-end: IRGB nibbles, two pixels a byte, the
 * high nibble leftmost.  Callers have clipped and checked the mode.
 */

#include "gfx.h"

/* Apply colour v under mode to the nibbles of *p that m selects; v is
   already in their position. */
static VOID op(p, m, v, mode)
register unsigned char *p;
int m, v, mode;
{
	if (mode == GFX_REPLACE)
		*p = (*p & ~m) | (v & m);
	else if (mode == GFX_XOR)
		*p ^= v & m;
	else
		*p &= ~m;
}

int gfx_c4pix(s, x, y, c, mode)
struct gfxsurf *s;
int x, y, c, mode;
{
	int	sh;

	sh = (x & 1) ? 0 : 4;
	op(gfx_addr(s, (long) y * s->stride + (x >> 1)),
	    0x0f << sh, (c & 15) << sh, mode);
	return (GFX_OK);
}

int gfx_c4get(s, x, y)
struct gfxsurf *s;
int x, y;
{
	return ((*gfx_addr(s, (long) y * s->stride + (x >> 1))
	    >> ((x & 1) ? 0 : 4)) & 15);
}

/* Odd ends as single pixels, whole bytes between; see gfx_m1span for
   the walk across the split. */
int gfx_c4span(s, x0, x1, y, c, mode)
register struct gfxsurf *s;
int x0, x1, y, c, mode;
{
	register unsigned char	*p;
	register int		n;
	long			off, left;
	int			v, k;

	if (x0 & 1)
		gfx_c4pix(s, x0++, y, c, mode);
	if ((x1 & 1) == 0 && x1 >= x0)
		gfx_c4pix(s, x1--, y, c, mode);
	if (x1 < x0)
		return (GFX_OK);
	c &= 15;
	v = (c << 4) | c;
	off = (long) y * s->stride + (x0 >> 1);
	p = gfx_addr(s, off);
	left = off < s->split ? s->split - off : -1L;
	for (n = (x1 >> 1) - (x0 >> 1) + 1; n > 0; n -= k) {
		k = left > 0 && left < n ? (int) left : n;
		if (mode == GFX_REPLACE)
			gfx_bfill(p, k, v);
		else if (mode == GFX_XOR)
			gfx_bxor(p, k, v);
		else
			gfx_bfill(p, k, 0);
		p += k;
		if ((left -= k) == 0)
			p = s->seg1;
	}
	return (GFX_OK);
}

/* Source bit b of row, applied to pixel (x, y) as gfx_blit applies it. */
static VOID c4bit(s, row, b, x, y, c, mode)
struct gfxsurf *s;
unsigned char *row;
int b, x, y, c, mode;
{
	if ((row[b >> 3] >> (7 - (b & 7))) & 1)
		gfx_c4pix(s, x, y, c, mode);
	else if (mode == GFX_REPLACE)
		gfx_c4pix(s, x, y, 0, mode);
}

/* n bytes from source bit sb of row, stopping before the split */
static VOID c4run(p, row, sb, n, last, t, op)
unsigned char *p, *row;
int sb, n, last;
unsigned char *t;
int op;
{
	gfx_c4row(p, row + (sb >> 3) + 1, n, sb & 7, row[sb >> 3],
	    last - (sb >> 3), t, op);
}

/* Odd end pixels one at a time, whole bytes between in runs. */
int gfx_c4blit(s, src, sstride, sx, x, y, w, h, c, mode)
register struct gfxsurf *s;
unsigned char *src;
unsigned sstride;
int sx, x, y, w, h, c, mode;
{
	register int	n, k;
	long		off, left;
	int		i, x0, x1, last, op;
	unsigned char	t[4];

	c &= 15;
	for (i = 0; i < 4; i++) {
		k = ((i & 2) ? 0xf0 : 0) | ((i & 1) ? 0x0f : 0);
		t[i] = mode == GFX_ERASE ? ~k : k & ((c << 4) | c);
	}
	op = mode == GFX_REPLACE ? GFX_RSET : mode == GFX_XOR ? GFX_RXOR
	    : GFX_RERASE;
	last = (sx + w - 1) >> 3;
	for (; h > 0; h--, y++, src += sstride) {
		x0 = x;
		x1 = x + w - 1;
		if (x0 & 1) {
			c4bit(s, src, sx, x0, y, c, mode);
			x0++;
		}
		if ((x1 & 1) == 0 && x1 >= x0) {
			c4bit(s, src, sx + x1 - x, x1, y, c, mode);
			x1--;
		}
		if (x1 < x0)
			continue;
		n = (x1 - x0 + 1) >> 1;
		off = (long) y * s->stride + (x0 >> 1);
		left = off < s->split ? s->split - off : -1L;
		k = left > 0 && left < n ? (int) left : n;
		c4run(gfx_addr(s, off), src, sx + x0 - x, k, last, t, op);
		if (k < n)
			c4run(s->seg1, src, sx + x0 - x + 2 * k, n - k, last,
			    t, op);
	}
	return (GFX_OK);
}
