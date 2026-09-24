/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/*
 * gfxb.c - the byte loops gfxa.s runs on the target, as the reference.
 * Each stays inside one segment.
 */

#include "gfx.h"

/* n bytes, lowest first, so d may sit below p */
VOID gfx_bcopy(d, p, n)
register unsigned char *d, *p;
register unsigned n;
{
	while (n-- > 0)
		*d++ = *p++;
}

VOID gfx_bfill(d, n, v)
register unsigned char *d;
register unsigned n;
int v;
{
	while (n-- > 0)
		*d++ = v;
}

/*
 * n 1 bpp bytes from p, each taking eight source bits under op.  The
 * source is a window: hi, then bytes from q while avail lasts, then 0s,
 * read s bits in.  The first byte is masked by m0, the last by m1.
 */
VOID gfx_m1row(p, q, n, s, hi, avail, m0, m1, op)
register unsigned char *p, *q;
register int n;
int s, hi, avail, m0, m1, op;
{
	register unsigned	w;
	int			v, m;

	for (m = m0; n > 0; n--, m = 0xff) {
		w = hi << 8;
		if (avail > 0) {
			w |= *q++;
			avail--;
		}
		if (n == 1)
			m &= m1;
		v = (w << s) >> 8 & m;
		switch (op) {
		case GFX_RSET:
			*p = (*p & ~m) | v;
			break;
		case GFX_RCLR:
			*p &= ~m;
			break;
		case GFX_RXOR:
			*p ^= v;
			break;
		default:
			*p &= ~v;
		}
		p++;
		hi = w & 0xff;
	}
}

/*
 * n 4 bpp bytes from p, each taking two source bits, read from the
 * window as in gfx_m1row.  The bits pick t[0..3], which the byte takes
 * (GFX_RSET), is XORed with (GFX_RXOR) or ANDed with (GFX_RERASE).
 */
VOID gfx_c4row(p, q, n, s, hi, avail, t, op)
register unsigned char *p, *q;
register int n;
int s, hi, avail;
unsigned char *t;
int op;
{
	register unsigned	w;
	int			v, k, i;

	while (n > 0) {
		w = hi << 8;
		if (avail > 0) {
			w |= *q++;
			avail--;
		}
		v = (w << s) >> 8;
		for (k = 0; k < 4 && n > 0; k++, n--, v <<= 2) {
			i = (v >> 6) & 3;
			if (op == GFX_RSET)
				*p = t[i];
			else if (op == GFX_RXOR)
				*p ^= t[i];
			else
				*p &= t[i];
			p++;
		}
		hi = w & 0xff;
	}
}

VOID gfx_bxor(d, n, v)
register unsigned char *d;
register unsigned n;
int v;
{
	while (n-- > 0)
		*d++ ^= v;
}
