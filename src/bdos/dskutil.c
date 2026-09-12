
/* Directory record I/O, scanning, checksums and allocation-vector helpers. */

#include "stdio.h"		/* Standard I/O declarations */

#include "bdosdef.h"		/* Type and structure declarations for BDOS */

#include "pktio.h"		/* Packet I/O definitions */

#include "biosdef.h"		/* Bios & mem mapperinterface */

#include "dskhash.h"		/* directory signature table */


/* declare external functions and variables */
EXTERN UWORD	do_phio();	/* external physical disk I/O routine */
EXTERN UWORD	error();	/* external error routine	*/

EXTERN UWORD	log_dsk;	/* logged-on disk vector */
EXTERN UWORD	ro_dsk;		/* read-only disk vector */
EXTERN UWORD	crit_dsk;	/* critical disk vector */


/* dirsecn describes the per-process directory buffer; -1 means unknown.
 * Foreign transfers invalidate it because zero-fill borrows this buffer.
 * dirown distinguishes directory transfers and travels with process state. */


/*	THE SHARED DIRECTORY GENERATION, one per drive.
 *
 * pdirbuf and dirsecn are per-process (they are inside struct stvars, which
 * proc.c copies at every switch), so a cached directory record describes
 * only what THIS process last read.  Nothing used to tell it that another
 * process had since changed that record, and on a drive with cks == 0 --
 * which is every C900 drive (src/bios/bios900.c) -- dirget() below would
 * then hand the stale copy out and close() would write all 128 bytes of it
 * back, erasing the peer's new entry.
 *
 * dirwgen[drive] moves on at every dir_wr() by anybody; each process
 * remembers, in GBL.dirgen, the value its own buffer was filled at.  A
 * cached record is usable only while the two agree.  That is the whole
 * guard, and it deliberately does NOT serialise anything: two processes
 * still run in the file system at once, they simply cannot hand each other
 * a record that has moved underneath them.  (The alternative -- having a
 * writer walk every process descriptor and clear its peers' dirsecn --
 * needs the process table from inside dskutil.c and a rule about a peer
 * that is mid-scan; one counter says the same thing and cannot miss one.)
 *
 * Per drive rather than one global counter so that traffic on B: does not
 * throw away everybody's cached A: record.  dirsecn is only ever valid for
 * the currently selected drive: seldsk() calls dirdrop() when the drive
 * changes, so indexing by curdsk is right.
 */
MLOCAL	UWORD	dirwgen[16];		/* bumped by dir_wr, read by all */

#define DIRGEN	dirwgen[UBWORD(GBL.curdsk) & 15]
				/* curdsk is poisoned to 0xff by a failed
				   select (fileio.c), so mask it	*/


GLOBAL WORD dirhave(dirsec)
/* Does this process's directory buffer still hold record `dirsec' as the
   disk now has it?  Both halves matter: the right record, and no directory
   write by anybody since it was read. */

REG UWORD dirsec;
{
    BSETUP

    return( GBL.dirsecn == (WORD)dirsec && GBL.dirgen == DIRGEN );
}


dirdrop()
/* Forget what the directory buffer holds */
{
    BSETUP

    GBL.dirsecn = -1;
}


/**********************
* read/write routine  *
**********************/

UWORD rdwrt(secnum, dma, parm)
/* General disk sector read/write routine */
/* It simply sets up a I/O packet and sends it to do_phio */

LONG	secnum;			/* logical sector number to read/write */
XADDR	dma;			/* dma address				*/
REG WORD parm;			/* 0 for read, write parm + 1 for write */

{
    struct iopb	rwpkt;
    BSETUP

    if ( ! GBL.dirown ) GBL.dirsecn = -1;	/* foreign dma: the directory
					   buffer may be the target */
    rwpkt.devnum = GBL.curdsk;		/* disk to read/write	*/
    if (parm)
    {
	rwpkt.iofcn = (BYTE)write; /* if parm non-zero, we're doing a write */
	rwpkt.ioflags = (BYTE)(parm-1);	/* pass write parm	*/
        if ( ro_dsk & (1 << (rwpkt.devnum)) )
	    if ( error(4) ) return(1);
				/* don't write on read-only disk -- and
				   THAT MEANS DO NOT WRITE.  error(4) only
				   returns at all in the mode where fn 45
				   asked for errors instead of an abort;
				   discarding its answer here let the write
				   go ahead after the BDOS had already told
				   the program the disk was read-only, and
				   let delete() free a live file's blocks */
    }
    else
    {
	rwpkt.iofcn = (BYTE)read;
	rwpkt.ioflags = (BYTE)0;
    }
    rwpkt.devadr = secnum;			/* sector number	*/
    rwpkt.xferadr = dma;			/* dma address		*/

/*		parameters that are currently not used by do_phio
    rwpkt.devtype = disk;
    rwpkt.xferlen = 1;
				*/
    rwpkt.infop = GBL.dphp;			/* pass ptr to dph	*/
    while ( do_phio(&rwpkt) )
	if ( error( parm ? 1 : 0 ) ) return(1);
		/* THE TRANSFER DID NOT HAPPEN, so say so.  error() returns
		   non-zero in exactly two cases and neither of them is a
		   completed transfer: fn 45's return-error mode, and the
		   default mode's operator answering `C' (continue with bad
		   data).  Returning 0 here made a refused directory read
		   into a published cache entry and let a refused directory
		   write be followed by clraloc() on a live file's blocks
		   (dir_rd, dir_wr below; delete(), truncit() in fileio.c).
		   bdosrw.c already tests do_io()'s result, so the data path
		   reports the error to the program rather than claiming the
		   record was written. */
    return(0);
}


/***************************
*  directory read routine  *
***************************/

UWORD dir_rd(secnum)

UWORD secnum;
{
    REG UWORD rtn;
    BSETUP

    GBL.dirown = 1;
    GBL.dirgen = DIRGEN;	/* sample BEFORE the transfer: rdwrt() can
				   yield (do_phio takes the file-system lock,
				   error() reaches a console read), and a
				   write that lands while we are reading must
				   leave us stale rather than current	*/
    rtn = rdwrt((LONG)secnum, map_adr((XADDR)GBL.dirbufp, 0), 0);
    GBL.dirown = 0;
    GBL.dirsecn = rtn ? -1 : (WORD)secnum;
    return(rtn);
}


/****************************
*  directory write routine  *
****************************/

UWORD dir_wr(secnum)

REG WORD secnum;
{
    REG UWORD rtn;
    UBYTE dchksum();
    BSETUP

    GBL.dirown = 1;
    rtn = rdwrt( (LONG)secnum, map_adr((XADDR)GBL.dirbufp, 0), 2);
    GBL.dirown = 0;
    DIRGEN += 1;	/* every other process's cached record of this drive
			   is now suspect, and this is the only place that
			   can tell them so.  Bumped even on failure: a
			   refused write may still have reached part of the
			   medium, and being conservative costs one re-read */
    if (rtn)
    {		/* the record never reached the disk: publish NOTHING.  A
		   returned-zero here used to leave dirsecn describing a
		   buffer the medium does not have, and the checksum vector
		   and the signature table describing it too	*/
	GBL.dirsecn = -1;
	return(rtn);
    }
    GBL.dirsecn = (WORD)secnum;
    GBL.dirgen = DIRGEN;	/* our own buffer IS what the disk now holds */
    if ( secnum < (GBL.parmp)->cks )
	*((GBL.dphp)->csv + secnum) = dchksum();
    dhrec((UWORD)secnum, GBL.dirbufp);
			/* every directory mutation lands here, so this is
			   the one place the signature table is refreshed */
    return(rtn);
}


/*******************************
*  directory checksum routine  *
*******************************/

UBYTE dchksum()
/* Compute checksum over one directory sector */
/* Note that this implementation is dependant on the representation */
/*   of a LONG and is therefore not very portable.  But it's fast   */
{
    REG LONG	*p;		/* local temp variables */
    REG LONG	lsum;
    REG WORD	i;

    BSETUP

    p = (LONG *)GBL.dirbufp;	/* point to directory buffer */
    lsum = 0;
    i = SECLEN / (sizeof lsum);
    do
    {
	lsum += *p++;		/* add next 4 bytes of directory */
	i -= 1;
    } while (i);
    lsum += (lsum >> 16);
    lsum += (lsum >> 8);
    return( (UBYTE)(lsum & 0xff) );
}


/************************
*  dirscan entry point	*
************************/

UWORD dirscan(funcp, fcbp, parms)

BOOLEAN (*funcp)();		/* funcp is a pointer to a Boolean function */
REG struct fcb *fcbp;		/* fcbp is a pointer to a fcb */
REG UWORD parms;		/* parms is 16 bit set of bit parameters */

/* Parms & 1  = 0 to start at beginning of dir, 1 to continue from last */
/* Parms & 2  = 0 to stop when *funcp is true, 1 to go until end	*/
/* Parms & 4  = 0 to check the dir checksum, 1 to store new checksum	*/
/* Parms & 8  = 0 to stop at hiwater, 1 to go until end of directory	*/

#define continu 1
#define full	 2
#define initckv  4
#define pasthw   8

{
    REG UWORD 	i;		/* loop counter		*/
    REG struct dpb *dparmp;	/* pointer to disk parm block */
    REG UWORD  	rtn;		/* return value		*/
    struct dhq	q;		/* this scan's signature filter	*/

    BSETUP

    dparmp = GBL.parmp;			/* init ptr to dpb */
    rtn  = 255;				/* assume it doesn't work */
    q.mode = 0;
    if ( fcbp != (struct fcb *)NULL && UBWORD(fcbp->fname[0]) != '?' )
	dhstart(&q, funcp, fcbp);	/* can the signature table skip
					   entries for this scan?	*/
			/* a name beginning with a wildcard never can, and
			   that is most of what DIR asks for: deciding it
			   here keeps those scans off the call entirely  */

/* Sorry about this FOR loop, but the initialization terms and end test
    really do depend on the input parameters, so......			*/
    for ( i = ( (parms & continu) ? GBL.srchpos + 1 : 0);
	  i <= ( (parms & pasthw) ? (dparmp->drm) : (GBL.dphp)->hiwater );
	  i++ )
    {				/* main directory scanning loop		*/
	GBL.srchpos = i;
	if ( q.mode && ! dhcand(&q, i) ) continue;
			/* the signature says this entry cannot satisfy
			   *funcp, so it costs nothing to pass over	*/
	if ( ! (i & 3) || ! dirhave((UWORD)(i >> 2)) )
	    if ( dirget(i, parms) ) continue;
			/* the record could not be READ, so no entry in it
			   may be examined: what is in the buffer is the
			   previous record, and it used to be offered to
			   *funcp as this one (dir_rd published it as cache
			   as well -- see rdwrt above) */
			/* the record the entry lives in has to be in the
			   buffer; an unskipped scan reaches dirget exactly
			   where the original read, at a record boundary.
			   dirhave(), not a bare dirsecn compare: the
			   signature table above can skip an exact-name scan
			   STRAIGHT to its entry, so this test is the only
			   thing between such a scan and a record another
			   process has since rewritten	*/
	if ( (*funcp)(fcbp, (GBL.dirbufp) + (i&3), i) )
			/* call function with parms of (1) fcb ptr,
			   (2) pointer to directory entry, and
		   	   (3) directory index		  	*/
	{
	    if (parms & full) rtn = 0;	/* found a match, but keep going */
	    else return(i & 3);		/* return directory code	*/
	}
    }
    return(rtn);
}


/****************************************
*  read one directory record into the	*
*  directory buffer, and check it	*
****************************************/

WORD dirget(i, parms)
/* Returns non-zero when the record could NOT be read, in which case the
   buffer does not describe it and no entry in it may be looked at. */

REG UWORD i;			/* directory entry index	*/
REG UWORD parms;		/* dirscan's parameter bits	*/

{
    REG struct dpb *dparmp;	/* pointer to disk parm block	*/
    REG UWORD	dirsec;		/* directory record number	*/
    REG UBYTE	*p;		/* scratch pointer		*/
    REG UWORD	bitvec;		/* disk nmbr represented as a vector */
    REG UWORD	rtn;		/* dir_rd's verdict		*/

    BSETUP

    dirsec = i >> 2;
retry:
    dparmp = GBL.parmp;
    if ( dirhave(dirsec) )
    {
	if ( ! (dparmp->cks) ) return(0);
			/* nothing checksums on this drive, so a re-read
			   could only reproduce what is already there --
			   which is true only because dirhave() has already
			   established that no process has written the
			   record since this buffer was filled	*/
	if (i & 3) return(0);
			/* mid-record: the original scan re-read only at a
			   record boundary, and so does this one	*/
    }
    if ( rtn = dir_rd(dirsec) ) return(rtn);
			/* read the directory sector -- and if the medium
			   refused, say so rather than leaving the caller with
			   the last record it read		*/
    if ( dirsec < (dparmp->cks) )	/* checksumming on this sector? */
    {
	p = ((GBL.dphp)->csv) + dirsec;
				/* point to checksum vector byte  */
	if (parms & initckv) *p = dchksum();
	else if (*p != dchksum())
	{			/* checksum error! */
	    (GBL.dphp)->hiwater = dparmp->drm;  /* reset hi water */
	    bitvec = 1 << (GBL.curdsk);
	    if (crit_dsk & bitvec)	/* if disk in critical mode */
		ro_dsk |= bitvec;	/* then set it to r/o	*/
	    else
	    {
		log_dsk &= ~bitvec;	/* else log it off  */
		seldsk(GBL.curdsk);	/* and re-select it */
		goto retry;		/* and re-do current op */
	    }
	}
    }
    return(0);
}


/****************************************
*  Routines to manage allocation vector *
*	setaloc()			*
*	clraloc()			*
*	getaloc()			*
****************************************/

/*	THE BOUND IS HERE, not in the callers.
 *
 * Most of the block numbers these two are handed come out of a DIRECTORY
 * ENTRY -- alloc()'s login scan, close()'s merge, delete(), truncit() -- and
 * a directory entry is data off a medium, not a value the BDOS computed.  An
 * unbounded bit number is not a harmless write into slack: every drive's alv
 * is carved, in drive order, out of one 1536-byte pool (src/bios/bios900.c
 * drvinit), so a block number one past A:'s dsm marks a block allocated in
 * B:'s LIVE allocation map, and a big one leaves the pool entirely.
 *
 * dsm is the last valid block number, so `>' is the test.  Silently, and on
 * purpose: the login scan meets these entries before anything can be
 * reported to anybody, and the alternative -- refusing to log the drive in
 * -- turns one bad entry into an unmountable disk.
 */

setaloc(bitnum)
/*  Set bit in allocation vector	*/
REG UWORD	bitnum;
{
    BSETUP

    if ( bitnum > (GBL.parmp)->dsm ) return;
    *((GBL.dphp)->alv + (bitnum>>3)) |= 0x80 >> (bitnum & 7);
}


clraloc(bitnum)
/* Clear bit in allocation vector	*/
REG UWORD	bitnum;
{
    BSETUP

    if ( bitnum && bitnum <= (GBL.parmp)->dsm )
	*((GBL.dphp)->alv + (bitnum>>3)) &= ~(0x80 >> (bitnum & 7));
}


UWORD	getaloc()
/* Get a free block in the file system and set the bit in allocation vector */
{
    REG UWORD	i;		/* loop counter		*/
    REG WORD	diskmax;	/* # bits in alv - 1	*/
    REG UBYTE	*p;		/* ptr to byte */

    BSETUP
    LOCK		/* need to lock the file system while messing
			   with the allocation vector		*/

    diskmax = (GBL.parmp)->dsm;
			/* get disk max field from dpb	*/
    p = (GBL.dphp)->alv;
    for (i = 0; i <= diskmax; i++)
    {
	if ( ~(*(p + (i >> 3))) & (0x80 >> (i&7)) )
	{			/* found a zero in allocation vector */
	    setaloc(i);
	    UNLOCK		/* can unlock file system now	*/
	    return(i);		/* return block number		*/
	}
    }
    UNLOCK		/* THE DISK IS FULL, AND THIS RELEASE WAS MISSING.
			   Harmless while LOCK/UNLOCK were null macros
			   (bdosdef.h) and a held-forever lock under a real
			   one: every caller of getaloc() on a full disk
			   would have leaked the file system.	*/
    return(~0);			/* if no free block found, return -1 */
}
