/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/* Size the segment pool.  Linked into the boot module. */
#include "stdio.h"
#include "c900cfg.h"

extern char	pgown[], pghld[], pgpg[];
extern int	pgcur, tpaphys, pgnslot;
extern unsigned	pgbram, pgeram;

/* Segment 1 offset zero contains a CPU far pointer, (seg<<24)|offset,
 * to the ROM configuration block. rom_bram/rom_eram are its first two
 * words, in 1 KB clicks (COHERENT machine.h ctob() uses shift 10). */
#define ROMCONF_PP	0x01000000L	/* seg 1:0 -- the ROM's far ptr	*/
#define RC_BRAM		0		/* word 0: first click of RAM	*/
#define RC_ERAM		1		/* word 1: click past the end	*/

/* Size the pool once at cold boot. Unrecognized ROM reports leave it
 * empty; resident pages and the top RAM page are never allocated. */
pginit()
{
	register long pp;
	register unsigned *rc;
	register int i;

	pgnslot = 0;
	pgbram = pgeram = 0;
	pgcur = 0;
	tpaphys = TPAPHYSPAGE;		/* where crt.s left segment TPASEG */
	for (i = 0; i < PGNSLOT; i++) {
		pgown[i] = 0;
		pghld[i] = 0;
		pgpg[i] = PGPGLO + i;
	}

	pp = *(long *)ROMCONF_PP;
	if (((pp >> 24) & 0x7fL) != 1L)
		return (0);		/* not the ROM's pointer */
	rc = (unsigned *)pp;
	pgbram = rc[RC_BRAM];
	pgeram = rc[RC_ERAM];

	pgnslot = pgsize(pgbram, pgeram);
	return (pgnslot);
}
