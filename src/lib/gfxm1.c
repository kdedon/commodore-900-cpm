/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/*
 * gfxm1.c - the 1 bpp back-end.  Callers have clipped and checked the
 * mode.  Bit 7 of a byte is its leftmost pixel.
 */

#include "gfx.h"

/* Apply colour c under mode to the bits of *p that m selects. */
static VOID op(p, m, c, mode)
register unsigned char *p;
register int m;
int c, mode;
{
	if (mode == GFX_ERASE || (mode == GFX_REPLACE && (c & 1) == 0))
		*p &= ~m;
	else if (mode == GFX_REPLACE)
		*p |= m;
	else if (c & 1)
		*p ^= m;
}

int gfx_m1pix(s, x, y, c, mode)
struct gfxsurf *s;
int x, y, c, mode;
{
	op(gfx_addr(s, (long) y * s->stride + (x >> 3)),
	    0x80 >> (x & 7), c, mode);
	return (GFX_OK);
}

int gfx_m1get(s, x, y)
struct gfxsurf *s;
int x, y;
{
	return ((*gfx_addr(s, (long) y * s->stride + (x >> 3))
	    >> (7 - (x & 7))) & 1);
}

/*
 * A raster's bytes x0/8 .. x1/8, stepping to seg1 when the offset
 * reaches the split.  left counts the bytes still in seg0.  The whole
 * bytes between the ends go in runs that stop at the split.
 */
int gfx_m1span(s, x0, x1, y, c, mode)
register struct gfxsurf *s;
int x0, x1, y, c, mode;
{
	register unsigned char	*p;
	register int		n;
	long			off, left;
	int			m, k;

	off = (long) y * s->stride + (x0 >> 3);
	p = gfx_addr(s, off);
	left = off < s->split ? s->split - off : -1L;
	n = (x1 >> 3) - (x0 >> 3);
	m = 0xff >> (x0 & 7);
	if (n == 0) {
		op(p, m & (0xff << (7 - (x1 & 7))), c, mode);
		return (GFX_OK);
	}
	op(p++, m, c, mode);
	if (--left == 0)
		p = s->seg1;
	for (n--; n > 0; n -= k) {
		k = left > 0 && left < n ? (int) left : n;
		if (mode == GFX_ERASE || (mode == GFX_REPLACE && (c & 1) == 0))
			gfx_bfill(p, k, 0);
		else if (mode == GFX_REPLACE)
			gfx_bfill(p, k, 0xff);
		else if (c & 1)
			gfx_bxor(p, k, 0xff);
		p += k;
		if ((left -= k) == 0)
			p = s->seg1;
	}
	op(p, 0xff << (7 - (x1 & 7)), c, mode);
	return (GFX_OK);
}

/*
 * Source bits sb.. of src, sb up to 7 before it and bytes past last read
 * as 0, into n bytes from p; the run stops before the split.
 */
static VOID m1run(p, src, sb, n, last, m0, m1, op)
unsigned char *p, *src;
int sb, n, last, m0, m1, op;
{
	int	i;

	i = sb < 0 ? -1 : sb >> 3;
	gfx_m1row(p, src + i + 1, n, sb & 7, i >= 0 ? src[i] : 0, last - i,
	    m0, m1, op);
}

int gfx_m1blit(s, src, sstride, sx, x, y, w, h, c, mode)
register struct gfxsurf *s;
unsigned char *src;
unsigned sstride;
int sx, x, y, w, h, c, mode;
{
	register int		n, k;
	long			off, left;
	int			x1, last, sb, m0, m1, op;

	if (mode == GFX_XOR && (c & 1) == 0)
		return (GFX_OK);
	op = mode == GFX_XOR ? GFX_RXOR : mode == GFX_ERASE ? GFX_RERASE
	    : (c & 1) ? GFX_RSET : GFX_RCLR;
	x1 = x + w - 1;
	last = (sx + w - 1) >> 3;
	n = (x1 >> 3) - (x >> 3) + 1;
	m0 = 0xff >> (x & 7);
	m1 = (0xff << (7 - (x1 & 7))) & 0xff;
	sb = sx - (x & 7);
	for (; h > 0; h--, y++, src += sstride) {
		off = (long) y * s->stride + (x >> 3);
		left = off < s->split ? s->split - off : -1L;
		k = left > 0 && left < n ? (int) left : n;
		m1run(gfx_addr(s, off), src, sb, k, last, m0,
		    k == n ? m1 : 0xff, op);
		if (k < n)
			m1run(s->seg1, src, sb + 8 * k, n - k, last, 0xff, m1,
			    op);
	}
	return (GFX_OK);
}
