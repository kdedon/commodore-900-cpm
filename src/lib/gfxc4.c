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
	int			v;

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
	for (n = (x1 >> 1) - (x0 >> 1); ; n--) {
		op(p++, 0xff, v, mode);
		if (n == 0)
			break;
		if (--left == 0)
			p = s->seg1;
	}
	return (GFX_OK);
}
