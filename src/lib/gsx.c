/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/*
 * gsx.c - a GSX screen driver over the drawing library, and the GDOS
 * coordinate scaling in front of it.
 *
 * The driver works in device coordinates, y up, and turns them into
 * rasters (y down) only when it calls gfx.  The alpha escapes run an
 * 8 x 8 character grid on the same surface.
 */

#include "gfx.h"
#include "gsx.h"

extern unsigned char	gsxfont[];

#define	H(ws)	((ws)->s->height)
#define	W(ws)	((ws)->s->width)

static unsigned	lstyle[8] = {
	0xFFFF, 0xFFC0, 0xC0C0, 0xFC30, 0xFFFC, 0xF0F0, 0xFCCC, 0x8888
};

/* GSX's colours 0..7 (black, red, green, blue, cyan, yellow, magenta,
   white) as IRGB, then the other eight */
static int	defpix[16] = {
	0, 4, 2, 1, 3, 6, 5, 15, 8, 12, 10, 9, 11, 14, 13, 7
};

static int	nbuf[2 * GSXMAXV];
static int	rbuf[2 * GSXMAXV + 2];
static int	xs[GSXMAXV];

/* Component k (R, G, B) of pixel v, 0..1000; colour assumes CGA-style
   levels, intensity adding a third. */
static int prgb(ws, v, k)
struct gsxws *ws;
int v, k;
{
	int	i;

	if (ws->ncol == 2)
		return (v ? 1000 : 0);
	i = (v >> 3) & 1;
	if ((v >> (2 - k)) & 1)
		return (i ? 1000 : 667);
	return (i ? 333 : 0);
}

static int nearest(ws, rgb)
struct gsxws *ws;
int *rgb;
{
	register int	v, k;
	int		best;
	long		d, bd, e;

	if (ws->ncol == 2)
		return (299L * rgb[0] + 587L * rgb[1] + 114L * rgb[2]
		    >= 500000L);
	best = 0;
	bd = -1;
	for (v = 0; v < 16; v++) {
		d = 0;
		for (k = 0; k < 3; k++) {
			e = rgb[k] - prgb(ws, v, k);
			d += e * e;
		}
		if (bd < 0 || d < bd) {
			bd = d;
			best = v;
		}
	}
	return (best);
}

static int cidx(ws, i)
struct gsxws *ws;
int i;
{
	return (i < 0 || i >= ws->ncol ? 1 : i);
}

static VOID attrs(ws)
register struct gsxws *ws;
{
	if (ws->lt < 1 || ws->lt > 8)
		ws->lt = 1;
	if (ws->mt < 1 || ws->mt > 8)
		ws->mt = 3;
	if (ws->fis < 0 || ws->fis > 3)
		ws->fis = 0;
	if (ws->fsi < 1 || ws->fsi > 8)
		ws->fsi = 1;
	if (ws->wm < 1 || ws->wm > 4)
		ws->wm = 1;
	ws->lc = cidx(ws, ws->lc);
	ws->mc = cidx(ws, ws->mc);
	ws->tc = cidx(ws, ws->tc);
	ws->fc = cidx(ws, ws->fc);
}

/*
 * The writing modes on a raster rectangle whose pixels are all set
 * (bit) or all clear in the primitive: 1 replace, 2 transparent,
 * 3 complement (XOR c), 4 erase to the background.
 */
static int put(ws, wm, x, y, w, h, bit, c)
register struct gsxws *ws;
int wm, x, y, w, h, bit, c;
{
	if (wm == 1)
		return (gfx_fill(ws->s, x, y, w, h, bit ? c : ws->pix[0],
		    GFX_REPLACE));
	if (!bit)
		return (GFX_OK);
	if (wm == 3)
		return (gfx_fill(ws->s, x, y, w, h, c, GFX_XOR));
	return (gfx_fill(ws->s, x, y, w, h, wm == 4 ? ws->pix[0] : c,
	    GFX_REPLACE));
}

/* A span of raster row y under pattern byte b, least significant bit
   leftmost, anchored at x 0. */
static int pspan(ws, x0, x1, y, b, c)
register struct gsxws *ws;
int x0, x1, y, b, c;
{
	register int	x;
	int		t, bit, r;

	if (x0 > x1) {
		t = x0;
		x0 = x1;
		x1 = t;
	}
	if (x0 < 0)
		x0 = 0;
	if (x1 >= W(ws))
		x1 = W(ws) - 1;
	if (x0 > x1 || y < 0 || y >= H(ws))
		return (GFX_OK);
	while (x0 <= x1) {
		bit = (b >> (x0 & 7)) & 1;
		for (x = x0; x < x1 && ((b >> ((x + 1) & 7)) & 1) == bit; x++)
			;
		if ((r = put(ws, ws->wm, x0, y, x - x0 + 1, 1, bit, c)) != GFX_OK)
			return (r);
		x0 = x + 1;
	}
	return (GFX_OK);
}

/*
 * Rows g[0 .. nr-1] of a glyph, most significant bit leftmost, with
 * column 0 of row 0 at (du, dv) from device point (x, y), y up; k
 * scales it and up (0, 900, 1800, 2700) turns it.  Each run of equal
 * bits is one rectangle.
 */
static int glyph(ws, g, nr, x, y, du, dv, k, up, c, wm, inv)
register struct gsxws *ws;
unsigned char *g;
int nr, x, y, du, dv, k, up, c, wm, inv;
{
	register int	c0, c1;
	int		r, b, bit, u0, u1, v0, v1, a0, a1, b0, b1, e;

	for (r = 0; r < nr; r++) {
		b = g[r];
		for (c0 = 0; c0 < 8; c0 = c1 + 1) {
			bit = (b >> (7 - c0)) & 1;
			for (c1 = c0; c1 < 7 && ((b >> (6 - c1)) & 1) == bit; c1++)
				;
			u0 = k * (du + c0);
			u1 = k * (du + c1 + 1) - 1;
			v0 = k * (dv - r);
			v1 = v0 + k - 1;
			switch (up) {
			case 900:
				a0 = x - v1;
				a1 = x - v0;
				b0 = y + u0;
				b1 = y + u1;
				break;
			case 1800:
				a0 = x - u1;
				a1 = x - u0;
				b0 = y - v1;
				b1 = y - v0;
				break;
			case 2700:
				a0 = x + v0;
				a1 = x + v1;
				b0 = y - u1;
				b1 = y - u0;
				break;
			default:
				a0 = x + u0;
				a1 = x + u1;
				b0 = y + v0;
				b1 = y + v1;
			}
			if ((e = put(ws, wm, a0, H(ws) - 1 - b1, a1 - a0 + 1,
			    b1 - b0 + 1, bit ^ inv, c)) != GFX_OK)
				return (e);
		}
	}
	return (GFX_OK);
}

/* device points to rasters, closing the path when shut */
static int *toras(ws, p, n, shut)
struct gsxws *ws;
int *p;
int n, shut;
{
	register int	i;

	for (i = 0; i < n; i++) {
		rbuf[2 * i] = p[2 * i];
		rbuf[2 * i + 1] = H(ws) - 1 - p[2 * i + 1];
	}
	if (shut) {
		rbuf[2 * n] = rbuf[0];
		rbuf[2 * n + 1] = rbuf[1];
	}
	return (rbuf);
}

static int pline(ws, p, n, c, pat, shut)
register struct gsxws *ws;
int *p;
int n, c, shut;
unsigned pat;
{
	int	bg, mode;

	bg = -1;
	mode = GFX_REPLACE;
	if (ws->wm == 1)
		bg = ws->pix[0];
	else if (ws->wm == 3)
		mode = GFX_XOR;
	else if (ws->wm == 4)
		c = ws->pix[0];
	return (gfx_pline(ws->s, toras(ws, p, n, shut), shut ? n + 1 : n, c,
	    bg, mode, pat));
}

/* Pattern byte for raster row y, 0 when hollow. */
static int prow(ws, y)
register struct gsxws *ws;
int y;
{
	switch (ws->fis) {
	case 1:
		return (0xFF);
	case 2:
		return (gsxfont[32 + 4 * (ws->fsi - 1) + (y & 3)]);
	case 3:
		return (gsxfont[64 + 8 * (ws->fsi - 1) + (y & 7)]);
	}
	return (0);
}

/* nearest integer to a / d, d > 0, halves up */
static int rdiv(a, d)
long a;
int d;
{
	long	n, q;

	n = 2 * a + d;
	q = n / (2L * d);
	if (n % (2L * d) != 0 && n < 0)
		q--;
	return ((int) q);
}

/* An outline in XOR would cancel the fill's edge; it is left off. */
static int outline(ws, p, n, c)
struct gsxws *ws;
int *p;
int n, c;
{
	if (ws->wm == 3 && ws->fis != 0)
		return (GFX_OK);
	return (pline(ws, p, n, c, 0xFFFF, 1));
}

/*
 * Fill raster rows by the crossings of each edge, an edge owning rows
 * from its upper end to just short of its lower; the outline closes
 * the last row.
 */
static int polyfill(ws, p, n, c)
register struct gsxws *ws;
int *p;
int n, c;
{
	register int	i, j;
	int		*r, y, y0, y1, x0, x1, m, t, e;

	if (n < 2)
		return (GFX_OK);
	r = toras(ws, p, n, 0);
	y0 = y1 = r[1];
	for (i = 1; i < n; i++) {
		if (r[2 * i + 1] < y0)
			y0 = r[2 * i + 1];
		if (r[2 * i + 1] > y1)
			y1 = r[2 * i + 1];
	}
	if (y0 < 0)
		y0 = 0;
	if (y1 >= H(ws))
		y1 = H(ws) - 1;
	for (y = y0; ws->fis != 0 && y <= y1; y++) {
		m = 0;
		for (i = 0; i < n; i++) {
			j = i + 1 < n ? i + 1 : 0;
			if (r[2 * i + 1] < r[2 * j + 1]) {
				x0 = r[2 * i];
				t = r[2 * i + 1];
				x1 = r[2 * j];
				e = r[2 * j + 1];
			} else {
				x0 = r[2 * j];
				t = r[2 * j + 1];
				x1 = r[2 * i];
				e = r[2 * i + 1];
			}
			if (y < t || y >= e)
				continue;
			xs[m++] = x0 + rdiv((long) (y - t) * (x1 - x0), e - t);
		}
		for (i = 1; i < m; i++)
			for (j = i; j > 0 && xs[j - 1] > xs[j]; j--) {
				t = xs[j];
				xs[j] = xs[j - 1];
				xs[j - 1] = t;
			}
		for (i = 0; i + 1 < m; i += 2)
			if ((e = pspan(ws, xs[i], xs[i + 1], y, prow(ws, y), c))
			    != GFX_OK)
				return (e);
	}
	return (outline(ws, p, n, c));
}

static int bar(ws, p, c)
register struct gsxws *ws;
int *p;
int c;
{
	int	q[8], y, y0, y1, r;

	y0 = H(ws) - 1 - p[1];
	y1 = H(ws) - 1 - p[3];
	if (y0 > y1) {
		y = y0;
		y0 = y1;
		y1 = y;
	}
	if (y0 < 0)
		y0 = 0;
	if (y1 >= H(ws))
		y1 = H(ws) - 1;
	for (y = y0; ws->fis != 0 && y <= y1; y++)
		if ((r = pspan(ws, p[0], p[2], y, prow(ws, y), c)) != GFX_OK)
			return (r);
	q[0] = q[6] = p[0];
	q[1] = q[3] = p[1];
	q[2] = q[4] = p[2];
	q[5] = q[7] = p[3];
	return (outline(ws, q, 4, c));
}

/* ---- the alpha screen and the graphic cursor ---- */

static int white(ws)
struct gsxws *ws;
{
	return (ws->ncol == 2 ? 1 : 15);
}

static VOID xhair(ws)
register struct gsxws *ws;
{
	int	x, y;

	x = ws->gx;
	y = ws->gy;
	gfx_span(ws->s, x - 8, x + 8, y, white(ws), GFX_XOR);
	gfx_line(ws->s, x, y - 8, x, y - 1, white(ws), GFX_XOR);
	gfx_line(ws->s, x, y + 1, x, y + 8, white(ws), GFX_XOR);
}

static VOID nocur(ws)
struct gsxws *ws;
{
	if (ws->gc)
		xhair(ws);
	ws->gc = 0;
}

static int escape(ws, pb)
register struct gsxws *ws;
register struct gsxpb *pb;
{
	register int	i;
	int		rows, cols, y, r;

	rows = H(ws) / 8;
	cols = W(ws) / 8;
	y = 8 * (ws->row - 1);
	switch (pb->contrl[5]) {
	case 1:
		pb->intout[0] = rows;
		pb->intout[1] = cols;
		pb->contrl[4] = 2;
		break;
	case 2:
	case 3:
		break;
	case 4:
		if (ws->row > 1)
			ws->row--;
		break;
	case 5:
		if (ws->row < rows)
			ws->row++;
		break;
	case 6:
		if (ws->col < cols)
			ws->col++;
		break;
	case 7:
		if (ws->col > 1)
			ws->col--;
		break;
	case 8:
		ws->row = ws->col = 1;
		break;
	case 9:
		gfx_fill(ws->s, 0, y + 8, W(ws), H(ws) - y - 8, ws->pix[0],
		    GFX_REPLACE);
		/* and the rest of the line */
	case 10:
		return (gfx_fill(ws->s, 8 * (ws->col - 1), y, W(ws), 8,
		    ws->pix[0], GFX_REPLACE));
	case 11:
		ws->row = pb->intin[0];
		ws->col = pb->intin[1];
		if (ws->row < 1)
			ws->row = 1;
		if (ws->row > rows)
			ws->row = rows;
		if (ws->col < 1)
			ws->col = 1;
		if (ws->col > cols)
			ws->col = cols;
		break;
	case 12:
		for (i = 0; i < pb->contrl[3]; i++) {
			r = glyph(ws, &gsxfont[8 * (pb->intin[i] & 127)], 8,
			    8 * (ws->col - 1), H(ws) - 1 - 8 * (ws->row - 1),
			    0, 0, 1, 0, white(ws), 1, ws->rev);
			if (r != GFX_OK)
				return (r);
			if (++ws->col > cols) {
				ws->col = 1;
				if (ws->row < rows)
					ws->row++;
			}
		}
		break;
	case 13:
		ws->rev = 1;
		break;
	case 14:
		ws->rev = 0;
		break;
	case 15:
		pb->intout[0] = ws->row;
		pb->intout[1] = ws->col;
		pb->contrl[4] = 2;
		break;
	case 16:
		pb->intout[0] = 0;
		pb->contrl[4] = 1;
		break;
	case 18:
		nocur(ws);
		ws->gx = pb->ptsin[0];
		ws->gy = H(ws) - 1 - pb->ptsin[1];
		ws->gc = 1;
		xhair(ws);
		break;
	case 19:
		nocur(ws);
		break;
	default:
		return (GSX_NA);
	}
	return (GSX_OK);
}

/* ---- Open Workstation ---- */

static VOID openws(ws, pb)
register struct gsxws *ws;
register struct gsxpb *pb;
{
	register int	i, k;
	int		*o;

	ws->lt = pb->intin[1];
	ws->lc = pb->intin[2];
	ws->mt = pb->intin[3];
	ws->mc = pb->intin[4];
	ws->tc = pb->intin[6];
	ws->fis = pb->intin[7];
	ws->fsi = pb->intin[8];
	ws->fc = pb->intin[9];
	ws->wm = 1;
	ws->ts = 1;
	ws->up = 0;
	ws->row = ws->col = 1;
	ws->rev = 0;
	ws->gc = 0;
	for (i = 0; i < 16; i++) {
		ws->pix[i] = ws->ncol == 2 ? (i != 0) : defpix[i];
		for (k = 0; k < 3; k++)
			ws->rgb[3 * i + k] = prgb(ws, ws->pix[i], k);
	}
	attrs(ws);
	gfx_fill(ws->s, 0, 0, W(ws), H(ws), ws->pix[0], GFX_REPLACE);

	o = pb->intout;
	for (i = 0; i < 45; i++)
		o[i] = -1;
	o[0] = W(ws) - 1;
	o[1] = H(ws) - 1;
	o[2] = 1;			/* not exactly scaled		*/
	o[3] = (int) (256000L / W(ws));	/* pixel size in um, taking	*/
	o[4] = (int) (192000L / H(ws));	/* a 4:3 picture 256 mm wide	*/
	o[5] = 8;			/* character heights		*/
	o[6] = 8;			/* line types			*/
	o[7] = 1;			/* line widths			*/
	o[8] = 8;			/* marker types			*/
	o[9] = 1;			/* marker sizes			*/
	o[10] = 1;			/* fonts			*/
	o[11] = 8;			/* patterns			*/
	o[12] = 8;			/* hatches			*/
	o[13] = ws->ncol;		/* colours at once		*/
	o[14] = 1;			/* GDPs: bar			*/
	o[15] = 1;
	o[25] = 3;			/* drawn with fill attributes	*/
	o[35] = ws->ncol > 2;		/* colour			*/
	o[36] = 1;			/* text turns by 90 degrees	*/
	o[37] = 1;			/* fill area			*/
	o[38] = 0;			/* no cell array read-back	*/
	o[39] = ws->ncol;		/* palette			*/
	o[40] = o[41] = o[42] = o[43] = 0;	/* no input devices	*/
	o[44] = 0;			/* output only			*/

	o = pb->ptsout;
	o[0] = o[1] = 7;		/* character size, least	*/
	o[2] = o[3] = 56;		/* and most			*/
	o[4] = 1;			/* line width, least		*/
	o[5] = 0;
	o[6] = 1;			/* and most			*/
	o[7] = 0;
	o[8] = 0;			/* marker height, least		*/
	o[9] = 7;
	o[10] = 0;			/* and most			*/
	o[11] = 7;
	pb->contrl[2] = 6;
	pb->contrl[4] = 45;
}

/* ---- the driver ---- */

static int out1(pb, v)
struct gsxpb *pb;
int v;
{
	pb->intout[0] = v;
	pb->contrl[4] = 1;
	return (GSX_OK);
}

static int pts1(pb, x, y)
struct gsxpb *pb;
int x, y;
{
	pb->ptsout[0] = x;
	pb->ptsout[1] = y;
	pb->contrl[2] = 1;
	return (GSX_OK);
}

VOID gsxinit(ws, s)
register struct gsxws *ws;
struct gfxsurf *s;
{
	ws->s = s;
	ws->ncol = s->bpp == 1 ? 2 : 16;
	ws->xpix = s->width;
	ws->ypix = s->height;
	ws->gc = 0;
}

int gsxdrv(ws, pb)
register struct gsxws *ws;
register struct gsxpb *pb;
{
	register int	i, k;
	int		n, *p, *rgb, r;

	pb->contrl[2] = 0;
	pb->contrl[4] = 0;
	n = pb->contrl[1];
	p = pb->ptsin;
	if (n > GSXMAXV)
		return (GSX_NA);
	switch (pb->contrl[0]) {
	case 1:
		openws(ws, pb);
		break;
	case 2:
		nocur(ws);
		break;
	case 3:
		ws->gc = 0;
		ws->row = ws->col = 1;
		return (gfx_fill(ws->s, 0, 0, W(ws), H(ws), ws->pix[0],
		    GFX_REPLACE));
	case 4:
		break;
	case 5:
		return (escape(ws, pb));
	case 6:
		if (n < 2)
			break;
		return (pline(ws, p, n, ws->pix[ws->lc], lstyle[ws->lt - 1], 0));
	case 7:
		for (i = 0; i < n; i++)
			if ((r = glyph(ws, &gsxfont[8 * (15 + ws->mt)], 7,
			    p[2 * i], p[2 * i + 1], -4, 3, 1, 0,
			    ws->pix[ws->mc], ws->wm, 0)) != GFX_OK)
				return (r);
		break;
	case 8:
		for (i = 0; i < pb->contrl[3]; i++)
			if ((r = glyph(ws, &gsxfont[8 * (pb->intin[i] & 127)], 8,
			    p[0], p[1], 8 * i, 6, ws->ts, ws->up,
			    ws->pix[ws->tc], ws->wm, 0)) != GFX_OK)
				return (r);
		break;
	case 9:
		return (polyfill(ws, p, n, ws->pix[ws->fc]));
	case 10:
		/* the cell array is only outlined */
		k = ws->fis;
		ws->fis = 0;
		r = bar(ws, p, ws->pix[ws->lc]);
		ws->fis = k;
		return (r);
	case 11:
		if (pb->contrl[5] != 1)
			return (GSX_NA);
		return (bar(ws, p, ws->pix[ws->fc]));
	case 12:
		k = p[1] / 7;
		ws->ts = k < 1 ? 1 : k > 8 ? 8 : k;
		pb->ptsout[0] = pb->ptsout[1] = 7 * ws->ts;
		pb->ptsout[2] = pb->ptsout[3] = 8 * ws->ts;
		pb->contrl[2] = 2;
		break;
	case 13:
		k = pb->intin[0];
		ws->up = k < 0 || k > 3150 ? 0 : (k + 449) / 900 * 900;
		return (out1(pb, ws->up));
	case 14:
		k = pb->intin[0];
		if (k < 0 || k >= ws->ncol)
			return (GSX_NA);
		rgb = &ws->rgb[3 * k];
		for (i = 0; i < 3; i++)
			rgb[i] = pb->intin[i + 1];
		ws->pix[k] = nearest(ws, rgb);
		pb->intout[0] = k;
		for (i = 0; i < 3; i++)
			pb->intout[i + 1] = prgb(ws, ws->pix[k], i);
		pb->contrl[4] = 4;
		break;
	case 15:
		ws->lt = pb->intin[0];
		attrs(ws);
		return (out1(pb, ws->lt));
	case 16:
		return (pts1(pb, 1, 0));
	case 17:
		ws->lc = cidx(ws, pb->intin[0]);
		return (out1(pb, ws->lc));
	case 18:
		ws->mt = pb->intin[0];
		attrs(ws);
		return (out1(pb, ws->mt));
	case 19:
		return (pts1(pb, 0, 7));
	case 20:
		ws->mc = cidx(ws, pb->intin[0]);
		return (out1(pb, ws->mc));
	case 21:
		return (out1(pb, 1));
	case 22:
		ws->tc = cidx(ws, pb->intin[0]);
		return (out1(pb, ws->tc));
	case 23:
		ws->fis = pb->intin[0];
		attrs(ws);
		return (out1(pb, ws->fis));
	case 24:
		ws->fsi = pb->intin[0];
		attrs(ws);
		return (out1(pb, ws->fsi));
	case 25:
		ws->fc = cidx(ws, pb->intin[0]);
		return (out1(pb, ws->fc));
	case 26:
		k = pb->intin[0];
		if (k < 0 || k >= ws->ncol)
			return (out1(pb, -1));
		pb->intout[0] = k;
		for (i = 0; i < 3; i++)
			pb->intout[i + 1] = pb->intin[1]
			    ? prgb(ws, ws->pix[k], i) : ws->rgb[3 * k + i];
		pb->contrl[4] = 4;
		break;
	case 32:
		ws->wm = pb->intin[0];
		attrs(ws);
		return (out1(pb, ws->wm));
	default:
		return (GSX_NA);
	}
	return (GSX_OK);
}

/* ---- GDOS: normalised coordinates ---- */

/* 0..32767 to 0..p-1, as GDOS's (2v * p) >> 16 in 16 bits */
static int todev(v, p)
int v, p;
{
	return ((int) (((long) (unsigned) (v << 1) * p) >> 16));
}

/* and back, rounding up, beyond the device pinned at 32767 */
static int tondc(v, p)
int v, p;
{
	if ((unsigned) v >= (unsigned) p)
		return (32767);
	return ((int) (((((long) v << 16) + 2L * p - 1) / p) >> 1));
}

int gsx(ws, pb)
register struct gsxws *ws;
register struct gsxpb *pb;
{
	register int	i;
	int		r, *in;

	if (pb->contrl[1] > GSXMAXV)
		return (GSX_NA);
	for (i = 0; i < 2 * pb->contrl[1]; i++)
		nbuf[i] = todev(pb->ptsin[i], i & 1 ? ws->ypix : ws->xpix);
	in = pb->ptsin;
	pb->ptsin = nbuf;
	r = gsxdrv(ws, pb);
	pb->ptsin = in;
	if (pb->contrl[0] == 1) {
		ws->xpix = pb->intout[0] + 1;
		ws->ypix = pb->intout[1] + 1;
	}
	for (i = 0; i < 2 * pb->contrl[2]; i++)
		pb->ptsout[i] = tondc(pb->ptsout[i],
		    i & 1 ? ws->ypix : ws->xpix);
	return (r);
}
