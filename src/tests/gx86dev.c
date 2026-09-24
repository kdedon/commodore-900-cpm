/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/*
 * The shim's GSX-86 device for GX86.CMD: BIOS function 34 draws on
 * 64 x 32 x 1 at the guest's DS:1000h, where GX86 checks the pixels.
 */

#include "cpm.h"
#include "../shim/i86.h"
#include "gfx.h"

static struct biospb	bp;
static struct gfxsurf	s = { 64, 32, 1, 8, 0, 0, 128L };

int i86gsxb(m, g)
struct i86 *m;
int **g;
{
	unsigned char *p;

	if (*g[0] == 1) {
		if ((p = (unsigned char *)i86addr(m, S_DS, (i16)0x1000, 256L))
		    == (unsigned char *)0)
			return (-1);
		s.seg0 = p;
		s.seg1 = p + 128;
	}
	bp.code = BIOS_GSX;
	bp.p1 = (long)&s;
	bp.p2 = (long)g;
	return ((int)__bdosl(BDOS_BIOSCALL, (long)&bp));
}
