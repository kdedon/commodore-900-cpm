
#include "stdio.h"		/* Standard declarations for BDOS, BIOS */

#include "bdosdef.h"		/* BDOS type and structure declarations	*/

#include "biosdef.h"		/* Declarations of BIOS functions 	*/
#include "boottrace.h"		/* opt-in cold-boot markers (src/bios) */

#include "basepage.h"		/* Base page structure			*/

#include "c900cfg.h"		/* TPABASE				*/

#include "ccpsv.h"		/* the state page			*/

#define SEP_ID	0x4000		/* Separate I/D flag			*/
#define	SEG	0x2000		/* Segmented load module		*/

#define	NREGIONS 2		/* Number of MRT regions		*/

#define GOOD	0		/* good return value			*/

#define WARMBOOT 0		/* Warm reboot BDOS call		*/
#define PRNTSTR	9		/* Print String BDOS call		*/
#define OPENF	15		/* Open File BDOS call			*/
#define SETDMA	26		/* Set DMA Address BDOS call		*/
#define PGLOAD	59		/* Program Load BDOS call		*/

#define MYDATA	0		/* Argument for map_adr			*/
#define TPAPROG	5		/* Argument for map_adr			*/
#define TRUE_TPAPROG	(TPAPROG | 0x100)

#define BGETMRT 18		/* Number of the BIOS call		*/

extern	UWORD	bdos();		/* To do I/O into myself		*/
extern	XADDR	bios();		/* To get MRT pointer			*/
extern	VOID	xfer();		/* Transfer control to user program	*/
extern	VOID	bdosinit();	/* One-time BDOS/CCP bring-up		*/
extern	LONG	map_adr();

struct lpb {
	XADDR	fcbaddr;	/* Address of fcb of opened file	*/
	XADDR	pgldaddr;	/* Low address of prog load area	*/
	XADDR	pgtop;		/* High address of prog load area, +1	*/
	XADDR	bpaddr;		/* Address of basepage; return value	*/
	XADDR	stackptr;	/* Stack ptr of user; return value	*/
	short 	flags;		/* Loader control flags; return value	*/
} LPB;

struct m_rt {			/* The Memory Region Table		*/
	int entries;
	struct {
		XADDR	m_low;
		XADDR	m_len;
	} m_reg[NREGIONS];
};

struct ustack			/* User's initial stack (nonsegmented)	*/
	{
		short	two;
		short	bpgaddr;
	} stack =
	{
		0x0002
	};

struct sstack			/* User's initial stack (segmented)	*/
	{
		XADDR	stwo;
		XADDR	sbpgadr;
	} sstack;

static char	*msgs[] =
	{
		"",
		"File is not executable$",
		"Insufficient memory$",
		"Read error on program load$",
		"Program Load Error$"
	};


	{
		{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
		0,
		0x1800
	};

/*  The CCP's own FCB.  Drive 1 is A:, and the CCP is looked for there
    and nowhere else -- v3 says the same thing in a comment, "load the
    CCP from a file called CCP.COM on the system drive (A:)"
    (`ref/cpm3/boot.asm:41-42').  User zero: the load happens with the
    BDOS user code forced to 0 below, because a session left in user 5
    must still find its command processor.  */

static UBYTE ccpfcb[36] = {
	1, 'C','C','P',' ',' ',' ',' ',' ', 'Z','8','K',
	0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0
};

static char	nofile[] = "\r\nCannot load A:CCP.Z8K -- system halted$";

static BOOLEAN	sysinit = FALSE;	/* bdosinit has run		*/



static struct ccpsv	sv;


/****************************************************
**
** ldfail() -- a load that cannot be retried.
**		A warm boot here would reload the CCP,
**		which is the thing that just failed.
**		v3 has the same dead end -- `no$CCP'
**		(ref/cpm3/boot.asm:71-74).
**
****************************************************/

MLOCAL VOID ldfail(m)
BYTE *m;
{
    prt_line(m);
    for (;;)
	;
}


/****************************************************
**
**		v3 has no counterpart because v3's
**		page is created on demand by
**		`multistart' (ccp3.asm:1798-1812) and
**		its CCP's initialised variables are
**		part of the CCP image.  Ours is
**		permanent, so somebody has to write the
**		values ccp.c used to carry as
**		initialisers.
**
****************************************************/

{
    REG WORD		i;


    if ((long) sizeof sv > (long) CCPSVLEN)

    for (i = 0; i < sizeof sv; i++)

    /*  The initialisers ccp.c used to spell as `= TRUE' / `= DISK_A'.
}


/****************************************************
**
** ccprun() -- run the pending program, or the CCP.
**		Entered from ccpentry (src/glue.s) on
**		the cold boot and on every warm boot.
**		Never returns.
**
****************************************************/

VOID ccprun()
{
    register short	k;
    UWORD		pend;
    UWORD		olduser;
    UBYTE		tlen;
    REG WORD		i;

    BTRACE("<5>");		/* ccprun entered from ccpentry */

    if (!sysinit) {		/* the latch ccpif.s used to hold	*/
	sysinit = TRUE;
	bdosinit();
    }


    pend = sv.sv_pend;
    if (pend) {				/* one request, one load	*/
	sv.sv_pend = 0;
    }

    if (pend) {
	/*  The CCP resolved and OPENED a command; its FCB, its tail and
	    its two base-page FCBs came in with the page.  */
	tlen = sv.sv_ptlen;
    } else {
	/*  Nothing pending: (re)load the CCP.  It is opened here rather
	    than by its predecessor because on the cold boot there is no
	    predecessor.  User zero -- see ccpfcb above -- and the user
	    code the session was in is put back before the CCP runs.  */
	olduser = bdos(32, 0xffffL);
	bdos(32, 0L);
	for (i = 12; i < 36; i++)
	    ccpfcb[i] = 0;
	k = bdos(OPENF, map_adr((XADDR) ccpfcb, MYDATA));
	bdos(32, (long) olduser);
	if (k > 3)
	    ldfail(nofile);
	tlen = 0;
	for (i = 0; i < 36; i++)
	    sv.sv_pfcb1[i] = sv.sv_pfcb2[i] = 0;
	for (i = 0; i < SV_CMDLEN; i++)
	    sv.sv_ptail[i] = 0;
    }

	if (!pend)
	    ldfail(nofile);
	bdos(WARMBOOT, 0L);
    }

    xfer(map_adr((XADDR) &context, MYDATA));		/* Go for it!	*/
}


/****************************************************
**
** ccpabort() -- cancel the CCP's submit file.
**		The ^C and disk-error path
**		(warmboot(1), sys/bdosmisc.c).  When
**		the CCP was resident these two flags
**		were BDOS-visible globals in the same
**		image; now they are page fields and the
**		BDOS reaches them the way it reaches
**		anything else in the TPA.
**
**		than reusing the copy ccprun() left,
**		because this runs while a CCP is live
**		and that copy is stale by definition.
**
****************************************************/

VOID ccpabort()
{
    if (sv.sv_magic != CCPSVMAGIC)
	return;
    sv.sv_submit = 0;
    sv.sv_morecmds = 0;
}
