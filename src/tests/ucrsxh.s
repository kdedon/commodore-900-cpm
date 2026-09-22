/ Copyright (c) 2026 Kevin Dedon.
/ SPDX-License-Identifier: MIT

/ ucrsxh.s -- UCASEH.RSX is linked at 0xF700 to leave enough TPA for DDT.
/ Its distinct name and function 60/203 identify it in a chain.

#define	RSXORG	0xF700
#define	RSXSUB	0xCB
#define	RSXNAM1	0x55, 0x43, 0x41, 0x53	/ UCAS
#define	RSXNAM2	0x45, 0x48, 0x20, 0x20	/ EH

#include "ucrsx.s"
