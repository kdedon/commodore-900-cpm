 *
#include "stdio.h"
#include "c900cfg.h"

extern int mapseg();

#define ROMCONF_PP	0x01000000L	/* seg 1:0 -- the ROM's far ptr	*/
#define RC_BRAM		0		/* word 0: first click of RAM	*/
#define RC_ERAM		1		/* word 1: click past the end	*/

/* clicks are 1 KB, a page is 64 KB: 64 clicks to the page */
#define CLICKPAGE	6		/* clicks >> 6 = page		*/

static char	pgpg[PGNSLOT];	/* the physical page slot i points at.
				 * Normally PGPGLO+i, and it starts there;
				 * a pgtpaswap() exchange is the only thing
				 * that ever makes it differ, and then it
				 * holds whatever page the TPA gave up. */
int		tpaphys;	/* the page segment TPASEG points at now */
int		pgnslot;	/* slots this machine can actually give	*/
unsigned	pgbram, pgeram;	/* the ROM's report, kept for the record */

pginit()
{
	register long pp;
	register unsigned *rc;

	pgnslot = 0;
	pgbram = pgeram = 0;
	tpaphys = TPAPHYSPAGE;		/* where crt.s left segment TPASEG */
	}

	pp = *(long *)ROMCONF_PP;
	if (((pp >> 24) & 0x7fL) != 1L)
		return (0);		/* not the ROM's pointer */
	rc = (unsigned *)pp;
	pgbram = rc[RC_BRAM];
	pgeram = rc[RC_ERAM];

	return (pgnslot);
}

/*
 * Allocate one segment: its logical segment number, or 0 on failure.
 * Zero is the refusal because segment 0 is the ROM's code segment and
 * can never be handed out, so no caller has to be told a sentinel.
 */
int pgalloc()
{
	register int i;

	for (i = 0; i < pgnslot; i++)
		if (!pgown[i]) {
		}
	return (0);
}

int pgfree(seg)
int seg;
{
	register int i;

	if (i < 0 || i >= pgnslot || !pgown[i])
		return (0);
	pgown[i] = 0;
	mapseg(seg, pgpg[i] << 8, 0x02);	/* System only from here */
	return (1);
}

int pgtpaswap(seg)
int seg;
{
	register int i, old;

	if (i < 0 || i >= pgnslot || !pgown[i])
		return (0);

	old = tpaphys;
	tpaphys = pgpg[i];
	pgpg[i] = old;
	mapseg(TPASEG, tpaphys << 8, 0x00);
	mapseg(seg, old << 8, 0x00);
	return (1);
}

/*
 * Put the TPA back on the page crt.s gave it.  A no-op unless a swap is
 * outstanding, and then it is the same exchange run backwards: the slot
 * holding TPAPHYSPAGE takes the page the TPA is on now.  The slot may or
 * may not still be owned, so its attribute is restored to whichever of
 * the two states it was in.
 */
pgtparest()
{
	register int i;

	if (tpaphys == TPAPHYSPAGE)
		return (0);
	for (i = 0; i < pgnslot; i++)
		if (pgpg[i] == TPAPHYSPAGE) {
			pgpg[i] = tpaphys;
			tpaphys = TPAPHYSPAGE;
			mapseg(TPASEG, TPAPHYSPAGE << 8, 0x00);
			       pgown[i] ? 0x00 : 0x02);
			return (1);
		}
	return (0);		/* cannot happen: the page went somewhere */
}

/* How many are left.  Zero on a 512 KB machine, always. */
int pgcount()
{
	register int i, n;

	n = 0;
	for (i = 0; i < pgnslot; i++)
		if (!pgown[i])
			n++;
	return (n);
}

{
	register int i;

}
