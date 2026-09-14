

#include "stdio.h"		/* Standard I/O declarations */

#include "bdosdef.h"		/* Type and structure declarations for BDOS */

#include "biosdef.h"		/* BIOS definitions, needed for bios wboot */
#include "boottrace.h"		/* opt-in cold-boot markers (src/bios) */

#include "cpmver.h"


/*  Declare external functions */
EXTERN		conout();		/* Console Output function	*/
EXTERN UBYTE	conin();		/* Console Input function	*/
EXTERN		prt_line();		/* Print String function 	*/
EXTERN UWORD	xbdos();		/* BDOS main routine (C900: was _bdos) */
EXTERN UBYTE	*traphnd();		/* assembly language trap handler */
EXTERN		rsxwboot();		/* RSX warm-boot removal (rsx.c)  */
EXTERN		initexc();		/* init the exception handler in  */
					/* exceptn.s			*/
EXTERN UWORD	dirscan();		/* Directory scanning routine	*/
EXTERN UWORD	dir_rd();		/* read one directory record	*/
EXTERN BOOLEAN  set_attr();		/* Set File attributes function */

/*  Declare external variables */
EXTERN	UWORD	log_dsk;		/* logged-on disk vector	*/
EXTERN	UWORD	ro_dsk;			/* read-only disk vector	*/
EXTERN	UWORD	crit_dsk;		/* vector of critical disks	*/
EXTERN  XADDR	tpa_lt;			/* TPA lower limit (temporary)	*/
EXTERN  XADDR	tpa_lp;			/* TPA lower limit (permanent)	*/
EXTERN  XADDR	tpa_ht;			/* TPA upper limit (temporary)	*/
EXTERN  XADDR	tpa_hp;			/* TPA upper limit (permanent)	*/
EXTERN  VOID	ccpabort();		/* sys/ccprun.c: cancel the CCP's
					   submit file.  `submit' and
					   `morecmds' used to be CCP globals
					   in the system image; the CCP is a
					   transient now and they are fields
					   of its state page (sys/ccpsv.h) */


#define trap2v 34			/* trap 2 vector number */
#define ctrlc  3			/* control-c		*/


/*  System serial number returned by function 107.  Six bytes, as on
    CP/M 3 (bdos30.asm:5082-5091, where the field is likewise a literal
    waiting to be stamped by the system generator).  gencpm-c900 will
    own this value; until it exists the port ships one.  */

GLOBAL UBYTE serial[6] = { 'C', '9', '0', '0', '0', '1' };


{
};


/********************************
*  bdos initialization routine	*
********************************/

bdosinit()
/* Initialize the File System */
{
    REG struct
    {
	WORD	nmbr;
	XADDR	low;
	LONG	length;
    } *segp;
    BSETUP

    BTRACE("<6>");		/* bdosinit entered */
    bsetvec(trap2v, map_adr((long)traphnd, 257)); /* set up trap vector */
						   /* (inst. space addr) */
    BTRACE("<7>");		/* trap-2 vector set (I-space map_adr) */
    GBL.delim  = '$';
    GBL.lstecho = FALSE;
    GBL.echodel = TRUE;
    GBL.chainp  = XNULL;
    GBL.user    = 0;
    GBL.multcnt = 1;
    GBL.errmode = 0;
    GBL.conmode = 0;
    GBL.retcode = 0;
    GBL.conpage = 0;		/* not configured: the BDOS pager is off */
    GBL.conline = 0;
    GBL.pmdefault = PM_ON;	/* v3's default, for the utilities	*/
    GBL.pagemode = GBL.pmdefault;
    BTRACE("<8>");		/* GBL block initialised */
    BTRACE("<9>");		/* about to call xbdos(13) */
    xbdos(13,0, XNULL);		/* reset disk system function */
    BTRACE("<c>");		/* xbdos(13) returned */
    BTRACE("<d>");		/* about to print the BDOS sign-on */
    prt_line(SYS_BANNER);
		/* C900: the FF + literal newline the original embedded in the
		   string are now escapes -- cc0 rejects raw newline in a string */
    prt_line("\r\nCopyright 1982 Digital Research Inc., Zilog Inc.$");
    prt_line(SYS_COPYRIGHT);
    segp = bgetseg();		/* get pointer to memory segment table */
    tpa_lt = tpa_lp = segp->low;
    tpa_ht = tpa_hp = tpa_lp + segp->length;
    initexc( &(GBL.excvec[0]) );
}


/************************
*  warmboot entry point	*
************************/

warmboot(parm)
/* Warm Boot the system */
WORD parm;			/* 1 to reset submit flag */
{
    BSETUP

    /*	The read-only drive vector survives the warm boot; only function
	13 or 37 clears it.  crit_dsk (per-program critical disks) is still
	cleared on warmboot.  */
    crit_dsk = 0;
    if (parm)
	ccpabort();		/* abandon any submit file: this is the
				   ^C and disk-error path, and v3 does the
				   same thing by deleting $$$.SUB
				   (ccp3.asm:463-470) */
    rsxwboot();			/* drop the resident system extensions
				   flagged for removal, and republish the
				   fence.  v3 does this in the same place
				   -- the CCP's per-command `rsx$chain'
				   call, ccp3.asm:389-396 -- and it must
				   run BEFORE the TPA limits are restored,
				   because rsxwboot() sets them */
    tpa_lt = tpa_lp;
    tpa_ht = tpa_hp;
    GBL.multcnt = 1;		/* the next program starts at one record
				   per read/write call (fcn 44), with the
				   BDOS reporting disk errors itself (fcn
				   45) -- where the v3 CCP puts them */
    GBL.errmode = 0;
    GBL.conmode = 0;
    /*	page mode back to its default, and a fresh page.  v3 does this
	per COMMAND, in the CCP (ccp3.asm:603-614 `set$pg$mode'), so that
	a program which turned paging off cannot leave it off for the
	next one.  The CCP regains control here -- this is the same
	moment rsxwboot() above calls v3's per-command `rsx$chain' -- so
	this is where the reset belongs in a BDOS that owns the byte.  */
    GBL.pagemode = GBL.pmdefault;
    GBL.conline = 0;
    initexc( &(GBL.excvec[0]) );
    bwboot();
}


/*************************/
/*  disk error handlers  */
/*************************/

prt_err(p)
/*  print the error message  */

BYTE  *p;
{
    BSETUP

    prt_line(p);
    prt_line(" error on drive $");
    conout(GBL.curdsk + 'A');
}


abrt_err(p)
/*  print the error message and always abort */

BYTE  *p;
{
    BSETUP

    prt_err(p);
    GBL.retcode = RC_BDOS;	/* what the program left behind (fcn 108) */
    warmboot(1);
}


ext_err(p)
/*  print the error message, and allow for retry, abort, or ignore */

BYTE  *p;
{
    REG UBYTE  ch;
    BSETUP

    prt_err(p);
    do
    {
	prt_line("\n\rDo you want to:  Abort (A),  Retry (R),$");
	prt_line("  or Continue with bad data (C)? $");
	ch = conin() & 0x5f;
	prt_line("\r\n$");

	switch ( ch )
	{
	    case ctrlc:
	    case 'A':  GBL.retcode = RC_BDOS;
		       warmboot(1);
	    case 'C':  return(1);
	    case 'R':  return(0);
	}
    }   while (TRUE);
}


filero(fcbp)
/*  File R/O error  */

REG struct fcb *fcbp;
{
    REG BYTE *p;
    REG UWORD i;
    REG UBYTE  ch;
    REG UWORD  rtn;
    WORD       sec;
    BSETUP

    p = (BYTE *)fcbp;
    prt_line("file error: $");
    i = 8;
    do conout(*++p); while (--i);
    conout('.');
    i = 3;
    do conout(*++p); while (--i);
    prt_line(" is read-only.$");
    do
    {
 prt_line("\r\nDo you want to: Change it to read/write (C), or Abort (A)? $");
	ch = conin() & 0x5f;
	prt_line("\r\n$");

	switch ( ch )
	{
	    case ctrlc:
	    case 'A':   GBL.retcode = RC_BDOS;
			warmboot(1);
	    case 'C':   fcbp->ftype[robit] &= 0x7f;
			sec = GBL.dirsecn;
			rtn = dirscan(set_attr, fcbp, 2);
			return(rtn);
	}
    }   while (TRUE);
}



seterr(code, dsk)
/*  record an error for the program to collect, and show it -- in the CP/M 3
    long form -- unless the program asked for the mode that keeps the
    console quiet  */

UWORD	  dsk;			/* drive the error is on	*/
{
    REG UBYTE *p;
    REG UWORD  i;
    BSETUP

    GBL.errcode = (UBYTE)code;
    if ( UBWORD(GBL.errmode) == 0xff ) return;

    prt_line("\r\nCP/M Error On $");
    conout(dsk + 'A');
    prt_line(" : $");
    prt_line(errmsg[code-1]);
    prt_line("\r\nBDOS Function = $");
    i = UBWORD(GBL.curfx);
    if (i >= 100)
    {
	conout('1');		/* no BDOS function reaches 200 */
	i -= 100;
    }
    conout('0' + i / 10);
    conout('0' + i % 10);
    if ( (p = (UBYTE *)GBL.curfcb) != NULL )
    {
	prt_line(" File = $");	/* mask the attribute bits out of the
				   name: they are flags, not characters */
	i = 8;
	do conout(*++p & 0x7f); while (--i);
	conout('.');
	i = 3;
	do conout(*++p & 0x7f); while (--i);
    }
    prt_line("\r\n$");
}


/************************
*  error entry point	*
************************/

error(errnum, fcbp)	/* VARARGS */
/* Print error message, do appropriate response */

UWORD        errnum;			/* error number */
struct fcb  *fcbp;			/* pointer to fcb */
{
    REG UWORD code;
    BSETUP

    if (GBL.errmode)
    {			/* the program asked for the error itself */
	switch (errnum)		/* may83 error number -> CP/M 3 code */
	{
	    case 2:			/* select, no retry offered */
	    case 3:  code = 4; break;	/* select, retryable	*/
	    case 4:  code = 2; break;	/* write to a read-only disk */
	    case 5:  code = 3; break;	/* write to a read-only file */
	    default: code = 1;		/* read/write: physical error */
	}
	seterr(code, UBWORD(GBL.curdsk));
	return(1);	/* stop retrying: the caller unwinds and the
			   dispatcher turns errcode into the return value */
    }

    prt_line("\r\nCP/M Disk $");
    switch (errnum)
    {
	case 0:  return( ext_err("read$") );
		 /* break; */

	case 1:  return( ext_err("write$") );
		 /* break; */

	case 2:  abrt_err("select$");
		 /* break; */

	case 3:  return( ext_err("select$") );
		 /* break; */

	case 4:  abrt_err("change$");
		 /* break; */

	case 5:  return filero(fcbp);
		 /* break; */

    }
}


/*****************************
*  set exception entry point *
*****************************/

setexc(xepbp)
/* Set Exception Vector */
REG XADDR xepbp;
{
    REG WORD i;
    REG struct
    {
        WORD vecnum;
        UBYTE *newvec;
        UBYTE *oldvec;
    } epb;

    BSETUP

    cpy_in(xepbp, &epb, sizeof epb);		/* copy in param block */

    i = epb.vecnum-2;
    if ( i==32 || i==33) return(-1);
    if ( (30 <= i) && (i <= 37) ) i -= 20;
    else if ( (i < 0) || (i > 9) ) return(-1);
    epb.oldvec = GBL.excvec[i];
    GBL.excvec[i] = epb.newvec;

    cpy_out(&epb, xepbp, sizeof epb);		/* copy out param block */

    return(0);
}


/*****************************
*  get/set TPA entry point   *
*****************************/

set_tpa(xp)
/* Get/Set TPA Limits */
REG XADDR xp;

#define set	1
#define sticky	2

{
struct
    {
	UWORD parms;
	XADDR low;
	XADDR high;
    } p;

    cpy_in(xp, &p, sizeof p);		/* copy in param block */

    if (p.parms & set)
    {
	tpa_lt = p.low;
	tpa_ht = p.high;
	if (p.parms & sticky)
	{
	    tpa_lp = tpa_lt;
	    tpa_hp = tpa_ht;
	}
    }
    else
    {
	p.low = tpa_lt;
	p.high = tpa_ht;
    }

    cpy_out(&p, xp, sizeof p);		/* copy out param block */

}


/*****************************************************
**
** ubyte = cpy_bi(xaddr)-- copy byte in
**
*****************************************************/

UBYTE cpy_bi(addr)
XADDR addr;
{
	UBYTE b;

	cpy_in(addr, &b, 1);
	return b;
}

