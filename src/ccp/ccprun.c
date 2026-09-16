
#include "stdio.h"		/* Standard declarations for BDOS, BIOS */

#include "bdosdef.h"		/* BDOS type and structure declarations	*/

#include "biosdef.h"		/* Declarations of BIOS functions 	*/
#include "boottrace.h"		/* opt-in cold-boot markers (src/bios) */

#include "basepage.h"		/* Base page structure			*/

#include "c900cfg.h"		/* TPABASE				*/

#include "ccpsv.h"		/* the state page			*/

#include "proc.h"		/* struct context -- shared with the
				   dispatcher, which builds a process out
				   of one (src/bdos/proc.c)		*/

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

/*  Startup context for the user's program (the layout is xfer_'s and
    lives in proc.h now, because the dispatcher builds a process out of
    one).  */

struct context	context =
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
** ldimage() -- put a program in the TPA and build
**		the context that starts it.
**
**		This is the half of ccprun() that is
**		about a PROGRAM rather than about the
**		CCP's request, and it is separate
**		because it has a second caller: the
**		dispatcher (src/bdos/proc.c pcreate),
**		which runs it with the TPA's page
**		swapped so that the image lands in a
**		second process's 64 KB instead of this
**		one's.  Everything here addresses the
**		TPA as 0x32:xxxx and is correct across
**		that swap without knowing about it,
**		which is the whole reason the swap is
**		the memory model (c900cfg.h).
**
**		`fcbadr' is a RESIDENT, already-opened
**		FCB -- resident because the caller's
**		page may not be the one this loads
**		into.  The tail and the two base-page
**		FCBs are resident buffers for the same
**		reason.  Returns 0, or pgmld's error.
**
****************************************************/

GLOBAL WORD ldimage(fcbadr, tlen, tailp, fcb1p, fcb2p, ctxp)
XADDR	fcbadr;			/* opened FCB, in the system's own data	*/
WORD	tlen;			/* command tail length			*/
UBYTE	*tailp;			/* SV_CMDLEN tail bytes, no length byte	*/
UBYTE	*fcb1p, *fcb2p;		/* the base page's two parsed FCBs	*/
struct context *ctxp;		/* filled in on success			*/
{
    struct m_rt		*mpr;
    register XADDR	physaddr;
    register short	k;
    UBYTE		tl;

    mpr = (struct m_rt *) bios(BGETMRT);
    LPB.pgldaddr = mpr->m_reg[0].m_low;
    LPB.pgtop = mpr->m_reg[0].m_len;
    LPB.fcbaddr = fcbadr;

    if ((k = bdos(PGLOAD, map_adr((XADDR) &LPB, MYDATA))) != GOOD)
	return (k);

/* Move command tail to basepage buffer; reset DMA address to that buffer. */

    tl = (UBYTE) tlen;
    physaddr = LPB.bpaddr - (SECLEN - sizeof (struct b_page));
    bdos(SETDMA, physaddr);
    cpy_out(&tl, physaddr, 1L);
    cpy_out(tailp, physaddr + 1, (long) SECLEN - 1);

/* Base page fcb's: the CCP parsed them out of the tail (sys/ccpgo.c).	*/

    physaddr -= sizeof (struct fcb);
    cpy_out(fcb1p, physaddr, (long) sizeof (struct fcb));

    physaddr -= sizeof (struct fcb);
    cpy_out(fcb2p, physaddr, (long) sizeof (struct fcb));

/* Build the user stack, then the context xfer() (or presume()) starts.	*/

    if (LPB.flags & SEG) {				/* Segmented    */
	sstack.sbpgadr = LPB.bpaddr;
	sstack.stwo    = LPB.pgldaddr + 2;
	cpy_out(&sstack, LPB.bpaddr - sizeof sstack, sizeof sstack);
	ctxp->regs[14] = (short)(LPB.stackptr >> 16);
	ctxp->regs[15] = (short)LPB.stackptr;
	ctxp->PC  = LPB.pgldaddr;
	ctxp->FCW = 0x9800;
    } else {						/* Nonsegmented */
	stack.bpgaddr = (short) LPB.bpaddr;
	cpy_out(&stack, LPB.bpaddr - sizeof stack, sizeof stack);
	ctxp->regs[15] = (short) LPB.stackptr;
	ctxp->PC  = map_adr(LPB.pgldaddr, TRUE_TPAPROG);
	ctxp->FCW = 0x1800;
    }
    return (GOOD);
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
    XADDR		fcbadr;
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
	fcbadr = map_adr((XADDR) sv.sv_pfcb, MYDATA);
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
	fcbadr = map_adr((XADDR) ccpfcb, MYDATA);
	tlen = 0;
	for (i = 0; i < 36; i++)
	    sv.sv_pfcb1[i] = sv.sv_pfcb2[i] = 0;
	for (i = 0; i < SV_CMDLEN; i++)
	    sv.sv_ptail[i] = 0;
    }

    if ((k = ldimage(fcbadr, (WORD) tlen, sv.sv_ptail,
		     sv.sv_pfcb1, sv.sv_pfcb2, &context)) != GOOD) {
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
