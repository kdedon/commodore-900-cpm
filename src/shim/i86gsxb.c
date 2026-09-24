/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/* The GSX-86 device: the graphics module on the HR screen, through
 * BIOS function 34.  No screen answers -1, which refuses the Open. */

#include "cpm.h"
#include "i86.h"

static struct biospb	bp;

int i86gsxb(m, g)
struct i86 *m;
char *g;
{
	bp.code = BIOS_GSX;
	bp.p1 = 0L;
	bp.p2 = (long)g;
	return ((int)__bdosl(BDOS_BIOSCALL, (long)&bp));
}
