/*
 * Portions Copyright (c) 2026 Kevin Dedon.
 */

/* BDOS cold start.  Linked into the boot module, which only cold boot
 * runs. */

#include "stdio.h"		/* Standard I/O declarations */
#include "bdosdef.h"		/* Type and structure declarations for BDOS */
#include "biosdef.h"		/* bsetvec, bgetseg, bconses */
#include "scb.h"		/* SCB offsets; the page length starts there */
#include "boottrace.h"		/* opt-in cold-boot markers (src/bios) */

/* Generated banner strings are complete dollar-terminated literals;
 * the K&R compiler does not concatenate adjacent string literals. */
#include "cpmver.h"

EXTERN		prt_line();		/* Print String function 	*/
EXTERN UWORD	xbdos();		/* BDOS main routine (C900: was _bdos) */
EXTERN UBYTE	*traphnd();		/* assembly language trap handler */
EXTERN		initexc();		/* init the exception handler in  */
					/* exceptn.s			*/
EXTERN		psession();		/* proc.c			*/
EXTERN UBYTE	scbimg[];		/* the SCB image (scb.c)	*/
EXTERN  XADDR	tpa_lt;			/* TPA lower limit (temporary)	*/
EXTERN  XADDR	tpa_lp;			/* TPA lower limit (permanent)	*/
EXTERN  XADDR	tpa_ht;			/* TPA upper limit (temporary)	*/
EXTERN  XADDR	tpa_hp;			/* TPA upper limit (permanent)	*/

#define trap2v 34			/* trap 2 vector number */

/* Start the cold boot's ONE extra session: on the console bound to SCC-B,
 * and only when console 0 is video (BIOS function 33 decides), following
 * COHERENT's /etc/ttys.  A serial operator gets
 * none.  Other bound consoles get nothing automatically; SESSION n still
 * reaches them.  Insufficient memory or a missing CCP leaves no session. */

GLOBAL VOID pcoldses()
{
	REG WORD c;

	c = bconses();
	if (c > 0)
		psession(c);
}


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
    REG WORD	i;		/* console index for the type-ahead table */
    BSETUP

    BTRACE("<6>");		/* bdosinit entered */
    bsetvec(trap2v, map_adr((long)traphnd, 257)); /* set up trap vector */
						   /* (inst. space addr) */
    BTRACE("<7>");		/* trap-2 vector set (I-space map_adr) */
    for (i = 0; i < CONBUFS; i++)
	kbchar[i] = 0;		/* every console's type-ahead is empty */
				/* initialize the "global" variables */
    GBL.delim  = '$';
    GBL.lstecho = FALSE;
    GBL.echodel = TRUE;
    GBL.chainp  = XNULL;
    GBL.user    = 0;
    GBL.multcnt = 1;
    GBL.errmode = 0;
    GBL.conmode = 0;
    GBL.retcode = 0;
    /* A zero page length disables the BDOS pager. Keep PM_ON as the published
 * default for utilities that implement their own paging. */
    GBL.conpage = scbimg[SCB_CONPAGE];	/* 0 unless stamped: pager off	*/
    GBL.conline = 0;
    GBL.pmdefault = PM_ON;	/* v3's default, for the utilities	*/
    GBL.pagemode = GBL.pmdefault;
    BTRACE("<8>");		/* GBL block initialised */
    GBL.dirsecn = -1;		/* the directory buffer holds nothing yet.
				   It was a file-scope static in dskutil.c
				   with an initialiser; it lives here so it
				   travels with the per-process buffer it
				   describes, and bss-zero would have
				   claimed record 0 was resident */
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
    /*	THE SCC-B SESSION AT A VIDEO CONSOLE.  This is the once-per-machine
	moment: the file system is up, so CCP.Z8K can be loaded, and the
	first CCP has not run, so nothing is on the consoles yet.  A serial
	operator gets no session here.  Every refusal is ignored -- see
	pcoldses() -- so a 512 KB machine and a disk with no CCP on it come
	up as they always did.  */
    pcoldses();
}
