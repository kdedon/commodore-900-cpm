
/* BDOS physical-sector cache in BUFSEG. Directory writes are write-through;
 * data writes remain dirty until eviction or flush. */

#include "stdio.h"		/* Standard I/O declarations */

#include "c900cfg.h"		/* BUFBASE/BUFPHYS/NBCB */

#include "bcb.h"

EXTERN WORD	wdsec();	/* one physical sector transfer	*/

/* Serialize cache replacement and flushes with the recursive filesystem lock.
 * do_phio already holds it during ordinary sector transfers. */
EXTERN VOID	plock();
EXTERN VOID	punlock();
EXTERN		mem_clr();	/* far zero fill		*/

#define WDREAD	0x08
#define WDWRITE	0x0A

/* Each set has NWAY buffers, with lru[] ordered MRU first. Low block bits
 * select the set; its final LRU entry is the replacement victim. */
#define NWAY	4
#define NSET	(NBCB / NWAY)

MLOCAL LONG	bblk[NBCB];		/* physical block in each buffer */
MLOCAL UBYTE	bval[NBCB];		/* buffer holds bblk[i]		 */
MLOCAL UBYTE	bdrt[NBCB];		/* buffer differs from the disk	 */
MLOCAL UBYTE	lru[NBCB];		/* per set: indices, MRU first	 */
MLOCAL WORD	bcbup;			/* pool initialized		 */


MLOCAL LONG bcbpa(ix)
/* Physical (DMA) address of buffer ix */
REG WORD ix;
{
    return( BUFPHYS + (LONG)(WORD)(ix << BCBSHF) );
}


bcbinit()
/* Discard every buffer.  Nothing is written back: this runs at cold start */
{
    REG WORD i;

    for (i = 0; i < NBCB; i++)
    {
	bval[i] = 0;
	bdrt[i] = 0;
	lru[i] = (UBYTE)i;
    }
    bcbup = 1;
}


WORD bcbput(ix)
/* Write buffer ix back if it is dirty.  Returns 0 on success */
REG WORD ix;
{
    if (bval[ix] && bdrt[ix])
    {
	if (wdsec(WDWRITE, bblk[ix], bcbpa(ix)) != 0) return(1);
	bdrt[ix] = 0;
    }
    return(0);
}


WORD bcbflush()
/* Write every dirty buffer back.  Returns 0 if all of them made it */
{
    REG WORD i;
    REG WORD rtn;

    plock();
    rtn = 0;
    for (i = 0; i < NBCB; i++)
	if (bcbput(i)) rtn = 1;
    punlock();
    return(rtn);
}


bcbmark(ix)
/* Mark buffer ix as differing from the disk */
REG WORD ix;
{
    bdrt[ix] = 1;
}


MLOCAL bcbmru(bse, pos)
/* Move the buffer at lru[bse+pos] to the front of its set's order */
REG WORD bse;
REG WORD pos;
{
    REG WORD i;
    REG UBYTE ix;

    if (pos == 0) return;
    ix = lru[bse+pos];
    for (i = pos; i > 0; i--)
	lru[bse+i] = lru[bse+i-1];
    lru[bse] = ix;
}


WORD bcbfind(blk, fill)
/* Return the index of the buffer holding block blk, reading it in if it is
   not already resident.  fill = 0 means the caller is about to overwrite
   the whole sector, so a fresh buffer is zeroed instead of read.
   Returns -1 if the block could not be read or a victim not written */

LONG	blk;			/* physical block number	*/
REG WORD fill;			/* read the block in?		*/

{
    REG WORD i;
    REG WORD ix;
    REG WORD bse;

    plock();
    if ( ! bcbup) bcbinit();
    bse = (WORD)((UWORD)blk & (NSET-1)) * NWAY;
    for (i = 0; i < NWAY; i++)
    {
	ix = lru[bse+i];
	if (bval[ix] && bblk[ix] == blk)
	{
	    bcbmru(bse, i);
	    punlock();
	    return(ix);
	}
    }
				/* not resident: take the set's LRU buffer */
    ix = lru[bse+NWAY-1];
    if (bcbput(ix))
    {
	punlock();
	return(-1);			/* victim would not write back	*/
    }
    bval[ix] = 0;
    if (fill)
    {
	if (wdsec(WDREAD, blk, bcbpa(ix)) != 0)
	{
	    punlock();
	    return(-1);
	}
    }
    else mem_clr(BCBADR(ix), (LONG)BCBLEN);
    bblk[ix] = blk;
    bval[ix] = 1;
    bdrt[ix] = (UBYTE)(fill ? 0 : 1);
    bcbmru(bse, NWAY-1);
    punlock();
    return(ix);
}
