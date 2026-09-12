/* System-mode program loading and launch.
 * Function 59 uses the BIOS memory-region table; cpy_out writes the base page;
 * xfer sets NSPSEG/NSPOFF and IRETs into the program.
 * Cold and warm boots load the requested program, or A:CCP.Z8K when none
 * is pending. The BDOS cache buffers repeated CCP reads. */

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
		/*  NOSPLIT (pgmld.c): the split-I/D data bank and side
		    table are single shared pages, so a second split
		    program cannot be loaded while one is live in another
		    process.  Refused before the load, which is the whole
		    point of the code -- the live program is untouched.  */
		"Split I/D program already running$",
		"Program Load Error$"
	};
#define MSGMAX	5			/* the last index in msgs[]	*/

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

/*  THE STATE IS RESIDENT NOW, one per process descriptor, held by
    src/bdos/proc.c and reached through ccpsvcur() -- the descriptor of
    the process that is asking, so two sessions never share one.  It used
    to be a page at a fixed TPA offset, which is why a TPA segment could
    not be shorter than 64 KB.

    `sv' stays as the working copy, so every sv.field reference in this
    file is unchanged; what moved is the two ends of the exchange.
    svload()/svstore() copy between it and this process's resident state
    instead of cpy_in/cpy_out-ing the TPA.  Both ends are resident BSS, so
    the copy is a plain byte loop and needs no address mapping.  */

EXTERN struct ccpsv	*ccpsvcur();	/* src/bdos/proc.c		*/

static struct ccpsv	sv;

MLOCAL VOID svload()			/* resident state -> sv		*/
{
    REG UBYTE	*s, *d;
    REG WORD	i;

    s = (UBYTE *) ccpsvcur();
    d = (UBYTE *) &sv;
    for (i = 0; i < sizeof sv; i++)
	d[i] = s[i];
}

MLOCAL VOID svstore()			/* sv -> resident state		*/
{
    REG UBYTE	*s, *d;
    REG WORD	i;

    s = (UBYTE *) &sv;
    d = (UBYTE *) ccpsvcur();
    for (i = 0; i < sizeof sv; i++)
	d[i] = s[i];
}


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
** ccpsvinit() -- build one process's CCP state.
**		GLOBAL because it is not only the cold
**		boot: proc.c pcrgen() builds a session's
**		too.  It is HANDED the state to build
**		rather than finding it, because the state
**		is resident and per descriptor now.  It
**		used to write to a fixed TPA offset in
**		WHATEVER page was the TPA when it was
**		called, which is why its caller had to be
**		standing inside the page swap.
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

GLOBAL VOID ccpsvinit(p)
struct ccpsv *p;			/* the descriptor's state to build */
{
    REG WORD		i;
    REG UBYTE		*q;

    /*  The size this is budgeted at.  It is resident BSS now rather than a
	reservation at the top of the TPA, so overrunning it no longer
	corrupts a program -- but PNPROC copies of it are charged against
	the 128 KB resident image check (mk/system.mk), and that is worth
	holding to a stated figure.  */

    if ((long) sizeof sv > (long) CCPSVLEN)
	ldfail("\r\nCCP state exceeds its budgeted size$");

    q = (UBYTE *) p;
    for (i = 0; i < sizeof sv; i++)
	q[i] = 0;

    /*  The initialisers ccp.c used to spell as `= TRUE' / `= DISK_A'.

	THE THREE POINTERS ARE LEFT NULL ON PURPOSE.  They point into the
	CCP's own copy of this state, which is BSS inside the transient's
	image, and the system does not know where that is -- it could name
	the fixed page before, and there is no fixed page now.  ccp.c's
	main() points them at its own usercmd when it finds them null,
	which is the same place this used to compute.  */

    p->sv_magic     = CCPSVMAGIC;
    p->sv_first_sub = 1;		/* ccp.c first_sub = TRUE	*/
    p->sv_dirflag   = 1;		/* ccp.c dirflag   = TRUE	*/
    p->sv_cur_disk  = 1;		/* ccp.c cur_disk  = DISK_A	*/
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
	ccpsvinit(ccpsvcur());
    }

    /*  THE RECOVERY THAT USED TO BE HERE IS GONE.  While the state was a
	page at TPA 0xFA00 a program that ran past @MXTPA could scribble on
	it, and a lost magic meant damage that had to be repaired.  The
	state is resident and SYS-only now, so no Normal-mode program can
	reach it at all.  What the magic still answers is "has this
	descriptor's state ever been built?", which is false for a slot
	whose process was created by fn 144 rather than as a session and
	which then loads a CCP.  Resident BSS starts zeroed, so the test
	is exactly that question and nothing more.  */

    if (ccpsvcur()->sv_magic != CCPSVMAGIC)
	ccpsvinit(ccpsvcur());
    svload();

    pend = sv.sv_pend;
    if (pend) {				/* one request, one load	*/
	sv.sv_pend = 0;
	svstore();
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
	bdos(PRNTSTR, map_adr((XADDR) msgs[min(MSGMAX, k)], MYDATA));
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
**		The state is read afresh here rather
**		than reusing the copy ccprun() left,
**		because this runs while a CCP is live
**		and that copy is stale by definition.
**		It acts on the RESIDENT copy: the CCP's
**		own working copy dies with the warm boot
**		this is part of, and the CCP reads the
**		resident one back when it reloads.
**
****************************************************/

VOID ccpabort()
{
    svload();
    if (sv.sv_magic != CCPSVMAGIC)
	return;
    sv.sv_submit = 0;
    sv.sv_morecmds = 0;
    svstore();
}


/****************************************************
**
** ccpsvget()/ccpsvput() -- BDOS 150 and 151.
**		The transient CCP's only way to its own
**		state, which is resident and SYS-only.
**		It GETs into its BSS copy when it starts
**		a command line and PUTs the copy back
**		when it finishes one, and again before
**		the warm boot that launches a program
**		(src/ccp/ccpgo.c __LOAD), because that
**		copy is in the TPA the program is about
**		to be loaded into.
**
**		Both act on the CALLER'S descriptor, so a
**		second session's CCP reaches its own
**		state and never another's.
**
****************************************************/

UWORD ccpsvget(xp)
XADDR xp;
{
    if (ccpsvcur()->sv_magic != CCPSVMAGIC)
	ccpsvinit(ccpsvcur());
    cpy_out(ccpsvcur(), xp, (long) sizeof sv);
    return (0);
}

UWORD ccpsvput(xp)
XADDR xp;
{
    cpy_in(xp, ccpsvcur(), (long) sizeof sv);
    return (0);
}
