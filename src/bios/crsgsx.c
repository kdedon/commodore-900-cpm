/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/*
 * crsgsx.c - GSX calls on the HR bitmap, in the graphics module.
 */

#include "../lib/gfx.h"
#include "../lib/gsx.h"

extern struct gfxsurf	hrsurf;

static struct gsxws	ws;

/*
 * One GSX call on s, or on the HR bitmap when s is null.  Open
 * Workstation binds the surface; before it, every call is GSX_NA.
 */
int gxcall(s, pb)
struct gfxsurf *s;
struct gsxpb *pb;
{
	if (pb->contrl[0] == 1)
		gsxinit(&ws, s ? s : &hrsurf);
	else if (ws.s == 0)
		return (GSX_NA);
	return (gsx(&ws, pb));
}
