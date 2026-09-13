

#include "stdio.h"		/* Standard I/O declarations */

#include "bdosdef.h"		/* Type and structure declarations for BDOS */

#include "biosdef.h"		/* and BIOS */

#include "pktio.h"		/* Packet I/O definitions */

#include "dskhash.h"		/* directory signature table */

/* declare external fucntions */
EXTERN UWORD 	dirscan();	/* directory scanning routine	*/
EXTERN		dirdrop();	/* forget the directory buffer's record */
EXTERN UWORD	error();	/* disk error routine		*/
EXTERN		seterr();	/* record a CP/M 3 error code for the caller */
EXTERN UWORD	do_phio();	/* packet disk i/o handler	*/
EXTERN		clraloc();	/* clear bit in allocation vector */
EXTERN		setaloc();	/* set bit in allocation vector */
EXTERN UWORD	swap();		/* assembly language byte swapper */
EXTERN UWORD	dir_wr();	/* directory write routine */
EXTERN		tmp_sel();	/* temporary select disk routine */
EXTERN UWORD	calcext();	/* calc max extent allocated for fcb */
EXTERN UWORD	udiv();		/* unsigned divide routine	*/


EXTERN UBYTE	*scbstampa();	/* scb.c: address of the SCB @DATE group */


/* declare external variables */
EXTERN UWORD	log_dsk;	/* logged-on disk vector	*/
EXTERN UWORD	ro_dsk;		/* read-only disk vector	*/
EXTERN UWORD	crit_dsk;	/* vector of disks in critical state	*/


/*  Per-drive date-stamping state, rebuilt by the login scan (alloc(),
    above) exactly as v3 rebuilds drvlbl in initial2.  drvlbl is the
    directory label's mode byte, 0 when the drive carries no label, and
    is the only thing that decides whether a stamp is written.  drvsfcb
    records that the drive has SFCBs at all, which function 100 needs
    before it may switch stamping on.				*/

MLOCAL UBYTE	drvlbl[16];
MLOCAL UBYTE	drvsfcb[16];


/************************************
*  This function passed to dirscan  *
*	from seldsk (below)	    *
************************************/

BOOLEAN alloc(fcbp, dirp, dirindx)	/* ARGSUSED */
/* Set up allocation vector for directory entry pointed to by dirp */

struct fcb	*fcbp;		/* not used in this function	*/
REG struct dirent *dirp;	/* pointer to directory entry	*/
WORD		dirindx;	/* index into directory for *dirp */
{
    REG WORD	i;		/* loop counter	*/
    BSETUP

    dhadd((UWORD)dirindx, dirp);
			/* the login scan visits every directory entry, so
			   it is also where the signature table is built */

    /*	The high water mark bounds every later directory scan, so an entry
	that does not raise it is invisible to the whole system.  v3's login
	scan (ref/cpm3/bdos30.asm:1296-1325) decides this per type: 21h and
	E5h jump straight back to initial2 without setcdr, a 20h label goes
	through drv$lbl -- which records its mode byte -- into initial3, and
	10h..1Fh reach initial3 directly.  Only entries below 10h also get
	their disk map scanned.  Following that exactly is what makes a
	directory label placed past the last file entry reachable at all.  */

    if ( UBWORD(dirp->entry) == DE_LABEL )
    {
	drvlbl[GBL.curdsk] = dirp->extent;	/* drv$lbl */
	(GBL.dphp)->hiwater = dirindx;
    }
    else if ( UBWORD(dirp->entry) == DE_SFCB )
	drvsfcb[GBL.curdsk] = 1;	/* the drive is stamped: fn 100 may
					   enable stamping on it	*/
    else if ( UBWORD(dirp->entry) < DE_SFCB )
    {
	(GBL.dphp)->hiwater = dirindx;	/* set up high water mark for disk */
	if ( UBWORD(dirp->entry) < DE_XFCB )
	{				/* a file FCB, and only a file FCB,
					   owns the blocks in its disk map */
	    i = 0;
	    if ((GBL.parmp)->dsm < 256)
	    {
		do setaloc( UBWORD(dirp->dskmap.small[i++]) );
		    while (i <= 15);
	    }
	    else
	    {
		do setaloc(swap(dirp->dskmap.big[i++]));
		    while (i <= 7);
	    }
	}
    }
    return(FALSE);
}


/************************
*  seldsk entry point	*
************************/

seldsk(dsknum)

REG UBYTE dsknum;		/* disk number to select */

{
    struct iopb selpkt;
    REG WORD	i;
    UWORD	j;
    REG UBYTE	logflag;
    BSETUP

    logflag = ~(log_dsk >> dsknum) & 1;
    if ((GBL.curdsk != dsknum) || logflag)
    {				/* if not last used disk or not logged on */
	selpkt.iofcn = sel_info;
	GBL.curdsk = (selpkt.devnum = dsknum);
	if (UBWORD(dsknum) > 15) error(2);
	selpkt.ioflags = logflag ^ 1;
	do
	{
	    do_phio(&selpkt);	/* actually do the disk select	*/
	    if ( (GBL.dphp = (struct dph *)selpkt.infop) != NULL ) break;
	} while ( ! error(3) );

	if (GBL.dphp == NULL)
	    GBL.curdsk = 0xff;
	    return;
	}
			/* set up GBL copies of dir_buf and dpb ptrs */
	GBL.parmp = (GBL.dphp)->dpbp;
	dirdrop();	/* the directory buffer now belongs to this disk */
    }
    if (logflag)
    {		/* if disk not previously logged on, do it now */
	LOCK	/* must lock the file system while messing with alloc vec */
	drvlbl[dsknum] = 0;	/* the login scan below rediscovers the	*/
	drvsfcb[dsknum] = 0;	/* label and the SFCBs of this medium	*/
	dhopen((UWORD)UBWORD(dsknum), (GBL.parmp)->drm);
	i = (GBL.parmp)->dsm;
	do clraloc(i); while (i--);	/* clear the allocation vector */
	i = udiv( (LONG)(((GBL.parmp)->drm) + 1), 
		  4 * (((GBL.parmp)->blm) + 1), &j);
					/* calculate nmbr of directory blks */
	if (j) i++;			/* round up */
	do setaloc(--i); while (i);	/* alloc directory blocks */
	dirscan(alloc, NULL, 0x0e);	/* do directory scan & alloc blocks */
	log_dsk |= 1 << dsknum;		/* mark disk as logged in	*/
	dhdone();			/* signatures complete: hashing on */
    }
}


/*******************************
*  General purpose byte mover  *
*******************************/

move(p1, p2, i)

REG BYTE *p1;
REG BYTE *p2;
REG WORD  i;
{
    while (i--)
	*p2++ = *p1++;
}


/*************************************
*  General purpose filename matcher  *
*************************************/

BOOLEAN match(p1, p2, chk_ext)

REG UBYTE *p1;
REG UBYTE *p2;
BOOLEAN  chk_ext;
{
    REG WORD	i;
    REG UBYTE temp;
    BSETUP

    i = 12;
    do
    {
	temp = (*p1 ^ '?');
	if ( ((*p1++ ^ *p2++) & 0x7f) && temp )
	    return(FALSE);
	i -= 1;
    } while (i);
    if (chk_ext)
    {
	if ( (*p1 != '?') && ((*p1 ^ *p2) & ~((GBL.parmp)->exm)) )
	    return(FALSE);
	p1 += 2;
	p2 += 2;
	if ((*p1 ^ *p2) & 0x3f) return(FALSE);
    }
    return(TRUE);
}



/************************************************
*  bdostime -- BIOS function 23 against @DATE	*
************************************************/

/*  The BIOS TOD block carries the date word HIGH byte first (native
    Z8001 order); the SCB group, the function-104/105 parameter block
    and the on-disk stamp all carry it LOW byte first (8080 order).
    The swap belongs here and nowhere else.	*/

UWORD bdostime(set)

REG WORD set;			/* 0 read the clock, 1 set it	*/
{
    UBYTE	tod[5];
    REG UBYTE	*s;
    REG WORD	i;

    s = scbstampa();
    if (set)
    {
	tod[0] = s[1];			/* date word, high byte first	*/
	tod[1] = s[0];
	for (i = 2; i < 5; i++) tod[i] = s[i];
	return( (UWORD)UBWORD(btime(tod, 1)) );
    }
    if ( btime(tod, 0) ) return(0xff);	/* no clock: leave @DATE alone	*/
    s[0] = tod[1];
    s[1] = tod[0];
    for (i = 2; i < 5; i++) s[i] = tod[i];
    return(0);
}


/****************************************
*  sfcbfld -- get$dtba			*
****************************************/

/*  Address of one sub-field of the SFCB describing the directory entry
    at dirindx, or NULL when there is none.  ref/cpm3/bdos30.asm:3314-
    3327: an entry that is itself the 4th item of its record has no
    sub-record, the 4th item must actually hold 21h, and the field is
    then buffa + 96 + 1 + 10*(dcnt & 3) + off.  The record is whatever
    the directory buffer holds, which under dirscan is always the
    record dirindx lives in.				*/

MLOCAL UBYTE *sfcbfld(dirindx, off)

REG WORD dirindx;
REG WORD off;			/* SF_CREATE, SF_UPDATE or SF_PWMODE */
{
    REG UBYTE *p;
    BSETUP

    if ( (dirindx & 3) == 3 ) return( (UBYTE *)NULL );
    p = (UBYTE *)(GBL.dirbufp) + SF_MARK;
    if ( UBWORD(*p) != DE_SFCB ) return( (UBYTE *)NULL );
    return( p + 1 + (dirindx & 3) * SF_SUBLEN + off );
}


/****************************************
*  stampfld -- stamp4			*
****************************************/


MLOCAL BOOLEAN stampfld(p)

REG UBYTE *p;			/* the field, or NULL for "no SFCB"	*/
{
    REG UBYTE *s;
    REG WORD  i;

    if ( p == (UBYTE *)NULL ) return(FALSE);
    bdostime(0);
    s = scbstampa();
    if ( UBWORD(s[0]) == 0xff && UBWORD(s[1]) == 0xff ) return(FALSE);
    for (i = 0; i < STAMPLEN; i++)
	if (p[i] != s[i]) break;
    if (i == STAMPLEN) return(FALSE);		/* already this minute	*/
    for (i = 0; i < STAMPLEN; i++) p[i] = s[i];
    return(TRUE);
}


/****************************************
*  qstamp1 / qdirfcb1			*
****************************************/

/*  Is the requested stamp switched on for the current drive?
    ref/cpm3/bdos30.asm:3331-3334: the label's mode byte is masked, and
    when the bit is on the drive is still checked read/write (qstamp1
    tail-calls nowrite).  A read-only drive is silently not stamped --
    it is not an error, and raising one would make opening a file on a
    write-protected disk fail.				*/

MLOCAL BOOLEAN stampon(mask)

REG UWORD mask;
{
    BSETUP

    if ( ! (UBWORD(drvlbl[GBL.curdsk]) & mask) ) return(FALSE);
    return( (ro_dsk & (1 << GBL.curdsk)) == 0 );
}


/*  Is this directory entry the file's FIRST one?  Only that entry is
    stamped (ref/cpm3/bdos30.asm:3336-3344 qdirfcb1): extent below the
    extent mask and module zero.			*/

MLOCAL BOOLEAN dirfcb1(dirp)

REG struct dirent *dirp;
{
    BSETUP

    if ( UBWORD(dirp->extent) & ~UBWORD((GBL.parmp)->exm) & 0x1f )
	return(FALSE);
    return( (UBWORD(dirp->s2) & 0x3f) == 0 );
}


/****************************************
*  update$stamp				*
****************************************/


MLOCAL BOOLEAN ustamp(fcbp, dirp, dirindx)

REG struct fcb *fcbp;
REG struct dirent *dirp;
REG WORD dirindx;
{
    if ( ! match(fcbp, dirp, TRUE) ) return(FALSE);
    if ( stampfld(sfcbfld(dirindx, SF_UPDATE)) ) dir_wr(dirindx >> 2);
    return(TRUE);
}


upd_stamp(fcbp)

REG struct fcb *fcbp;		/* the FCB function 21/34/40 is writing */
{
    REG UBYTE sext;
    REG UBYTE ss2;

    if ( ! stampon(DL_UPDATE) ) return;
    if ( UBWORD(fcbp->s2) & UPDSTAMPED ) return;
    sext = fcbp->extent;
    ss2  = fcbp->s2;
    fcbp->extent = 0;			/* zero$ext$mod, then	*/
    fcbp->s2 = 0;			/* search$namlen	*/
    dirscan(ustamp, fcbp, 0);
    fcbp->extent = sext;
    fcbp->s2 = (UBYTE)(UBWORD(ss2) | UPDSTAMPED);
}


/****************************************
*  function 102 -- read file stamps	*
****************************************/


MLOCAL BOOLEAN rdstamp(fcbp, dirp, dirindx)

REG struct fcb *fcbp;
REG struct dirent *dirp;
REG WORD dirindx;
{
    UBYTE	buf[8];
    REG UBYTE	*p;
    REG WORD	i;
    BSETUP

    if ( ! match(fcbp, dirp, TRUE) ) return(FALSE);
    for (i = 0; i < 8; i++) buf[i] = 0;
    fcbp->extent = 0;
    if ( (p = sfcbfld(dirindx, SF_CREATE)) != (UBYTE *)NULL )
    {
	for (i = 0; i < 8; i++) buf[i] = p[i];
	fcbp->extent = p[SF_PWMODE - SF_CREATE];
    }
    cpy_out(buf, GBL.dmaadr, 8L);
    return(TRUE);
}


UWORD rd_stamps(fcbp)

REG struct fcb *fcbp;
{
    REG UBYTE *p;
    REG WORD i;
    BSETUP

    p = &(fcbp->fname[0]);
    for (i = 0; i < 11; i++)		/* check$wild: no wildcards	*/
	if ( UBWORD(p[i]) == '?' )
	{
	    seterr(9, UBWORD(GBL.curdsk));	/* bdos30.asm:1769-1774	*/
	    return(0xff);
	}
    fcbp->extent = 0;
    fcbp->s2 = 0;
}


/****************************************
*  function 100 -- set directory label	*
****************************************/

MLOCAL BOOLEAN lblfind(fcbp, dirp, dirindx)	/* ARGSUSED */

struct fcb *fcbp;
REG struct dirent *dirp;
WORD dirindx;
{
    return( UBWORD(dirp->entry) == DE_LABEL );
}


MLOCAL BOOLEAN lblmake(fcbp, dirp, dirindx)	/* ARGSUSED */

struct fcb *fcbp;
REG struct dirent *dirp;
REG WORD dirindx;
{
    return( (dirindx & 3) != 3 && UBWORD(dirp->entry) == DE_EMPTY );
}



UWORD set_label(fcbp)

REG struct fcb *fcbp;
{
    REG UBYTE	*e;
    REG WORD	idx;
    REG WORD	i;
    REG BOOLEAN	made;
    BSETUP

    if ( (UBWORD(fcbp->extent) & DL_STAMPS) && ! drvsfcb[GBL.curdsk] )
	return(0xff);			/* nowhere to write a stamp	*/

    made = FALSE;
    if ( dirscan(lblfind, fcbp, 8) == 255 )
    {
	if ( dirscan(lblmake, fcbp, 8) == 255 ) return(0xff);
	made = TRUE;
    }
    idx = GBL.srchpos;
    e = (UBYTE *)(GBL.dirbufp) + ((idx & 3) << 5);

    if (made)
	for (i = 0; i < 32; i++) e[i] = 0;
    e[0] = DE_LABEL;
    move(&fcbp->fname[0], &e[1], 11);
    if (made) stampfld(&e[DL_CRSTAMP]);
    stampfld(&e[DL_UPSTAMP]);

    drvlbl[GBL.curdsk] = e[12];
    if ( (UWORD)idx > (GBL.dphp)->hiwater ) (GBL.dphp)->hiwater = idx;
    dir_wr(idx >> 2);
    crit_dsk |= 1 << (GBL.curdsk);
    return(0);
}


/****************************************
*  function 101 -- get label mode	*
****************************************/


UWORD get_label(dsknum)

REG UWORD dsknum;		/* drive 0..15, anything else = default */
{
    BSETUP

    if (dsknum > 15) dsknum = UBWORD(GBL.dfltdsk);
    seldsk((UBYTE)dsknum);
}


/************************
*  openfile entry point	*
************************/

BOOLEAN openfile(fcbp, dirp, dirindx)	/* ARGSUSED */

REG struct fcb *fcbp;		/* pointer to fcb for file to open */
struct dirent  *dirp;		/* pointer to directory entry	*/
WORD	dirindx;

{
    REG UBYTE fcb_ext;		/* extent field from fcb	*/
    REG BOOLEAN rtn;
    BSETUP

    if ( rtn = match(fcbp, dirp, TRUE) )
    {
	fcb_ext = fcbp->extent;	 /* save extent number from user's fcb */
	move(dirp, fcbp, sizeof *dirp);
				/* copy dir entry into user's fcb  */
	fcbp->extent = fcb_ext;
	fcbp->s2 |= 0x80;	 /* set hi bit of S2 (write flag)	*/
	crit_dsk |= 1 << (GBL.curdsk);
			/* access stamp: ref/cpm3/bdos30.asm:4117-4120,
			   `mvi c,0100$0000b' then qstamp/stamp1 -- the
			   access stamp shares the create field	*/
	if ( dirfcb1(dirp) && stampon(DL_ACCESS)
	     && stampfld(sfcbfld(dirindx, SF_CREATE)) )
	    dir_wr(dirindx >> 2);
    }
   return(rtn);
}


/*************************/
/* flush buffers routine */
/*************************/

UWORD flushit()
{
    REG UWORD	rtn;		/* return code from flush buffers call */
    struct iopb flushpkt;	/* I/O packet for flush buffers call */

    flushpkt.iofcn = flush;
    while ( rtn = do_phio(&flushpkt) )
	if ( error(1) ) break;
    return(rtn);
}


/*********************************
* file close routine for dirscan *
*********************************/

BOOLEAN close(fcbp, dirp, dirindx)

REG struct fcb *fcbp;		/* pointer to fcb */
REG struct dirent *dirp;	/* pointer to directory entry */
WORD	dirindx;		/* index into directory	*/

{
    REG WORD  i;
    REG UBYTE *fp;
    REG UBYTE *dp;
    REG UWORD fcb_ext;
    REG UWORD dir_ext;
    BSETUP

    if ( match(fcbp, dirp, TRUE) )
    {			/* Note that FCB merging is done here as a final
			   confirmation that disks haven't been swapped */
	LOCK
	fp = &(fcbp->dskmap.small[0]);
	dp = &(dirp->dskmap.small[0]);
	if ((GBL.parmp)->dsm < 256)
	{		/* Small disk map merge routine  */
	    i = 16;
	    do
	    {
		if (*dp)
		{
		    if (*fp)
		    {
			if (*dp != *fp) goto badmerge;
		    }
		    else *fp = *dp;
		}
		else *dp = *fp;
		fp += 1;
		dp += 1;
		i -= 1;
	    } while (i);
	}
	else
	{		/* Large disk map merge routine */
	    i = 8;
	    do
	    {
		if (*(UWORD *)dp)
		{
		    if (*(UWORD *)fp)
		    {
			if (*(UWORD *)dp != *(UWORD *)fp) goto badmerge;
		    }
		    else *(UWORD *)fp = *(UWORD *)dp;
		}
		else *(UWORD *)dp = *(UWORD *)fp;
		fp += sizeof (UWORD);
		dp += sizeof (UWORD);
		i -= 1;
	    } while (i);
	}
	/* Disk map merging complete */
	fcb_ext = calcext(fcbp);	/* calc max extent for fcb */
	dir_ext = (UWORD)(dirp->extent) & 0x1f;
	if ( (fcb_ext > dir_ext) || 
	    ((fcb_ext == dir_ext) && 
		(UBWORD(fcbp->rcdcnt) > UBWORD(dirp->rcdcnt))) )
			/* if fcb points to larger file than dirp */
	{
	    dirp->rcdcnt = fcbp->rcdcnt;	/* set up rc, ext from fcb */
	    dirp->extent = (BYTE)fcb_ext;
	}
	dirp->s1 = fcbp->s1;
	if ( (fcbp->ftype[robit]) & 0x80) error(5,fcbp);
						 /* read-only file error */
	dirp->ftype[arbit] &= 0x7f;		/* clear archive bit	    */
	dir_wr(dirindx >> 2);
	UNLOCK
	return(TRUE);

badmerge:
	UNLOCK
	ro_dsk |= (1 << GBL.curdsk);
	return(FALSE);
    }
    else return(FALSE);
}


/************************
*  close_fi entry point	*
************************/

UWORD close_fi(fcbp)

struct fcb *fcbp;		/* pointer to fcb for file to close */
{
    flushit();				/* first, flush the buffers	*/
    if ((fcbp->s2) & 0x80) return(0);	/* if file write flag not on,
					   don't need to do physical close */
    return( dirscan(close, fcbp, 0));	/* call dirscan with close function */
}


/************************
*  search entry point	*
************************/

/* First two functions for dirscan */

BOOLEAN alltrue(p1, p2, i)	/* ARGSUSED */
UBYTE	*p1;
UBYTE	*p2;
WORD	i;
{
    return(TRUE);
}
 
BOOLEAN matchit(p1, p2, i)	/* ARGSUSED */
UBYTE	*p1;
UBYTE	*p2;
WORD	i;
{
    return(match(p1, p2, TRUE));
}


/* search entry point */ 
UWORD search(fcbp, dsparm, p)	/* ARGSUSED */

REG struct fcb *fcbp;		/* pointer to fcb for file to search */
REG UWORD dsparm;		/* parameter to pass through to dirscan */
UBYTE	*p;			/* pointer to pass through to tmp_sel	*/
				/*     -- now unused --			*/

{
    REG UWORD	rtn;		/* return value */
    BSETUP

    if (fcbp->drvcode == '?')
    {
	rtn = dirscan(alltrue, fcbp, dsparm | 8);
    }
    else
    {
	if (fcbp->extent != '?') fcbp->extent = 0;
	fcbp->s2 = 0;
	rtn = dirscan(matchit, fcbp, dsparm);
    }
    cpy_out( GBL.dirbufp, GBL.dmaadr, SECLEN);
    return(rtn);
}



UWORD fexists(fcbp, off)

REG struct fcb *fcbp;		/* the caller's FCB		*/
REG WORD off;			/* 0 = first name, 16 = second	*/

{
    struct fcb	probe;
    REG UBYTE	*p;
    REG UBYTE	*q;
    REG WORD	i;
    BSETUP

    probe.drvcode = fcbp->drvcode;	/* copy$user$no (:1786-1788)	*/
    p = off ? (UBYTE *)&(fcbp->dskmap.small[1]) : &(fcbp->fname[0]);
    q = &(probe.fname[0]);
    i = 11;
    do *q++ = (UBYTE)(*p++ & 0x7f); while (--i);
			/* the attribute bits are not part of the name:
			   match() ignores them and so must the probe */
    probe.extent = 0;
    probe.s1 = 0;
    probe.s2 = 0;
    if ( dirscan(matchit, &probe, 0) == 255 ) return(FALSE);
    seterr(8, UBWORD(GBL.curdsk));	/* file$exists, bdos30.asm:4371	*/
    return(TRUE);
}


/************************
*  create entry point	*
************************/

BOOLEAN create(fcbp, dirp, dirindx)

REG struct fcb *fcbp;		/* pointer to fcb for file to create */
REG struct dirent *dirp;	/* pointer to directory entry	*/
REG WORD dirindx;		/* index into directory		*/

{
    REG BYTE *p;
    REG WORD i;
    REG BOOLEAN rtn;
    BSETUP

    if ( rtn = (UBWORD(dirp->entry) == 0xe5) )
    {
	p = &(fcbp->rcdcnt);
	i = 17;
	do
	{			/* clear fcb rcdcnt and disk map */
	    *p++ = 0;
	    i -= 1;
	} while (i);
	move(fcbp, dirp, sizeof *dirp);	/* move the fcb to the directory */
			/* ref/cpm3/bdos30.asm:4359-4368 make3a: the create
			   field is stamped when EITHER create or access
			   stamping is on (`mvi c,0101$0000b'), and the
			   update field as well when update stamping is on
			   -- after which set$filewf marks the FCB so the
			   file's first write does not stamp it again	*/
	if ( dirfcb1(dirp) )
	{
	    if ( stampon(DL_ACCESS|DL_CREATE) )
		stampfld(sfcbfld(dirindx, SF_CREATE));
	    if ( stampon(DL_UPDATE) )
	    {
		stampfld(sfcbfld(dirindx, SF_UPDATE));
		fcbp->s2 = (UBYTE)(UBWORD(fcbp->s2) | UPDSTAMPED);
	    }
	}
	dir_wr(dirindx >> 2);		/* write the directory sector */
	if ( dirindx > (GBL.dphp)->hiwater )
	    (GBL.dphp)->hiwater = dirindx;
	crit_dsk |= 1 << (GBL.curdsk);
    }
    return(rtn);
}


/************************
*  delete entry point	*
************************/

BOOLEAN delete(fcbp, dirp, dirindx)

REG struct fcb *fcbp;		/* pointer to fcb for file to delete */
REG struct dirent *dirp;	/* pointer to directory entry	*/
REG WORD dirindx;		/* index into directory		*/

{
    REG WORD i;
    REG BOOLEAN rtn;
    BSETUP

    if ( rtn = match(fcbp, dirp, FALSE) )
    {
	dirp->entry = 0xe5;
	LOCK
	{
	}
	UNLOCK
    }
    return(rtn);
}


/************************
*  rename entry point	*
************************/

BOOLEAN rename(fcbp, dirp, dirindx)

REG struct fcb *fcbp;		/* pointer to fcb for file to delete */
REG struct dirent *dirp;	/* pointer to directory entry	*/
REG WORD dirindx;		/* index into directory		*/

{
    REG UWORD i;
    REG BYTE *p;		/* general purpose pointers */
    REG BYTE *q;
    REG BOOLEAN rtn;
    BSETUP

    if ( rtn =  match(fcbp, dirp, FALSE) )
    {
	p = &(fcbp->dskmap.small[1]);
	q = &(dirp->fname[0]);
	i = 11;
	do
	{
	    *q++ = *p++ & 0x7f;
	    i -= 1;
	} while (i);
	dir_wr(dirindx >> 2);
    }
    return(rtn);
}


/************************
*  set_attr entry point	*
************************/

BOOLEAN set_attr(fcbp, dirp, dirindx)

REG struct fcb *fcbp;		/* pointer to fcb for file to delete */
REG struct dirent *dirp;	/* pointer to directory entry	*/
REG WORD dirindx;		/* index into directory		*/

{
    REG BOOLEAN rtn;
    BSETUP

    if ( rtn = match(fcbp, dirp, FALSE) )
    {
	move(&fcbp->fname[0], &dirp->fname[0], 11);
	dir_wr(dirindx >> 2);
    }
    return(rtn);
}


/****************************
*  utility routine used by  *
*  setran and getsize	    *
****************************/

LONG extsize(fcbp)
/* Return size of extent pointed to by fcbp */
REG struct fcb *fcbp;

{
    return( ((LONG)(fcbp->extent & 0x1f) << 7)
		| ((LONG)(fcbp->s2 & 0x3f) << 12) );
}


/************************
*  setran entry point	*
************************/

setran(fcbp)

REG struct fcb *fcbp;		/* pointer to fcb for file to set ran rec */

{
    LONG random;

    random = (LONG)UBWORD(fcbp->cur_rec) + extsize(fcbp);
				/* compute random record field	*/
			/* C900: the original picked bytes out of the LONG
			   with a member-less struct overlay (random.b2 etc),
			   a K&R offset trick our compiler rejects; shifts
			   are the same big-endian bytes */
    fcbp->ran0 = (UBYTE)(random >> 16);
    fcbp->ran1 = (UBYTE)(random >> 8);
    fcbp->ran2 = (UBYTE)random;
}


/**********************************/
/* fsize is a funtion for dirscan */
/* passed from getsize		  */
/**********************************/

BOOLEAN fsize(fcbp, dirp, dirindx)	/* ARGSUSED */

REG struct fcb *fcbp;		/* pointer to fcb for file to delete */
REG struct dirent *dirp;	/* pointer to directory entry	*/
WORD dirindx;			/* index into directory		*/

{
    REG BOOLEAN rtn;
    LONG temp;

    if ( rtn = match(fcbp, dirp, FALSE) )
    {
	temp = (LONG)UBWORD(dirp->rcdcnt) + extsize(dirp);
				/* compute file size	*/
			/* C900: byte overlay -> shifts (see setran) */
	fcbp->ran0 = (UBYTE)(temp >> 16);
	fcbp->ran1 = (UBYTE)(temp >> 8);
	fcbp->ran2 = (UBYTE)temp;
    }
    return(rtn);
}

/************************
*  getsize entry point	*
************************/

getsize(fcbp)
/* get file size	*/
REG struct fcb *fcbp;		/* pointer to fcb to get file size for */

{
    LONG maxrcd;
    LONG temp;
    REG WORD dsparm;

    maxrcd = 0;
    dsparm = 0;
    temp = 0;
    while ( dirscan(fsize, fcbp, dsparm) < 255 )
    {				/* loop until no more matches */
			/* C900: byte overlay -> shifts (see setran) */
	temp = ((LONG)UBWORD(fcbp->ran0) << 16)
	     | ((LONG)UBWORD(fcbp->ran1) << 8)
	     |  (LONG)UBWORD(fcbp->ran2);
	if (temp > maxrcd) maxrcd = temp;
	dsparm = 1;
    }
    fcbp->ran0 = (UBYTE)(maxrcd >> 16);
    fcbp->ran1 = (UBYTE)(maxrcd >> 8);
    fcbp->ran2 = (UBYTE)maxrcd;
}



MLOCAL UWORD	tr_ent;		/* index of the entry the cut falls in	*/
MLOCAL UWORD	tr_nblk;	/* disk map entries it keeps		*/
MLOCAL UBYTE	tr_ext;		/* its new extent byte			*/
MLOCAL UBYTE	tr_s2;		/* its new module byte			*/
MLOCAL UBYTE	tr_rc;		/* its new record count			*/
MLOCAL UWORD	tr_eoff;	/* logical extent within that entry	*/


/*  log2(exm+1): how many logical extents one directory entry holds.
    exm is always 2^k - 1, so this is just the population count.	*/

MLOCAL WORD tr_esh()
{
    REG WORD i;
    REG WORD n;
    BSETUP

    n = 0;
    for (i = UBWORD((GBL.parmp)->exm); i; i >>= 1) n++;
    return(n);
}


MLOCAL BOOLEAN truncit(fcbp, dirp, dirindx)

REG struct fcb *fcbp;
REG struct dirent *dirp;
REG WORD dirindx;
{
    REG WORD	i;
    REG WORD	nmap;
    REG UWORD	et;
    REG BOOLEAN	big;
    BSETUP

    if ( ! match(fcbp, dirp, FALSE) ) return(FALSE);

    big  = ((GBL.parmp)->dsm > 255);
    nmap = big ? 8 : 16;
    et = ( ((UBWORD(dirp->s2) & 0x3f) << 5) | (UBWORD(dirp->extent) & 0x1f) )
	 >> tr_esh();
    if (et < tr_ent) return(TRUE);	/* wholly below the cut: untouched */

    LOCK
    for ( i = (et == tr_ent) ? tr_nblk : 0; i < nmap; i++)
    {
	if (big)
	{
	    dirp->dskmap.big[i] = 0;
	}
	else
	{
	    dirp->dskmap.small[i] = 0;
	}
    }
    if (et == tr_ent)
    {
	REG UWORD off;

	for (i = nmap; i; i--)		/* get$dir$ext: scan backwards	*/
	    if ( big ? (dirp->dskmap.big[i-1] != 0)
		     : (UBWORD(dirp->dskmap.small[i-1]) != 0) ) break;
	off = i ? (UWORD)((i - 1) >> (7 - UBWORD((GBL.parmp)->bsh))) : 0;

	if (i == 0) dirp->rcdcnt = 0;		/* dminx = 0		*/
	else if (off != tr_eoff) dirp->rcdcnt = 0x80;	/* dir ext < fcb ext */
	else dirp->rcdcnt = tr_rc;
	dirp->extent = (UBYTE)( (UBWORD(tr_ext) & ~UBWORD((GBL.parmp)->exm))
				| off );
	dirp->s2 = tr_s2;
	dirp->ftype[arbit] &= 0x7f;	/* the file changed: reset archive */
    }
    else dirp->entry = DE_EMPTY;
    UNLOCK
    return(TRUE);
}


UWORD trunf(fcbp)

REG struct fcb *fcbp;
{
    LONG	rr;
    LONG	size;
    UBYTE	sav[3];
    REG UBYTE	*p;
    REG UWORD	lo;
    REG UWORD	n;
    REG UWORD	e;
    REG WORD	nsh;
    REG WORD	i;
    BSETUP

    p = &(fcbp->fname[0]);
    for (i = 0; i < 11; i++)		/* check$wild			*/
	if ( UBWORD(p[i]) == '?' )
	{
	    seterr(9, UBWORD(GBL.curdsk));	/* bdos30.asm:1769-1774	*/
	    return(0xff);
	}

    sav[0] = fcbp->ran0;
    sav[1] = fcbp->ran1;
    sav[2] = fcbp->ran2;
    rr = ((LONG)UBWORD(sav[0]) << 16) | ((LONG)UBWORD(sav[1]) << 8)
	 | (LONG)UBWORD(sav[2]);

    getsize(fcbp);			/* compute$rr, over every entry	*/
    size = ((LONG)UBWORD(fcbp->ran0) << 16) | ((LONG)UBWORD(fcbp->ran1) << 8)
	   | (LONG)UBWORD(fcbp->ran2);
    fcbp->ran0 = sav[0];
    fcbp->ran1 = sav[1];
    fcbp->ran2 = sav[2];
    if (size == 0L || rr >= size) return(0xff);

    /*	One directory entry holds (exm+1) logical extents of 128 records,
	so nsh (at most 11) splits the record number into the entry index
	and the record within it.  All word arithmetic: the record number
	never exceeds 18 bits, so its high byte contributes at most five
	bits to the entry index.				*/
    nsh = 7 + tr_esh();
    lo = ((UWORD)UBWORD(sav[1]) << 8) | (UWORD)UBWORD(sav[2]);
    tr_ent = ((UWORD)UBWORD(sav[0]) << (16 - nsh)) | (lo >> nsh);
    n = (lo & ((1 << nsh) - 1)) + 1;	/* records kept in that entry	*/

    e = (tr_ent << tr_esh()) + ((n - 1) >> 7);
    tr_eoff = (n - 1) >> 7;		/* logical extent within the entry */
    tr_ext = (UBYTE)(e & 0x1f);
    tr_s2  = (UBYTE)((e >> 5) & 0x3f);
    tr_rc  = (UBYTE)(n - (((n - 1) >> 7) << 7));
			/*  a full logical extent is EX=k, RC=80h -- never
			    EX=k+1, RC=0, which is the only end-of-file
			    encoding this BDOS writes or accepts	*/
    tr_nblk = (n + UBWORD((GBL.parmp)->blm)) >> ((GBL.parmp)->bsh);

    if ( dirscan(truncit, fcbp, 2) >= 255 ) return(0xff);
    return(0);
}


/************************
*  free_sp entry point	*
************************/

free_sp(dsknum)

UBYTE dsknum;		/* disk number to get free space of */
{
    LONG records;
    REG UWORD   *alvec;
    REG UWORD	bitmask;
    REG UWORD	alvword;
    REG WORD	i;
    BSETUP

    seldsk(dsknum);		/* select the disk */
    records = (LONG)0;		/* initialize the variables */
    alvec = (GBL.dphp)->alv;
    bitmask = 0;
    for (i = 0; i <= (GBL.parmp)->dsm; i++)	/* for loop to compute */
    {
	if ( ! bitmask)
	{
	    bitmask = 0x8000;
	    alvword = ~(*alvec++);
	}
	if ( alvword & bitmask)
	    records += (LONG)( ((GBL.parmp)->blm) + 1 );
	bitmask >>= 1;
    }
}
