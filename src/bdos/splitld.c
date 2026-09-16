/*
 * splitld.c -- load-time half of the split-I/D shim, resident side.
 *
 * Called by pgmld once a 0xEE0B program's segments are in place: code in
 * the TPA (code bank), CON/DAT in the data bank.  Maps the data-bank and
 * side-table segments and clears the side table and the scanner's
 * LDR-target scratch bitmap, then hands the walk itself to spscan() in
 * the relocated module (splitscan.c, segment SPLITMSEG).
 *
 * The split is where the resident cost is: the two mapseg() calls and
 * the two block clears are a handful of instructions that would
 * otherwise pull mapseg_ and mem_clr_ into the module and cost it its
 * self-containment -- the property that lets the module link without
 * knowing a single CPM.SYS address, and so without ever going stale
 * against one.
 *
 * The scanner's LDR-target scratch bitmap lives in the upper part of
 * the side-table segment (offset SPLITMAXT), which is why text is
 * capped at SPLITMAXT bytes (pgmld enforces this; the largest known
 * 0xEE0B text, ZCC2's, is 0xD23E).
 */

#include "c900cfg.h"
#include "zsplit.h"

extern int mapseg();
extern int mem_clr();
extern int spscan();		/* splitmod.s equate -> the module */

short	spflag;		/* nonzero: the loaded program is split I/D --
			 * read by the SC gate's nonseg pointer mapping */

int spload(tsize)
zw tsize;
{
	register zw nw;

	mapseg(SPLITDSEG, SPLITDPAGE, 2);	/* attr 2 = SYS r/w */
	mapseg(SPLITTSEG, SPLITTPAGE, 2);

	nw = tsize >> 1;
	mem_clr(SPLITTBASE, (long)tsize);

	/* LDR-target bitmap: (nw + 7) / 8 bytes, rounded up to a word --
	 * the whole region above SPLITMAXT is scanner scratch */
	mem_clr(SPLITTBASE + (long)SPLITMAXT,
	    (long)((((nw + 7) >> 3) + 1) & ~1));

	return (spscan(tsize));
}
