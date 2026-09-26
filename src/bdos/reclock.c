/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */

/* Record locking, functions 42 and 43.  A lock names a physical record on
 * a drive, so two FCBs for one file meet on the same entry.  The owner
 * and the file name ride along so close and process death can drop them.
 *
 *   0  done			 6  record number out of range
 *   1  record not written	 8  locked by another process
 *   3  cannot close extent	14  lock table full
 *   4  extent not written
 *
 * With a multi-sector count the high byte holds the records processed
 * before a failure.
 */

#include "stdio.h"
#include "bdosdef.h"

EXTERN	WORD	new_ext();
EXTERN	UWORD	get_rc();
EXTERN	WORD	blkindx();
EXTERN	UWORD	blknum();
EXTERN	UWORD	close_fi();
EXTERN	WORD	pgcur;		/* the running process */

#define	NLOCK	16

struct lkent {
	LONG	lk_rec;		/* physical record on the drive	*/
	UBYTE	lk_own;		/* process + 1; 0 is a free entry */
	UBYTE	lk_drv;
	UBYTE	lk_name[12];	/* user, then name and type	*/
};

MLOCAL struct lkent	lktab[NLOCK];
MLOCAL WORD		lkused;


MLOCAL VOID lkname(fcbp, name)
REG struct fcb *fcbp;
REG UBYTE *name;
{
    REG WORD i;

    name[0] = fcbp->drvcode;	/* the user number after tmp_sel */
    for (i = 0; i < 8; i++)
	name[i + 1] = fcbp->fname[i] & 0x7f;
    for (i = 0; i < 3; i++)
	name[i + 9] = fcbp->ftype[i] & 0x7f;
}

MLOCAL WORD lkfind(rec)
LONG rec;
{
    REG WORD i;
    BSETUP

    for (i = 0; i < NLOCK; i++)
	if (lktab[i].lk_own && lktab[i].lk_rec == rec
	    && lktab[i].lk_drv == GBL.curdsk)
	    return (i);
    return (-1);
}

MLOCAL VOID lkfree(i)
WORD i;
{
    lktab[i].lk_own = 0;
    lkused--;
}

/* Position the FCB at its random record, as a random read would, and
 * find that record on the disk. */

MLOCAL UWORD lkseek(fcbp, recp)
REG struct fcb *fcbp;
LONG *recp;
{
    REG UWORD block;
    REG WORD rtn;
    REG struct dpb *dp;
    BSETUP

    if ( rtn = new_ext(fcbp, TRUE, TRUE) ) return (rtn);
    fcbp->cur_rec = fcbp->ran2 & 0x7f;
    if ( UBWORD(fcbp->cur_rec) >= get_rc(fcbp) ) return (1);
    dp = GBL.parmp;
    block = blknum(fcbp, blkindx(fcbp), dp->dsm & ~0xff);
    if (block == 0) return (1);
    *recp = ((LONG)block << dp->bsh) + (LONG)(fcbp->cur_rec & dp->blm);
    return (0);
}

/* Functions 42 (lock) and 43 (unlock). */

GLOBAL UWORD lkrec(fcbp, lock)
REG struct fcb *fcbp;
BOOLEAN lock;
{
    LONG	recs[NLOCK];
    REG WORD	i, j, need, cnt;
    REG UWORD	rtn;
    UBYTE	r0, r1, r2;
    BSETUP

    cnt = UBWORD(GBL.multcnt);
    if (lock && cnt > NLOCK) return (14);
    r0 = fcbp->ran0;
    r1 = fcbp->ran1;
    r2 = fcbp->ran2;
    rtn = 0;
    for (i = 0; i < cnt; i++)
    {
	if ( rtn = lkseek(fcbp, &recs[lock ? i : 0]) ) break;
	if ( ! lock && (j = lkfind(recs[0])) >= 0
	     && lktab[j].lk_own == pgcur + 1 )
	    lkfree(j);
	incr_rr(fcbp);
    }
    fcbp->ran0 = r0;
    fcbp->ran1 = r1;
    fcbp->ran2 = r2;
    if (rtn) return ((i << 8) | rtn);
    if ( ! lock ) return (0);

    /* No disk I/O from here on, so no other process runs. */
    need = 0;
    for (i = 0; i < cnt; i++)
    {
	if ( (j = lkfind(recs[i])) < 0 ) need++;
	else if (lktab[j].lk_own != pgcur + 1) return ((i << 8) | 8);
    }
    if (need > NLOCK - lkused) return (14);
    for (i = 0; i < cnt; i++)
    {
	if (lkfind(recs[i]) >= 0) continue;
	for (j = 0; lktab[j].lk_own; j++)
	    ;
	lktab[j].lk_rec = recs[i];
	lktab[j].lk_own = pgcur + 1;
	lktab[j].lk_drv = GBL.curdsk;
	lkname(fcbp, lktab[j].lk_name);
	lkused++;
    }
    return (0);
}

/* Is this record locked by another process?  Writes to it fail. */

GLOBAL BOOLEAN lkother(block, rcrd)
UWORD block;
UBYTE rcrd;
{
    REG WORD j;
    REG struct dpb *dp;
    BSETUP

    if (lkused == 0) return (FALSE);
    dp = GBL.parmp;
    j = lkfind(((LONG)block << dp->bsh) + (LONG)(rcrd & dp->blm));
    return (j >= 0 && lktab[j].lk_own != pgcur + 1);
}

/* Function 16: a close drops the caller's locks on the file. */

GLOBAL UWORD lkclose(fcbp)
REG struct fcb *fcbp;
{
    REG UWORD rtn;
    REG WORD i, k;
    UBYTE name[12];
    BSETUP

    rtn = close_fi(fcbp);
    if (lkused == 0 || rtn >= 255) return (rtn);
    lkname(fcbp, name);
    for (i = 0; i < NLOCK; i++)
    {
	if (lktab[i].lk_own != pgcur + 1 || lktab[i].lk_drv != GBL.curdsk)
	    continue;
	for (k = 0; k < 12 && lktab[i].lk_name[k] == name[k]; k++)
	    ;
	if (k == 12) lkfree(i);
    }
    return (rtn);
}

/* The running process is ending: drop everything it holds. */

GLOBAL VOID lkdrop()
{
    REG WORD i;

    for (i = 0; i < NLOCK; i++)
	if (lktab[i].lk_own == pgcur + 1) lkfree(i);
}
