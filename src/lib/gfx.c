/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/*
 * gfx.c - surfaces, clipping, and the primitives built on a back-end's
 * pixel and span.  Nothing here knows how a depth packs its pixels.
 */

#include "gfx.h"

extern int	gfx_m1pix(), gfx_m1get(), gfx_m1span(), gfx_m1blit();
extern int	gfx_c4pix(), gfx_c4get(), gfx_c4span(), gfx_c4blit();

VOID gfx_ram(s, w, h, bpp, stride, p0, p1, split)
register struct gfxsurf *s;
int w, h, bpp;
unsigned stride;
unsigned char *p0, *p1;
long split;
{
	s->width = w;
	s->height = h;
	s->bpp = bpp;
	s->stride = stride;
	s->seg0 = p0;
	s->seg1 = p1;
	s->split = split;
}

VOID gfx_hrmono(s, p0, p1)
struct gfxsurf *s;
unsigned char *p0, *p1;
{
	gfx_ram(s, 1024, 800, 1, 128, p0, p1, HRSPLIT);
}

VOID gfx_hrclr(s, p0, p1)
struct gfxsurf *s;
unsigned char *p0, *p1;
{
	gfx_ram(s, 640, 400, 4, 320, p0, p1, HRSPLIT);
}

unsigned char *gfx_addr(s, off)
register struct gfxsurf *s;
long off;
{
	if (off < s->split)
		return (s->seg0 + (unsigned) off);
	return (s->seg1 + (unsigned) (off - s->split));
}

static int badmode(mode)
int mode;
{
	return (mode != GFX_REPLACE && mode != GFX_XOR && mode != GFX_ERASE);
}

int gfx_pixel(s, x, y, c, mode)
register struct gfxsurf *s;
int x, y, c, mode;
{
	if (badmode(mode))
		return (GFX_NOTSUP);
	if (x < 0 || y < 0 || x >= s->width || y >= s->height)
		return (GFX_OK);
	switch (s->bpp) {
	case 1:
		return (gfx_m1pix(s, x, y, c, mode));
	case 4:
		return (gfx_c4pix(s, x, y, c, mode));
	}
	return (GFX_NOTSUP);
}

int gfx_get(s, x, y)
register struct gfxsurf *s;
int x, y;
{
	if (x < 0 || y < 0 || x >= s->width || y >= s->height)
		return (-1);
	switch (s->bpp) {
	case 1:
		return (gfx_m1get(s, x, y));
	case 4:
		return (gfx_c4get(s, x, y));
	}
	return (-1);
}

int gfx_span(s, x0, x1, y, c, mode)
register struct gfxsurf *s;
int x0, x1, y, c, mode;
{
	register int	t;

	if (badmode(mode))
		return (GFX_NOTSUP);
	if (x0 > x1) {
		t = x0;
		x0 = x1;
		x1 = t;
	}
	if (y < 0 || y >= s->height || x1 < 0 || x0 >= s->width)
		return (GFX_OK);
	if (x0 < 0)
		x0 = 0;
	if (x1 >= s->width)
		x1 = s->width - 1;
	switch (s->bpp) {
	case 1:
		return (gfx_m1span(s, x0, x1, y, c, mode));
	case 4:
		return (gfx_c4span(s, x0, x1, y, c, mode));
	}
	return (GFX_NOTSUP);
}

/*
 * Midpoint Bresenham.  acc holds (2 * minor * i + major) mod 2 * major,
 * so step i's minor coordinate is floor((2 * minor * i + major) /
 * (2 * major)): the ideal line rounded, halves away from the start.
 * Each pixel is visited once, which XOR needs.
 */
int gfx_line(s, x0, y0, x1, y1, c, mode)
register struct gfxsurf *s;
int x0, y0, x1, y1, c, mode;
{
	register int	i;
	int		dx, dy, sx, sy, acc, n;

	if (badmode(mode))
		return (GFX_NOTSUP);
	if (y0 == y1)
		return (gfx_span(s, x0, x1, y0, c, mode));
	dx = x1 - x0;
	dy = y1 - y0;
	sx = 1;
	sy = 1;
	if (dx < 0) {
		dx = -dx;
		sx = -1;
	}
	if (dy < 0) {
		dy = -dy;
		sy = -1;
	}
	if (dx >= dy) {
		acc = dx;
		for (n = 0; n <= dx; n++) {
			if ((i = gfx_pixel(s, x0, y0, c, mode)) != GFX_OK)
				return (i);
			x0 += sx;
			if ((acc += 2 * dy) >= 2 * dx) {
				acc -= 2 * dx;
				y0 += sy;
			}
		}
	} else {
		acc = dy;
		for (n = 0; n <= dy; n++) {
			if ((i = gfx_pixel(s, x0, y0, c, mode)) != GFX_OK)
				return (i);
			y0 += sy;
			if ((acc += 2 * dx) >= 2 * dy) {
				acc -= 2 * dy;
				x0 += sx;
			}
		}
	}
	return (GFX_OK);
}

int gfx_fill(s, x, y, w, h, c, mode)
register struct gfxsurf *s;
int x, y, w, h, c, mode;
{
	register int	r;

	if (badmode(mode))
		return (GFX_NOTSUP);
	if (w <= 0 || h <= 0)
		return (GFX_OK);
	if (y < 0) {
		h += y;
		y = 0;
	}
	if (h > s->height - y)
		h = s->height - y;
	for (; h > 0; h--, y++)
		if ((r = gfx_span(s, x, x + w - 1, y, c, mode)) != GFX_OK)
			return (r);
	return (GFX_OK);
}

/* The corners belong to the top and bottom rows only, so XOR leaves
   none of them cancelled. */
int gfx_box(s, x, y, w, h, c, mode)
register struct gfxsurf *s;
int x, y, w, h, c, mode;
{
	register int	r;
	int		x1, y1;

	if (w <= 0 || h <= 0)
		return (badmode(mode) ? GFX_NOTSUP : GFX_OK);
	x1 = x + w - 1;
	y1 = y + h - 1;
	if ((r = gfx_span(s, x, x1, y, c, mode)) != GFX_OK || h == 1)
		return (r);
	if ((r = gfx_span(s, x, x1, y1, c, mode)) != GFX_OK || h == 2)
		return (r);
	if ((r = gfx_line(s, x, y + 1, x, y1 - 1, c, mode)) != GFX_OK
	    || w == 1)
		return (r);
	return (gfx_line(s, x1, y + 1, x1, y1 - 1, c, mode));
}

/*
 * Copy a w x h window of a 1 bpp image, sstride bytes per row, starting
 * sx pixels into each row.  A set source bit applies c under mode; a
 * clear one writes 0 under REPLACE and leaves the pixel otherwise.
 */
int gfx_blit(s, src, sstride, sx, x, y, w, h, c, mode)
register struct gfxsurf *s;
unsigned char *src;
unsigned sstride;
int sx, x, y, w, h, c, mode;
{
	register int	i;
	int		j, bit, r;
	unsigned char	*row;

	if (badmode(mode))
		return (GFX_NOTSUP);
	if (x < 0) {
		sx -= x;
		w += x;
		x = 0;
	}
	if (y < 0) {
		src += (long) -y * sstride;
		h += y;
		y = 0;
	}
	if (w > s->width - x)
		w = s->width - x;
	if (h > s->height - y)
		h = s->height - y;
	if (w <= 0 || h <= 0)
		return (GFX_OK);
	if (s->bpp == 1)
		return (gfx_m1blit(s, src, sstride, sx, x, y, w, h, c, mode));
	if (s->bpp == 4)
		return (gfx_c4blit(s, src, sstride, sx, x, y, w, h, c, mode));

	for (j = 0; j < h; j++) {
		row = src + (long) j * sstride;
		for (i = 0; i < w; i++) {
			bit = (row[(sx + i) >> 3] >> (7 - ((sx + i) & 7))) & 1;
			if (bit)
				r = gfx_pixel(s, x + i, y + j, c, mode);
			else if (mode == GFX_REPLACE)
				r = gfx_pixel(s, x + i, y + j, 0, mode);
			else
				r = GFX_OK;
			if (r != GFX_OK)
				return (r);
		}
	}
	return (GFX_OK);
}

/*
 * A polyline through n points, pts holding x, y pairs, each segment
 * stepped as gfx_line steps it.  pat is a 16-pixel style, most
 * significant bit first, carried on across vertices.  A set bit applies
 * c under mode; a clear one writes bg, or nothing when bg is negative.
 * A shared vertex is drawn once, as is a last point equal to the first,
 * so XOR leaves no holes.
 */
int gfx_pline(s, pts, n, c, bg, mode, pat)
register struct gfxsurf *s;
int *pts;
int n, c, bg, mode;
unsigned pat;
{
	register int	m;
	int		i, k, r, x, y, dx, dy, sx, sy, acc, len, xmaj, shut;

	if (badmode(mode))
		return (GFX_NOTSUP);
	shut = n > 2 && pts[0] == pts[2 * n - 2] && pts[1] == pts[2 * n - 1];
	k = 0;
	for (i = 0; i + 1 < n; i++) {
		x = pts[2 * i];
		y = pts[2 * i + 1];
		dx = pts[2 * i + 2] - x;
		dy = pts[2 * i + 3] - y;
		sx = 1;
		sy = 1;
		if (dx < 0) {
			dx = -dx;
			sx = -1;
		}
		if (dy < 0) {
			dy = -dy;
			sy = -1;
		}
		xmaj = dx >= dy;
		len = xmaj ? dx : dy;
		acc = len;
		for (m = 0; m <= len; m++) {
			if ((m > 0 || i == 0) && !(shut && i == n - 2 && m == len)) {
				if ((pat >> (15 - (k & 15))) & 1)
					r = gfx_pixel(s, x, y, c, mode);
				else if (bg >= 0)
					r = gfx_pixel(s, x, y, bg, GFX_REPLACE);
				else
					r = GFX_OK;
				if (r != GFX_OK)
					return (r);
				k++;
			}
			if (xmaj) {
				x += sx;
				if ((acc += 2 * dy) >= 2 * dx) {
					acc -= 2 * dx;
					y += sy;
				}
			} else {
				y += sy;
				if ((acc += 2 * dx) >= 2 * dy) {
					acc -= 2 * dy;
					x += sx;
				}
			}
		}
	}
	return (GFX_OK);
}
