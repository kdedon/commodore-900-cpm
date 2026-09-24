/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/*
 * crstek.c - the HR console's Tek 4010 side, in the graphics module.
 *
 * Alpha mode entered from graphics (US, or CR) puts text at the beam in
 * 14 x 22-unit cells, 74 x 35 to the screen, until ESC.
 */

#include "../lib/gfx.h"
#include "../lib/tek.h"

#define	BS	010
#define	LF	012
#define	CR	015
#define	ESC	033
#define	US	037

#define	TCW	14		/* alpha cell, Tek units */
#define	TCH	22
#define	HOMEY	767

extern struct gfxsurf *hs;
extern hcat(), hcoff();

static struct tek tek;
static int talpha;		/* text goes to the beam */

/* Point the parser at the console's surface, in alpha mode. */
tkinit()
{
	tekinit(&tek, hs);
	talpha = 0;
}

/* Text at the beam; 1 when c is the console's instead. */
static int beam(c)
int c;
{
	register int x, y;

	x = (tek.hx << 5) | tek.lx;
	y = (tek.hy << 5) | tek.ly;
	switch (c) {
	case ESC:
		talpha = 0;
		return (1);
	case CR:
		x = 0;
		break;
	case LF:
		y -= TCH;
		break;
	case BS:
		if (x >= TCW)
			x -= TCW;
		break;
	default:
		if (c < ' ')
			return (1);
		hcat(tek.x, tek.y, c);
		if ((x += TCW) > 1024 - TCW) {
			x = 0;
			y -= TCH;
		}
	}
	if (y < 0)
		y = HOMEY;
	tekat(&tek, x, y);
	return (0);
}

/*
 * One console byte; 1 when it is text for the console.  gnd says the
 * console's own escape parser is idle: only then may GS or FS start
 * graphics.
 */
int tkput(c, gnd)
int c, gnd;
{
	int	was;

	if (!gnd && tek.mode == TEK_ALPHA)
		return (1);
	was = tek.mode;
	if (!tekput(&tek, c)) {
		if (tek.mode != TEK_ALPHA)
			hcoff();
		else if (was != TEK_ALPHA)
			talpha = (c & 0x7f) == US;
		return (0);
	}
	if (was != TEK_ALPHA)
		talpha = 1;
	return (talpha ? beam(c & 0x7f) : 1);
}

tkclear()
{
	hcoff();
	talpha = 0;
	tekclear(&tek);
}
