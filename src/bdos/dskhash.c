

#include "stdio.h"		/* Standard I/O declarations */

#include "bdosdef.h"		/* Type and structure declarations for BDOS */

#include "c900cfg.h"		/* DHBASE, DHMAX */

#include "dskhash.h"

/* the dirscan functions whose result implies match() or an empty slot */
EXTERN BOOLEAN	matchit();	/* search first/next		*/
EXTERN BOOLEAN	openfile();	/* open, and extent open	*/
EXTERN BOOLEAN	close();	/* close			*/
EXTERN BOOLEAN	delete();	/* delete			*/
EXTERN BOOLEAN	rename();	/* rename			*/
EXTERN BOOLEAN	set_attr();	/* set file attributes		*/
EXTERN BOOLEAN	fsize();	/* file size			*/
EXTERN BOOLEAN	create();	/* create -- wants an empty slot */

EXTERN UWORD	log_dsk;	/* logged-on disk vector	*/

/* The three signature arrays, in the disk buffer segment (c900cfg.h) */
#define DHNAM	((UWORD *)DHBASE)
#define DHEXT	((UWORD *)(DHBASE + 2L * DHMAX))
#define DHUSR	((UBYTE *)(DHBASE + 4L * DHMAX))

MLOCAL WORD	dhok;		/* table describes drive dhdrv		*/
MLOCAL WORD	dhbld;		/* rebuild in progress			*/
MLOCAL UWORD	dhdrv;		/* drive the table describes		*/


UWORD dhname(p)
/* Hash the 11 name/type bytes.  The attribute bits live in bit 7 of each
   byte and match() ignores them, so they are masked out here too */

REG UBYTE *p;

{
    REG UWORD	h;
    REG WORD	i;

    h = 0;
    i = 11;
    do
    {
	h = (h << 4) ^ (h >> 12) ^ (UWORD)(UBWORD(*p++) & 0x7f);
	i -= 1;
    } while (i);
    return(h);
}


dhset(indx, dirp)
/* Record the signature of one directory entry */

REG UWORD indx;
REG struct dirent *dirp;

{
    if (indx >= DHMAX) return;
    DHUSR[indx] = dirp->entry;
    DHNAM[indx] = dhname(&(dirp->fname[0]));
    DHEXT[indx] = (UWORD)( (UBWORD(dirp->s2) << 8) | UBWORD(dirp->extent) );
}


dhrec(dirsec, dirp)
/* Record the four entries of directory record dirsec.  Every directory
   mutation reaches the disk through dir_wr(), which calls this, so the
   table cannot outlive the entry it describes */

REG UWORD dirsec;
REG struct dirent *dirp;

{
    REG WORD i;
    REG UWORD indx;
    BSETUP

    if ( ! (dhok || dhbld) ) return;
    if ( dhdrv != (UWORD)UBWORD(GBL.curdsk) ) return;
    indx = dirsec << 2;
    for (i = 0; i < 4; i++)
	dhset(indx + i, dirp + i);
}


dhopen(dsknum, drm)

UWORD	dsknum;
REG UWORD drm;

{
    BSETUP

    if ( (GBL.parmp)->cks )
    {			/* removable: signatures are not used at all */
	dhok = 0;
	dhbld = 0;
	return;
    }
    if (dhok && dhdrv == dsknum) return;	/* still valid */
    dhok = 0;
    dhdrv = dsknum;
}


dhadd(indx, dirp)
/* Record one entry's signature if a rebuild is in progress.  The drive
   login scan calls this for every directory entry */

UWORD	indx;
struct dirent *dirp;

{
    if (dhbld) dhset(indx, dirp);
}


dhdone()
/* The login scan has recorded every entry; the table is now usable */
{
    if (dhbld)
    {
	dhok = 1;
	dhbld = 0;
    }
}


WORD dhstart(q, funcp, fcbp)
/* Build the filter for one dirscan.  Returns q->mode: 0 = the scan gets no
   help and must look at every entry */

REG struct dhq	*q;
BOOLEAN		(*funcp)();
REG struct fcb	*fcbp;

{
    REG UBYTE	*p;
    REG WORD	i;
    WORD	chkext;
    BSETUP

    q->mode = 0;
    q->chk = 0;
    if ( ! dhok ) return(0);
    if ( dhdrv != (UWORD)UBWORD(GBL.curdsk) ) return(0);
    if ( ! (log_dsk & (1 << dhdrv)) ) return(0);
				/* a reset logs the drive off; the table is
				   only rebuilt when it is logged back in */
    if ( (GBL.parmp)->cks ) return(0);
				/* checksummed (removable) media detects a
				   media change by re-reading every record
				   as it scans: leave that scan alone */

    if (funcp == create)
    {				/* create wants the first free slot */
	q->mode = 2;
	return(2);
    }
    if (fcbp == (struct fcb *)NULL) return(0);

/* A name with a wildcard in it cannot be hashed, and the user and extent
   fields alone reject too few entries to pay for the test, so such a scan
   is left exactly as it was.  This is the first thing decided because it
   is the common case for DIR, whose scans are short and frequent.	*/
    p = &(fcbp->fname[0]);
    for (i = 0; i < 11; i++)
	if (UBWORD(p[i]) == '?') return(0);

    if (funcp == matchit || funcp == openfile || funcp == close)
	chkext = 1;		/* these match with the extent fields */
    else if (funcp == delete || funcp == rename
	     || funcp == set_attr || funcp == fsize)
	chkext = 0;		/* these match the name only */
    else return(0);		/* alloc, alltrue: every entry is wanted */

    q->nam = dhname(p);
    q->chk = DHC_NAM;
    if (UBWORD(fcbp->drvcode) != '?')
    {				/* fcb byte 0 is the user number */
	q->usr = UBWORD(fcbp->drvcode);
	q->chk |= DHC_USR;
    }
    if (chkext && (UBWORD(fcbp->extent) != '?'))
    {
	q->ext = (UWORD)( (UBWORD(fcbp->s2) << 8) | UBWORD(fcbp->extent) );
	q->exmask = (UWORD)((~UBWORD((GBL.parmp)->exm)) & 0xff);
	q->chk |= DHC_EXT;
    }
    q->mode = 1;
    return(1);
}


WORD dhcand(q, indx)
/* Could the entry at indx satisfy this scan?  A false answer is final --
   the entry is skipped without reading its directory record */

REG struct dhq *q;
REG UWORD indx;

{
    REG UWORD	d;

    if (q->mode == 2) return( UBWORD(DHUSR[indx]) == 0xe5 );

    if (DHNAM[indx] != q->nam) return(0);
				/* mode 1 always carries a name hash: it is
				   the selective test, so it goes first   */
    if ( (q->chk & DHC_USR)
	 && ( ((UWORD)UBWORD(DHUSR[indx]) ^ q->usr) & 0x7f ) ) return(0);
				/* match() compares byte 0 modulo bit 7 */
    if (q->chk & DHC_EXT)
    {
	d = DHEXT[indx] ^ q->ext;
	if (d & q->exmask) return(0);	/* extent, less the EXM bits	*/
	if (d & 0x3f00) return(0);	/* module field of s2		*/
    }
    return(1);
}
