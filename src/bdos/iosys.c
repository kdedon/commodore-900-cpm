/*
 * Portions Copyright (c) 2026 Kevin Dedon.
 */

/* Translate BDOS I/O packets into BIOS calls under the filesystem lock. */

#include "stdio.h"		/* Standard I/O declarations */

#include "bdosdef.h"		/* Type and structure declarations for BDOS */

#include "pktio.h"		/* Packet I/O definitions */

#include "biosdef.h"		/* Declarations for BIOS entry points */

EXTERN	udiv();			/* Assembly language unsigned divide routine */
				/* in bdosif.s.  It's used because Alcyon C  */
				/* can't do / or % without an external */

/************************
*  do_phio entry point	*
************************/

UWORD do_phio(iop)

REG struct iopb *iop;		/* iop is a pointer to a i/o parameter block */

{
    MLOCAL UBYTE last_dsk;  	/* static variable to tell which disk
				     was last used, to avoid disk selects */
    REG struct dph *hdrp;	  /* pointer to disk parameter header	*/
    REG struct dpb *dparmp;	  /* pointer to disk parameter block	*/
    REG UWORD	rtn;		  /* return parameter			*/
    UWORD	iosect;		  /* sector number returned from divide rtn */

    LOCK		/* lock the disk system while doing physical i/o */

    rtn = 0;
    switch (iop->iofcn)
    {
	case sel_info:	
		last_dsk = iop->devnum;
		iop->infop = bseldsk(last_dsk, iop->ioflags);
		break;

	case read:
	case write:
		if (last_dsk != iop->devnum)
		    bseldsk((last_dsk = iop->devnum), 0);
		    /* guaranteed disk is logged on, because temp_sel in
			BDOSMAIN does it	*/
		hdrp = iop->infop;
		dparmp = hdrp->dpbp;

		bsettrk( udiv( iop->devadr, dparmp->spt, (long)&iosect )
			 + dparmp->trk_off );
		bsetsec( bsectrn( iosect, hdrp->xlt ) );
		bsetdma(iop->xferadr);
		if ((iop->iofcn) == read) rtn = bread();
		else rtn = bwrite(iop->ioflags);
		break;

	case flush:
		rtn = bflush();
    }

    UNLOCK
    return(rtn);
}


/* Function 50 accepts disk, clock, page-allocation and device-control BIOS
 * calls listed below. Other codes return FFFFFFFFh. This is an interface
 * policy, not a protection boundary: SC 3 exposes the raw BIOS dispatcher. */

LONG bioscl(code, p1, p2)

REG WORD code;			/* BIOS function code from the block	*/
LONG	p1, p2;			/* its two LONG parameters		*/

{
    switch (code)
    {
	case 8:			/* HOME				*/
	case 9:			/* SELDSK			*/
	case 10:		/* SETTRK			*/
	case 11:		/* SETSEC			*/
	case 12:		/* SETDMA			*/
	case 13:		/* READ				*/
	case 14:		/* WRITE			*/
	case 16:		/* SECTRAN			*/
	case 21:		/* FLUSH			*/
	case 23:		/* TIME -- the only route to the chip */
	case 25:		/* SEGMENT -- likewise, for 64 KB	*/
	case 29:		/* CONCNT -- how many consoles exist	*/
	case 30:		/* CONRX -- ring or polled receive	*/
	case 24:		/* TICK -- see the table (N2)		*/
	case 28:		/* CONDEV -- see the table (N2)		*/
	case 31:		/* AUXIST -- see the table (N2)		*/
	case 32:		/* AUXDEV -- see the table (N2)		*/
		return( bios(code, p1, p2) );
    }
    return(0xffffffffL);	/* refused -- see the table above */
}
