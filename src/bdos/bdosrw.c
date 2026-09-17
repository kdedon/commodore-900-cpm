/*
 * Portions Copyright (c) 2026 Kevin Dedon.
 */

/* Sequential, random and multi-record file transfers. */

#include "stdio.h"		/* Standard I/O declarations */

#include "bdosdef.h"		/* Type and structure declarations for BDOS */


/* External function definitions */
EXTERN	UWORD	rdwrt();	/* disk read/write routine	*/
EXTERN	WORD	getaloc();	/* allocate a block of disk space */
EXTERN	UWORD	swap();		/* assembly language byte swapper */
EXTERN	UWORD	dirscan();	/* directory scanning routine	*/
EXTERN	BOOLEAN openfile();	/* open file function passed to dirscan */
EXTERN	UWORD	close_fi();	/* close file function		*/
EXTERN	BOOLEAN create();	/* create file function passed to dirscan */
EXTERN	UWORD	error();	/* error handler		*/

/* External variable definitions */
EXTERN	UWORD	ro_dsk;		/* read-only disk vector	*/


/**********************************************************/
/*  First, some utility functions used by seqio and ranio */
/**********************************************************/

/******************************
*  FCB block number routines  *
******************************/

WORD	blkindx(fcbp)
/*  return index into fcb disk map	*/

REG struct fcb	*fcbp;		/* pointer to fcb	*/
{
    REG struct dpb *dparmp;	/* pointer to disk parameter block */
    REG WORD i;
    REG WORD blkshf;
    BSETUP

    dparmp = GBL.parmp;
    blkshf = dparmp->bsh;
    i = ((fcbp->extent) & dparmp->exm) << (7 - blkshf);
    return (i + (UBWORD(fcbp->cur_rec) >> blkshf) );
}


UWORD	blknum(fcbp, index, wrdfcb)
/* return block number in fcb indicated by index */

REG struct fcb	*fcbp;		/* pointer to fcb	*/
REG WORD	index;		/* index into disk map of fcb */
WORD		wrdfcb;		/* boolean, fcb disk map of words? */
{
    if (wrdfcb)
	return( swap(fcbp->dskmap.big[index]) );
    else return( UBWORD(fcbp->dskmap.small[index]) );
}


setblk(fcbp, index, wrdfcb, block)
/* put block number into fcb */

REG struct fcb	*fcbp;		/* pointer to fcb	*/
REG WORD	index;		/* index into disk map of fcb */
WORD		wrdfcb;		/* boolean, fcb disk map of words? */
REG UWORD	block;		/* block number		*/
{
    fcbp->s2 &= 0x7f;		/* set file write flag	*/
    if (wrdfcb)
	fcbp->dskmap.big[index] = swap(block);
    else fcbp->dskmap.small[index] = (UBYTE)block;
}


/***************************
*  disk read/write routine *
***************************/

UWORD do_io(block, rcrd, parm)

UWORD	block;		/* block number		*/
UBYTE	rcrd;		/* record number	*/
REG WORD parm;		/* write parameter	*/
{
    REG LONG lsec;
    REG struct dpb *dparmp;
    BSETUP

    dparmp = GBL.parmp;		/* init dpb pointer */
    lsec = ((LONG)block << (dparmp->bsh)) +
		(LONG)(rcrd & (dparmp->blm));
    return( rdwrt(lsec, GBL.dmaadr, parm) );
}


/*******************************************
*  routine for crossing extent boundaries  *
*******************************************/

WORD new_ext(fcbp, reading, ran)
/*  If sequential I/O, open the next extent	*/
/*  If random I/O, compute new extent from random record field */

REG struct fcb	*fcbp;		/* pointer to fcb */
BOOLEAN reading;		/* read/write flag */
WORD  ran;			/* random I/O flag */
{
    REG UBYTE mod;		/* module number		*/
    REG UBYTE ext;		/* extent number		*/
    UBYTE old_mod;		/* s2 and extent to restore	*/
    UBYTE old_ext;		/* if the new one is not there	*/
    BSETUP

    if (ran)
    {
	mod = ( (fcbp->ran0) << 4) | ( (fcbp->ran1) >> 4);
	ext = ( ((fcbp->ran1) & 0x0f) << 1);
	if ((fcbp->ran2) & 0x80) ext |= 1;
		/* the calculation of ext was coded this way because of a */
		/* compiler bug from Alcyon */
    }
    else
    {
	mod = (fcbp->s2) & 0x3f;
	ext = (fcbp->extent) + 1;	/* for sequential, incr extent	*/
    }
    if (ext >= 32)
    {
	ext = 0;
	mod += 1;
    }
    if (mod >= 64) return(6);	/* past maximum file size */
    if ( mod == ((fcbp->s2) & 0x3f) )
	if ( ! ((ext ^ (fcbp->extent)) & ~((GBL.parmp)->exm) & 0x1f) )
	{			/* we're in same logical extent */
	    fcbp->extent = ext;
	    return(0);
	}
    /* Extent or Module numbers don't match	*/
    /* Close the old extent and open a new one	*/
    if ( close_fi(fcbp) >= 255 ) return(3);
			/* can't close old extent */
    old_mod = fcbp->s2;
    old_ext = fcbp->extent;
    fcbp->s2 = mod;
    fcbp->extent = ext;
    if ( dirscan(openfile, fcbp, 0) >= 255 )  /* open new extent */
    {
	if (reading) mod = 4;		/* reading unwritten extent */
	else if ( dirscan(create, fcbp, 8) >= 255 )
	    mod = 5;			/* can't create new extent */
	else return(0);
	/* C900: put back the extent and module the disk map still
	   belongs to, as v3's badseek does -- left on the new extent,
	   a following random write matched it as "same extent" and
	   wrote into the old extent's blocks with no entry of its own */
	fcbp->s2 = old_mod;
	fcbp->extent = old_ext;
	return(mod);
    }
    return(0);
}


/************************************
* Routine to calculate the maximum  *
* extent number of an FCB in a	    *
* extent-folded environment	    *
************************************/

UWORD calcext(fcbp)

REG struct fcb *fcbp;

{
    REG UWORD i;
    REG BYTE *p;
    BSETUP

    i = 15;
    p = &(fcbp->dskmap.small[16]);
    do
    {
	if (*--p) break;
	i -= 1;
    } while (i);
/* Now i contains the index of the last non-zero block in the FCB */
    if ((GBL.parmp)->dsm > 255) i >>= 1;
    i >>= 7 - ((GBL.parmp)->bsh);
    return ( (fcbp->extent) & ~((GBL.parmp)->exm) & 0x1f | i );
}


/*********************************
* Routine to get the actual	 *
* record count of the currently  *
* active logical extent of a FCB *
*********************************/

UWORD get_rc(fcbp)

REG struct fcb *fcbp;

{
    REG UWORD ext;

    ext = calcext(fcbp);	/* find last active extent in fcb */
    if (ext == fcbp->extent) return(UBWORD(fcbp->rcdcnt));
			/* if this is the last active fcb, return fcb's rc */
    else if (ext > fcbp->extent) return(128);
			/* if the fcb has more extents past this one, then */
			/* the current one is logically full	*/
    else return (0);
			/* if we seeked past the last active extent, rc = 0 */
}


/************************
*  bdosrw entry point	*
************************/

UWORD bdosrw(fcbp, reading, random)

REG struct fcb *fcbp;		/* fcbp is a pointer to a fcb */
REG BOOLEAN reading;		/* boolean to tell whether to read or write */
WORD random;			/* 0 = sequential, 1 = random (normal),  */
				/* 2 = random with zero fill	*/
{
    REG UWORD	block;		/* block number from fcb	*/
    REG WORD	index;		/* index into disk map of fcb	*/
    REG XADDR	old_dma;	/* temp holding spot for dmaadr */
    REG UBYTE	*buf;		/* type compatibility kludge	*/
    REG WORD	parm;		/* parameter to do-io		*/
    REG WORD	bigfile;	/* file system is in word mode	*/
    REG UWORD	rtn;		/* return parameter		*/
    REG UBYTE	rc;		/* temp storage for rcdcnt	*/
    BSETUP

    bigfile = ((GBL.parmp)->dsm) & ~0xff;
    if ( ( ! reading) && (fcbp->ftype[robit] & 0x80) )
    {				/* check for read-only file */
	error(5,fcbp);
	if (GBL.errcode) return(0xff);
		/* the program asked for its own errors (fcn 45): report the
		   read-only file rather than write to it.  In the default
		   mode error() either aborted or cleared the attribute, so
		   falling through stays right. */
    }
    if (random)
    {
	if ( rtn = new_ext(fcbp, reading, TRUE) ) return(rtn);
			/* open new extent if necessary, return if error */
	fcbp->cur_rec = (fcbp->ran2) & 0x7f;
    }
    else		/* sequential */
	if (UBWORD(fcbp->cur_rec) == 128)
	{				/* time to try next extent */
			/* C900: UBWORD added -- UBYTE is (signed) char in this
			   dialect, so the bare compare was -128 == 128, never
			   true: sequential I/O could not cross a 16K extent
			   boundary (any file > 16K hit EOF at record 128) */
	    if ( new_ext(fcbp, reading, FALSE) )
		return(1);	/* if can't open new extent, error */
	    fcbp->cur_rec = 0;	/* opened new extent, zero cur_rec */
	}

    /* record is now in active fcb */
    rc = fcbp->rcdcnt;
    if ( UBWORD(fcbp->cur_rec) >= get_rc(fcbp) )
    {
	if (reading) return(1);	 /* reading unwritten data */
	fcbp->s2 &= 0x7f;	/* set file write flag */
	rc = fcbp->cur_rec + 1;
    }
    index = blkindx(fcbp);		/* get index into fcb disk map */
    block = blknum(fcbp, index, bigfile);
    if (block) parm = (reading ? 0 : 1);
    else		/* if allocated block, parm is just read or write */
    {			/* unallocated block	*/
	if (reading) return(1);		/* reading unwritten data */
	/* Writing to new block	*/
	block = getaloc();
	if (block == (UWORD)~0) return(2);	/* out of space	*/
			/* C900: was (UWORD *)~0 -- a 16-bit-pointer pun;
			   pointers are 32 bits here, block is a UWORD */
	setblk(fcbp, index, bigfile, block);
	parm = 3;
	if (random == 2)
	{		/* Write random with zero fill	*/
	    old_dma = GBL.dmaadr;
	    GBL.dmaadr = map_adr((XADDR)GBL.dirbufp,0);/* Do DMA from dir_buf */
	    index = SECLEN;
	    buf = (UBYTE *)GBL.dirbufp;	/* fix type incompatibility */
	    do buf[--index] = 0;
		while (index);		/* zero the dma buffer */
	    for (index = 0; index <= ((GBL.parmp)->blm); index++)
	    {
		do_io(block, index, parm);
				/* write zeros to the block
				   (avoid cc2 ICE with byte cast of word regvar) */
		parm = 1;	/* next write is not to new block */
	    }
	    GBL.dmaadr = old_dma;	/* restore dma address	*/
	}
    }
    rtn = do_io(block, fcbp->cur_rec, parm);
    if ( rtn == 0 )
    {
	fcbp->rcdcnt = rc;
	if ( ! random ) fcbp->cur_rec += 1;
    }
    return(rtn);
}


/****************************************
*  random record field of an fcb	*
*					*
*  CP/M-8000 stores it big-endian:	*
*  ran0 is the high byte (fcb+33),	*
*  ran2 the record within the extent.	*
****************************************/

incr_rr(fcbp)			/* random record field += 1 */

REG struct fcb *fcbp;
{
    if ( ++(fcbp->ran2) == 0 )
	if ( ++(fcbp->ran1) == 0 )
	    ++(fcbp->ran0);
}


/* Transfer multcnt records, restoring DMA and random-record fields on exit.
 * Nonphysical failures return completed records in the high byte; physical
 * errors reserve that byte for the extended error code. */

UWORD multio(fcbp, reading, random)

REG struct fcb *fcbp;		/* fcbp is a pointer to a fcb		*/
BOOLEAN reading;		/* read or write			*/
WORD random;			/* 0 = sequential, 1 = random,		*/
				/* 2 = random with zero fill		*/
{
    REG UWORD	count;		/* records this call must transfer	*/
    REG UWORD	done;		/* records transferred so far		*/
    REG UWORD	rtn;
    XADDR	sav_dma;	/* caller's DMA address			*/
    UBYTE	sav_r0, sav_r1, sav_r2;	/* caller's random record	*/
    BSETUP

    count = UBWORD(GBL.multcnt);
    if (count <= 1) return( bdosrw(fcbp, reading, random) );

    sav_dma = GBL.dmaadr;
    sav_r0 = fcbp->ran0;
    sav_r1 = fcbp->ran1;
    sav_r2 = fcbp->ran2;

    done = 0;
    while (done < count)
    {
	if ( rtn = bdosrw(fcbp, reading, random) ) break;
	done += 1;
	if (random) incr_rr(fcbp);
	GBL.dmaadr += SECLEN;
    }

    GBL.dmaadr = sav_dma;
    if (random)
    {
	fcbp->ran0 = sav_r0;
	fcbp->ran1 = sav_r1;
	fcbp->ran2 = sav_r2;
    }
    if (rtn == 0) return(0);
    if ( (rtn & 0xff) == 0xff ) return(rtn);
    return( (done << 8) | (rtn & 0xff) );
}
