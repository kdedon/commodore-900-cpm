/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/*
 * tek.c - the Tektronix 4010 graphics parser.
 */

#include "gfx.h"
#include "tek.h"

#define	FF	014
#define	CR	015
#define	ESC	033
#define	FS	034
#define	GS	035
#define	US	037

/* Tek home, the top-left alpha position */
#define	HOMEY	767

/* Tek address to surface pixel, the corners landing on the corners */
static int tekx(s, x)
struct gfxsurf *s;
int x;
{
	return ((int) (((long) x * (s->width - 1) + 511) / 1023));
}

static int teky(s, y)
struct gfxsurf *s;
int y;
{
	return (s->height - 1 - (int) (((long) y * (s->height - 1) + 389) / 779));
}

static VOID home(t)
register struct tek *t;
{
	t->hy = HOMEY >> 5;
	t->ly = HOMEY & 31;
	t->hx = 0;
	t->lx = 0;
	t->x = tekx(t->s, 0);
	t->y = teky(t->s, HOMEY);
}

VOID tekinit(t, s)
register struct tek *t;
struct gfxsurf *s;
{
	t->s = s;
	t->mode = TEK_ALPHA;
	t->dark = 0;
	t->esc = 0;
	t->loy = 0;
	home(t);
}

VOID tekclear(t)
register struct tek *t;
{
	gfx_fill(t->s, 0, 0, t->s->width, t->s->height, 0, GFX_REPLACE);
	t->mode = TEK_ALPHA;
	t->esc = 0;
	home(t);
}

static VOID enter(t, mode)
register struct tek *t;
int mode;
{
	t->mode = mode;
	t->dark = 1;
	t->loy = 0;
}

/* Lo-X has arrived: move, draw or plot to the address. */
static VOID address(t)
register struct tek *t;
{
	register struct gfxsurf *s;
	int x, y, c;

	s = t->s;
	x = tekx(s, (t->hx << 5) | t->lx);
	y = teky(s, (t->hy << 5) | t->ly);
	c = (1 << s->bpp) - 1;
	if (t->mode == TEK_PT)
		gfx_pixel(s, x, y, c, GFX_REPLACE);
	else if (!t->dark)
		gfx_line(s, t->x, t->y, x, y, c, GFX_REPLACE);
	t->dark = 0;
	t->x = x;
	t->y = y;
}

int tekput(t, c)
register struct tek *t;
register int c;
{
	c &= 0x7f;
	if (t->mode == TEK_ALPHA) {
		if (c == GS)
			enter(t, TEK_VEC);
		else if (c == FS)
			enter(t, TEK_PT);
		else
			return (1);
		return (0);
	}
	if (t->esc) {
		t->esc = 0;
		if (c == FF)
			tekclear(t);
		return (0);
	}
	switch (c) {
	case ESC:
		t->esc = 1;
		return (0);
	case GS:
		enter(t, TEK_VEC);
		return (0);
	case FS:
		enter(t, TEK_PT);
		return (0);
	case US:
		t->mode = TEK_ALPHA;
		return (0);
	case CR:
		t->mode = TEK_ALPHA;
		return (1);
	}
	switch (c & 0x60) {
	case 0x00:
		break;
	case 0x20:
		if (t->loy)
			t->hx = c & 31;
		else
			t->hy = c & 31;
		break;
	case 0x60:
		t->ly = c & 31;
		t->loy = 1;
		break;
	case 0x40:
		t->lx = c & 31;
		t->loy = 0;
		address(t);
		break;
	}
	return (0);
}
