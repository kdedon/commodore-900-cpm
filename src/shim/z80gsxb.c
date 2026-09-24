/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/* The GSX-80 device: BDOS function 115 on the HR screen. */

#include "cpm.h"
#include "z80.h"

int z80gsxd(m, g)
struct z80 *m;
struct gdospb *g;
{
	return (gdos(g));
}
