/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/* Size the segment pool.  Linked into the boot module. */
#include "stdio.h"
#include "c900cfg.h"

extern char	pgown[], pghld[];
extern unsigned char pgpg[];
extern int	pgcur, tpaphys, pgnslot;
extern int	pgbkfree();
extern unsigned	pgbram, pgeram;

/* Segment 1 offset zero contains a CPU far pointer, (seg<<24)|offset,
 * to the ROM configuration block. rom_bram/rom_eram are its first two
 * words, in 1 KB clicks (COHERENT machine.h ctob() uses shift 10). */
#define ROMCONF_PP	0x01000000L	/* seg 1:0 -- the ROM's far ptr	*/
#define RC_BRAM		0		/* word 0: first click of RAM	*/
#define RC_ERAM		1		/* word 1: click past the end	*/

/* clicks are 1 KB, a page is 64 KB: 64 clicks to the page */
#define CLICKPAGE	6		/* clicks >> 6 = page		*/

static unsigned char pghole[] = PGHOLES;
static unsigned char pgsyspg[] = PGSYSPG;

/* Is page p one the resident system keeps? */
static int pgsys(p)
int p;
{
	register int i;

	for (i = 0; i < PGNSYSPG; i++)
		if (pgsyspg[i] == p)
			return (1);
	return (0);
}

/*
 * The loader's map, nram (first, count) runs, if every page in it is RAM
 * the ROM reported and none is LR text: a loader may reserve more than
 * the holes but never invent RAM.  Runs must ascend without overlap.
 */
static int pgmapok(lo, hi, ram, nram)
int lo, hi, nram;
unsigned short *ram;
{
	register int r, p, c;

	if (nram <= 0)
		return (0);
	for (r = 0; r < nram; r++) {
		p = ram[2 * r];
		c = ram[2 * r + 1];
		if (p < lo || p >= hi || c <= 0 || c > hi - p
		    || (p <= PGLRTEXT && PGLRTEXT - p < c))
			return (0);
		lo = p + c;
	}
	return (1);
}

/*
 * Fill pgpg[] with the pages a ROM report of [bram, eram) clicks backs,
 * less the loader's reservations when its map is good and the holes when
 * not, and answer how many.
 */
int pgsize(bram, eram, ram, nram)
unsigned bram, eram;
unsigned short *ram;
int nram;
{
	register int p, hi, n, h;

	hi = (int)(eram >> CLICKPAGE);	/* one past the last page */

	/* A report that leaves out the resident system is not this machine. */
	if ((int)(bram >> CLICKPAGE) > SYSPHYSPAGE || hi <= PGPGLO)
		return (0);
	if (eram >= (unsigned)(PGPGHI << CLICKPAGE))
		hi = PGPGHI;

	n = 0;
	if (pgmapok((int)(bram >> CLICKPAGE), hi, ram, nram)) {
		for (; nram > 0; nram--, ram += 2)
			for (p = ram[0], h = p + ram[1]; p < h; p++)
				if (p >= PGPGLO && p < hi - 1 && n < PGNSLOT
				    && !pgsys(p))
					pgpg[n++] = p;
		return (n);
	}

	hi--;				/* the ROM's segment-1 page */
	for (p = PGPGLO; p < hi; p++) {
		for (h = 0; h < PGNHOLE && pghole[h] != p; h++)
			;
		if (h == PGNHOLE && !pgsys(p))
			pgpg[n++] = p;
	}
	return (n);
}

/* Size the pool once at cold boot. Unrecognized ROM reports leave it
 * empty; resident pages and the top RAM page are never allocated.
 * ram/nram are the loader's memory map, nram 0 for none. */
pginit(ram, nram)
unsigned short *ram;
int nram;
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
	}
	pgbkfree();			/* no bank mapped until a split load */

	pp = *(long *)ROMCONF_PP;
	if (((pp >> 24) & 0x7fL) != 1L)
		return (0);		/* not the ROM's pointer */
	rc = (unsigned *)pp;
	pgbram = rc[RC_BRAM];
	pgeram = rc[RC_ERAM];

	pgnslot = pgsize(pgbram, pgeram, ram, nram);
	return (pgnslot);
}
