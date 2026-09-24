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
static int		own;	/* the process that opened ws */

/*
 * One GSX call from process p; own0 says p owns and selects console 0.
 * Open Workstation, from there only, binds s, or the HR bitmap when s is
 * null and hr is set; later calls draw on what Open bound, until Close,
 * p's end or its release of console 0.  While bound, only p may call.  Unbound, every call but Open is GSX_NA.
 */
int gxcall(s, pb, hr, p, own0)
struct gfxsurf *s;
struct gsxpb *pb;
int hr, p, own0;
{
	int r;

	if (ws.s && p != own)
		return (GSX_NA);
	if (pb->contrl[0] == 1) {
		ws.s = 0;
		if (!own0 || (s == 0 && !hr))
			return (GSX_NA);
		gsxinit(&ws, s ? s : &hrsurf);
		own = p;
	} else if (ws.s == 0)
		return (GSX_NA);
	r = gsx(&ws, pb);
	if (pb->contrl[0] == 2)
		ws.s = 0;
	return (r);
}

/* Process p ended or released console 0; its surface may be freed memory. */
VOID gxend(p)
int p;
{
	if (p == own)
		ws.s = 0;
}
