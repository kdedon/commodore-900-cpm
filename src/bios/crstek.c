/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/*
 * crstek.c - the HR console's Tek 4010 side, in the graphics module.
 */

#include "../lib/gfx.h"
#include "../lib/tek.h"

static struct gfxsurf hrsurf;
static struct tek tek;

/* Point the parser at the HR bitmap, in alpha mode. */
tkinit()
{
	gfx_hrmono(&hrsurf, (unsigned char *)0x3a000000L,
	    (unsigned char *)0x3b000000L);
	tekinit(&tek, &hrsurf);
}

/*
 * One console byte; 1 when it is text for the console.  gnd says the
 * console's own escape parser is idle: only then may GS or FS start
 * graphics.
 */
int tkput(c, gnd)
int c, gnd;
{
	if (!gnd && tek.mode == TEK_ALPHA)
		return (1);
	return (tekput(&tek, c));
}

tkclear()
{
	tekclear(&tek);
}

/* Run a NUL-terminated Tek stream on the caller's surface. */
tkram(s, p)
struct gfxsurf *s;
char *p;
{
	struct tek t;

	tekinit(&t, s);
	while (*p)
		tekput(&t, *p++ & 0xff);
}
