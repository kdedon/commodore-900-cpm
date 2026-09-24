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
 * reaches the split.  left counts the bytes still in seg0.
 */
int gfx_m1span(s, x0, x1, y, c, mode)
register struct gfxsurf *s;
int x0, x1, y, c, mode;
{
	register unsigned char	*p;
	register int		n;
	long			off, left;
	int			m;

	off = (long) y * s->stride + (x0 >> 3);
	p = gfx_addr(s, off);
	left = off < s->split ? s->split - off : -1L;
	n = (x1 >> 3) - (x0 >> 3);
	m = 0xff >> (x0 & 7);
	for (;;) {
		if (n == 0)
			m &= 0xff << (7 - (x1 & 7));
		op(p++, m, c, mode);
		if (n-- == 0)
			break;
		if (--left == 0)
			p = s->seg1;
		m = 0xff;
	}
	return (GFX_OK);
}

/* Eight source bits starting at bit sb of row, which may be up to 7
   before it; last is the final byte the window may read. */
static int bits8(row, sb, last)
unsigned char *row;
int sb, last;
{
	register int	i, v;

	if (sb < 0)
		return (row[0] >> -sb);
	i = sb >> 3;
	v = row[i] << (sb & 7);
	if ((sb & 7) != 0 && i < last)
		v |= row[i + 1] >> (8 - (sb & 7));
	return (v & 0xff);
}

int gfx_m1blit(s, src, sstride, sx, x, y, w, h, c, mode)
register struct gfxsurf *s;
unsigned char *src;
unsigned sstride;
int sx, x, y, w, h, c, mode;
{
	register unsigned char	*p;
	register int		n;
	unsigned char		*row;
	long			off, left;
	int			x1, last, sb, m, v;

	x1 = x + w - 1;
	last = (sx + w - 1) >> 3;
	for (; h > 0; h--, y++, src += sstride) {
		row = src;
		off = (long) y * s->stride + (x >> 3);
		p = gfx_addr(s, off);
		left = off < s->split ? s->split - off : -1L;
		n = (x1 >> 3) - (x >> 3);
		m = 0xff >> (x & 7);
		sb = sx - (x & 7);
		for (;;) {
			if (n == 0)
				m &= 0xff << (7 - (x1 & 7));
			v = bits8(row, sb, last) & m;
			if (mode == GFX_REPLACE)
				*p = (*p & ~m) | ((c & 1) ? v : 0);
			else if (mode == GFX_XOR) {
				if (c & 1)
					*p ^= v;
			} else
				*p &= ~v;
			p++;
			if (n-- == 0)
				break;
			if (--left == 0)
				p = s->seg1;
			m = 0xff;
			sb += 8;
		}
	}
	return (GFX_OK);
}
