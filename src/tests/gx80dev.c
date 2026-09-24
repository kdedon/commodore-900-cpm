/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/*
 * The shim's GSX-80 device for GX80.COM: Open binds BIOS function 34 to
 * 64 x 32 x 1 at the guest's 1000h, where GX80 checks the pixels, and
 * BDOS function 115 draws the rest.
 */

#include "cpm.h"
#include "../shim/z80.h"
#include "gfx.h"

static struct biospb	bp;
static struct gfxsurf	s = { 64, 32, 1, 8, 0, 0, 128L };

int z80gsxd(m, g)
struct z80 *m;
struct gdospb *g;
{
	if (*g->contrl != 1)
		return (gdos(g));
	s.seg0 = (unsigned char *)m->m + 0x1000;
	s.seg1 = s.seg0 + 128;
	bp.code = BIOS_GSX;
	bp.p1 = (long)&s;
	bp.p2 = (long)g;
	return ((int)__bdosl(BDOS_BIOSCALL, (long)&bp));
}
