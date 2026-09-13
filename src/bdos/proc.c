
#include "stdio.h"		/* Standard declarations		*/
#include "bdosdef.h"		/* struct stvars, GBL/BSETUP		*/
#include "biosdef.h"		/* cpy_in / cpy_out			*/
#include "c900cfg.h"		/* TPASEG, TPABASE			*/
#include "ccpsv.h"		/* struct ccpsv -- per-process CCP state */
#include "proc.h"

EXTERN	VOID	mem_cpy();
EXTERN	XADDR	map_adr();
EXTERN	UWORD	bdos();
EXTERN	VOID	initexc();
EXTERN	WORD	ldimage();	/* the loader half of ccprun (src/ccp)	*/

/*  src/bios/pgalloc.c -- the pages themselves.  */
EXTERN	WORD	pgalloc();
EXTERN	WORD	pgfree();
EXTERN	WORD	pgtpaswap();
EXTERN	VOID	pghold();
EXTERN	WORD	pgrelproc();	/* free THIS process's scratch segments	*/
EXTERN	WORD	pgcur;		/* and the allocator's idea of which
				   process that is.  It is `pcur', and
				   the two are assigned together: see
				   PCUR() below and pgalloc.c's banner. */

/*  pcur moves in exactly three places and every one of them must tell the
    allocator, because pgrelall() on a warm boot releases the scratch of
    the process named here and of no other.  Getting this wrong is not a
    crash, it is a background interpreter's memory being handed away, so
    the assignment is a macro rather than two statements to keep apart.  */
#define	PCUR(x)		(pgcur = pcur = (x))

EXTERN	LONG	tickget();	/* src/bios/trap.s: one LDL of tickcnt	*/
EXTERN	WORD	conststat();	/* src/bios/bios900.c: poll console `n'	*/

/*  src/bios/glue.s and src/bdos/procasm.s -- the two halves of a switch
    that cannot be written in C.  */
EXTERN	VOID	presume();	/* restore a pframe and IRET; no return	*/
EXTERN	VOID	pnspget();	/* read NSPSEG/NSPOFF into a pframe	*/
EXTERN	VOID	pcopark();	/* park mid-call; returns when resumed	*/
EXTERN	VOID	pcoresume();	/* resume one so parked; no return	*/

/*  Resident state a page swap does not move.  */
EXTERN	struct stvars	gbls;		/* bdosmain.c			*/
EXTERN	UWORD	rsxhead, rsxtop;	/* rsx.c			*/
EXTERN	XADDR	tpa_lp, tpa_lt, tpa_hp, tpa_ht;	/* bdosmain.c		*/
EXTERN	WORD	spflag;			/* splitld.c			*/

#define	RSXCEIL		((UWORD)0)	/* rsx.c, same value: 0 = 0x10000,
					   the whole segment		*/

#define	OPENF	15			/* Open File BDOS call		*/

GLOBAL	short	psched = 0;	/* the gate's one-word question		*/
GLOBAL	short	sysstk = PSTKTOP;	/* the running process's supervisor
					   stack top, read by src/bios/glue.s
					   (xfer_, ccpentry_) and procasm.s
					   (presume_).  Initialised to the
					   literal those three used to carry,
					   so nothing changes until there is
					   a second process.		*/


GLOBAL	short	pquant = PQBASE;	/* ticks left in this slice	*/


MLOCAL WORD pqfor(prio)
WORD	prio;
{
	REG WORD q;

	q = PQBASE / (1 + ((prio & 0xFF) >> 6));
	return (q < 1 ? 1 : q);
}

MLOCAL	struct pdesc	pd[PNPROC];
MLOCAL	WORD		pcur = 0;	/* index of the running process	*/

/*  THE CCP'S STATE, ONE PER PROCESS, AND RESIDENT.  It used to be a
    1,536-byte page at a fixed TPA offset (0xFA00), which is why a TPA
    segment could not be shorter than 64 KB and why a program that ran
    past @MXTPA could scribble on it.  Here it is ordinary SYS-only BSS
    indexed by descriptor, so every session has one of its own by
    construction -- where the fixed address had to be built per page by
    hand, inside the swap window, because it named whichever page was the
    TPA at the time.

    The transient CCP cannot address this: it runs in Normal mode and
    resident storage is SYS-only.  It keeps a working copy in its own BSS
    and exchanges it through BDOS 150/151 (src/ccp/ccprun.c).  */

MLOCAL	struct ccpsv	psv[PNPROC];

GLOBAL struct ccpsv *ccpsvcur()
{
	return (&psv[pcur]);
}
MLOCAL	WORD		pnlive = 0;	/* live descriptors; 0 until the
					   first pcreate() adopts the one
					   that was already running	*/
MLOCAL	WORD		pnsplit = 0;	/* live split-I/D processes, 0 or 1 */
MLOCAL	WORD		pnxt = 0;	/* who pgone() must give the machine
					   to.  A variable rather than an
					   argument because pgone() is also
					   called from assembly, out of
					   pcopark_, where there is no
					   argument to pass	*/

/*  The file-system lock.  An owner and a depth, which is precisely the
    contract bdosdef.h states: a process that already holds it walks in,
    and only the outermost release lets go.  bss-zero is a valid unheld
    state (depth 0), so nothing initialises it.  */

MLOCAL	WORD		lkown;		/* process holding it, if lkdep	*/
MLOCAL	WORD		lkdep;		/* how many times over		*/


MLOCAL	WORD		pnoyld;

/*  The request pcreate() is given, copied out of the caller's TPA before
    anything swaps a page under it.  36 + 1 + 128 bytes, and it is
    resident on purpose: after the swap the caller's copy is on another
    page and the FCB this loads from has to still be readable.  */

struct pcreq {
	UBYTE	pq_fcb[36];	/* the program's FCB, unopened		*/
	UBYTE	pq_tlen;	/* command tail length			*/
	UBYTE	pq_tail[SV_CMDLEN];	/* the tail itself, no length byte */
};

MLOCAL	struct pcreq	pq;
MLOCAL	UBYTE		pqfcb1[36], pqfcb2[36];	/* the base page's two FCBs */



MLOCAL VOID pmove(s, d, n)
BYTE	*s, *d;
UWORD	n;
{
	mem_cpy(map_adr((XADDR)s, 0), map_adr((XADDR)d, 0), (long)n);
}



MLOCAL VOID psave(p)
REG struct pdesc *p;
{
	pmove((BYTE *)&gbls, (BYTE *)&p->pd_gbl, (UWORD)sizeof gbls);
	p->pd_rsxhead = rsxhead;
	p->pd_rsxtop  = rsxtop;
	p->pd_tpalp   = tpa_lp;
	p->pd_tpalt   = tpa_lt;
	p->pd_tpahp   = tpa_hp;
	p->pd_tpaht   = tpa_ht;
	p->pd_split   = spflag;
}

MLOCAL VOID pload(p)
REG struct pdesc *p;
{
	pmove((BYTE *)&p->pd_gbl, (BYTE *)&gbls, (UWORD)sizeof gbls);
	rsxhead = p->pd_rsxhead;
	rsxtop  = p->pd_rsxtop;
	tpa_lp  = p->pd_tpalp;
	tpa_lt  = p->pd_tpalt;
	tpa_hp  = p->pd_tpahp;
	tpa_ht  = p->pd_tpaht;
	spflag  = p->pd_split;
}




MLOCAL VOID pwscan()
{
	REG struct pdesc *p;
	REG WORD	 i;

	for (i = 0; i < PNPROC; i++) {
		p = &pd[i];
		if (p->pd_state != PS_LIVE || p->pd_wait == PW_RUN)
			continue;
		if (p->pd_wait == PW_TICK) {
			if ((tickget() - p->pd_wtick) >= 0L) {
				p->pd_wait = PW_RUN;
				pnwait--;
			}
		} else if (p->pd_wait == PW_CON) {
			if (conststat(p->pd_wobj)) {
				p->pd_wait = PW_RUN;
				pnwait--;
			}
		}
	}
}



MLOCAL WORD pnext(i)
REG WORD i;
{
	REG WORD n, j;

	if (pnwait)
		pwscan();

	j = i;
	for (n = 0; n < PNPROC; n++) {
		if (++j >= PNPROC)
			j = 0;
		if (pd[j].pd_state == PS_LIVE && pd[j].pd_wait == PW_RUN)
			return (j);
	}
	return (i);
}



MLOCAL WORD pnextany(i)
REG WORD i;
{
	REG WORD n, j;

	j = i;
	for (n = 0; n < PNPROC; n++) {
		if (++j >= PNPROC)
			j = 0;
		if (pd[j].pd_state == PS_LIVE)
			return (j);
	}
	return (i);
}



GLOBAL VOID pblock(why, obj, dl)
WORD	why, obj;
LONG	dl;
{
	REG struct pdesc *me;

	padopt();
	me = &pd[pcur];
	if (me->pd_wait == PW_RUN)
		pnwait++;
	me->pd_wait  = why;
	me->pd_wobj  = obj;
	me->pd_wtick = dl;
}

GLOBAL VOID punblk()
{
	if (pd[pcur].pd_wait != PW_RUN) {
		pd[pcur].pd_wait = PW_RUN;
		pnwait--;
	}
}

GLOBAL VOID pwake(why, obj)
REG WORD	why, obj;
{
	REG struct pdesc *p;
	REG WORD	 i;

	if (pnwait == 0)
		return;
	for (i = 0; i < PNPROC; i++) {
		p = &pd[i];
		if (p->pd_state == PS_LIVE && p->pd_wait == why &&
		    p->pd_wobj == obj) {
			p->pd_wait = PW_RUN;
			pnwait--;
		}
	}
}



MLOCAL VOID padopt()
{
	REG WORD i;

	if (pnlive == 0) {
		for (i = 0; i < PNPROC; i++)
			pd[i].pd_stk = PSTKOF(i);
		pd[0].pd_state = PS_LIVE;
		pd[0].pd_seg   = 0;		/* it is the TPA */
		pd[0].pd_con   = 0;
		for (i = 0; i < 8; i++)
			pd[0].pd_name[i] = ' ';	/* nothing recorded the file
						   this one came from	*/
		pd[0].pd_prio  = 0;
		pd[0].pd_quant = pqfor(pd[0].pd_prio);
		pd[0].pd_inc   = 0;
		pd[0].pd_wait  = PW_RUN;	/* it is the one running */
		PCUR(0);
		pnlive = 1;
		sysstk = pd[0].pd_stk;		/* which is PSTKTOP, the value
						   it already held	*/
	}
}


/*  IS A SPLIT-I/D PROGRAM LIVE SOMEWHERE ELSE?  The loader's question,
    asked before it writes anything (src/bdos/pgmld.c, X_NXI_MAGIC): the
    split data bank and the side table are single fixed pages that no
    page swap moves (c900cfg.h SPLITDSEG/SPLITTSEG), so loading a second
    split program REPLACES the live one's data image and its patch
    table.  It must be refused before the first byte, not after.

    The descriptors are scanned rather than `pnsplit' consulted, because
    pnsplit counts only the children fn 144 made: a split program the
    CCP loaded into its own page -- ED, ASZ8K, XCON, XDUMP, AR8K, NMZ8K
    and SIZEZ8K are all 0xEE0B on the release disk -- is not in it.

    The descriptor of any process that is NOT running is current by
    construction: a process becomes another process by being switched
    out, and psave() is what switches it out.

    `pcur' is counted only when `pspcur' says to, and the two callers
    are why.  A warm boot loads over the running process's OWN program
    (src/ccp/ccprun.c): that program is finished, its data bank is not
    wanted, and counting it would make a split program unable to be
    followed by another one on the same console.  pcrgen() loads into a
    CHILD's page while the caller stays alive and will resume into its
    own data bank, so there the caller counts -- and its descriptor is
    accurate at that moment because psave(me) has just run.  */

MLOCAL	WORD		pspcur;		/* count pcur as well: set only
					   across pcrgen()'s load	*/

GLOBAL WORD pspother()
{
	REG WORD i;

	if (pnlive == 0)
		return (0);		/* nothing has forked; the running
					   program is the only one there is */
	for (i = 0; i < PNPROC; i++)
		if ((i != pcur || pspcur) &&
		    pd[i].pd_state == PS_LIVE && pd[i].pd_split)
			return (1);
	return (0);
}



XADDR	infop;
{
	REG struct pdesc *me, *kid;
	REG WORD	i, k, seg;
	WORD		kidx;		/* the child's descriptor index	*/
	XADDR		dma0;		/* the caller's own default DMA	*/
	struct context	ctx;

	padopt();
	me = &pd[pcur];

	for (i = 0; i < PNPROC; i++)
		if (pd[i].pd_state == PS_FREE)
			break;
	if (i >= PNPROC)
		return (PC_NOPD);
	kid = &pd[i];
	kidx = i;			/* recorded here because `i' is a
					   loop variable again below, and
					   the child's CCP state is psv[kidx] */

	/*  THE SLOT IS CLAIMED HERE, before anything below can yield.
	    plock() parks the caller whenever another process holds the
	    filesystem lock, and a second creator resuming in that window
	    used to find this same slot still PS_FREE, pick it, and build
	    its process in it -- after which the first creator came back
	    and built its own on top.  PS_RSVD is not PS_FREE, so the
	    search above skips it, and it is not PS_LIVE, so nothing else
	    in this file can see it.  Every failure return below puts it
	    back.  */
	kid->pd_state = PS_RSVD;

	/*  A page.  Zero means the pool is empty, which on a 512 KB
	    machine is always and by construction (pgalloc.c): there the
	    answer to "run a second program" is no, and it is a hardware
	    answer.  */
	if ((seg = pgalloc()) == 0) {
		kid->pd_state = PS_FREE;
		return (PC_NOPAGE);
	}

	plock();

	/*  The request, out of the caller's page and into ours, BEFORE
	    the page moves -- and INSIDE the lock, because `pq' is one
	    resident buffer shared by every creator.  It used to be copied
	    before plock(), so a creator parked in that lock came back to
	    a `pq' the next creator had replaced and loaded the other
	    program's file into its own child's page.  The lock is not
	    widened in scope by this: it already bracketed the whole load,
	    and `infop' still addresses the caller's TPA here because the
	    page swap is below and a resumed process gets its page back.  */
	cpy_in(infop, &pq, (long) sizeof pq);
	if (UBWORD(pq.pq_tlen) > SV_CMDLEN)
		pq.pq_tlen = SV_CMDLEN;	/* UBYTE is a signed char here
						   (stdio.h ALCYON), so the
						   comparison has to widen */

	psave(me);			/* the caller, as it stands now	*/

	pspcur = 1;			/*  and it STAYS alive over the load
					    below, so its own split banks
					    count: a split program launching
					    a split program would otherwise
					    have its data image replaced
					    under it.  psave() above is what
					    makes its descriptor say so	*/

	pnoyld = 1;			/* from here to the swap back, the
					   descriptors do not describe the
					   machine: no switch (see above) */
	pgtpaswap(seg);			/* the child's page is the TPA;	*/
	me->pd_seg = seg;		/*   the caller is parked on seg */

	/*  The child's BDOS state.  It INHERITS the caller's -- current
	    disk, user number, error mode -- which is what a program
	    launching another program means by "the same session", and
	    then takes the per-program resets warmboot() (bdosmisc.c)
	    applies to any newly loaded program.  */
	gbls.multcnt = 1;
	gbls.errmode = 0;
	gbls.conmode = 0;
	gbls.errcode = 0;
	gbls.retcode = 0;
	gbls.column  = 0;
	gbls.chainp  = XNULL;
	initexc(&(gbls.excvec[0]));

	/*  A fresh page carries no RSX modules, so the fence is the
	    whole TPA up to the CCP's state page.  */
	rsxhead = 0;
	rsxtop  = RSXCEIL;
	tpa_lt  = tpa_lp;
	tpa_ht  = tpa_hp = TPABASE + (long)rsxtop;
	spflag  = 0;

	/*  A SESSION NEEDS ITS CCP STATE INITIALISED.  It is the CHILD'S
	    now, named directly, and no longer "whatever page is the TPA":
	    the state is resident and indexed by descriptor, so this does
	    not depend on standing inside the page swap the way the fixed
	    address did.  ccprun() does the same for process 0 at the cold
	    boot.  Without it the CCP loaded below starts on zeros -- no
	    current disk, no command pointer -- and the second console's
	    first prompt is a crash.  */
		ccpsvinit(&psv[kidx]);
	/*  Open and load, exactly as ccprun() does on a warm boot.  Where
	    ccprun() forces user 0 (a session in user 5 must still find
	    its command processor), this does not: a program launched by
	    a program runs in the user area its parent is in, so the open
	    uses the inherited user code.  */
	for (k = 12; k < 36; k++)
		pq.pq_fcb[k] = 0;	/* ex/s1/s2/rc and the map: an FCB
					   handed to fn 15 must be clean */
	k = bdos(OPENF, map_adr((XADDR)pq.pq_fcb, 0));
	if (k > 3) {
		k = 1;			/* BADHDR: no such program	*/
	} else {
		for (i = 0; i < 36; i++)
			pqfcb1[i] = pqfcb2[i] = 0;
		dma0 = me->pd_dma0;	/*  the loader records the default DMA
					    of the program it loaded in the
					    RUNNING descriptor (pgmld.c, for
					    fn 13), and the running descriptor
					    here is the PARENT's: hold the
					    caller's own while it does	*/
		k = ldimage(map_adr((XADDR)pq.pq_fcb, 0), (WORD)pq.pq_tlen,
			    pq.pq_tail, pqfcb1, pqfcb2, &ctx);
		if (k == 0)
			kid->pd_dma0 = me->pd_dma0;	/*  ...to its owner */
		me->pd_dma0 = dma0;
	}

	/*  A second split-I/D program would want a second data bank and
	    a second side table, and there is one of each (c900cfg.h).
	    THE REFUSAL IS THE LOADER'S NOW (pgmld.c, NOSPLIT): it is the
	    only place that knows the file is 0xEE0B, and it knows it
	    before it has written a byte of the shared banks, which is
	    what this check could not do -- by the time it ran, the live
	    split program's data image and side table were already gone.
	    Translated back to PC_SPLIT so fn 144's answer is unchanged.  */
	if (k == PL_NOSPLIT)
		k = PC_SPLIT;

	/*  The backstop, kept: if the loader's question is ever asked
	    wrongly this still refuses, and a refusal after the damage is
	    better than no refusal.  `pnsplit' counts only fn 144's own
	    children -- a split program the CCP loaded is not in it --
	    which is why it cannot be the primary check.  */
	if (k == 0 && spflag && pnsplit > 0)
		k = PC_SPLIT;

	/*  Whatever happened, the caller's page comes back.  On failure
	    the child's page goes back to the pool with it.  */
	pgtpaswap(seg);
	me->pd_seg = 0;
	pnoyld = 0;
	pspcur = 0;			/* the next load is somebody's warm
					   boot until this says otherwise */
	punlock();

	if (k != 0) {
		pgfree(seg);
		kid->pd_state = PS_FREE;	/* the claim, given back	*/
		pload(me);
		return (k);
	}

	for (i = 0; i < 14; i++)
		kid->pd_f.pf_reg[i] = ctx.regs[i];
	kid->pd_f.pf_id     = 0;
	kid->pd_f.pf_fcw    = ctx.FCW & 0xF7FF;
	kid->pd_f.pf_pcseg  = (short)(ctx.PC >> 16);
	kid->pd_f.pf_pcoff  = (short)ctx.PC;
	kid->pd_f.pf_nspseg = ctx.regs[14];
	kid->pd_f.pf_nspoff = ctx.regs[15];

	psave(kid);			/* the child's stvars and fence,
					   which are live in the globals
					   right now		*/
	kid->pd_state = PS_LIVE;
	kid->pd_wait  = PW_RUN;		/* a new process is runnable, and the
					   slot it got may have belonged to
					   one that was not		*/
	kid->pd_inc   = 0;		/* a new process is parked the way a
					   process at the gate is: pd_f is
					   the whole of it	*/
	kid->pd_seg   = seg;		/* its image is parked on seg	*/
	for (i = 0; i < 8; i++)
		kid->pd_name[i] = pq.pq_fcb[1 + i];	/* the FCB's name field,
							   blank-padded already */
	kid->pd_prio  = me->pd_prio;
	kid->pd_quant = pqfor(kid->pd_prio);	/* filled once, so the hot
						   paths only assign	*/
	pghold(seg, 1);			/* a live process's page is not a
					   transient's leak: the warm boot
					   must not reclaim it	*/
	if (kid->pd_split)
		pnsplit++;
	pnlive++;
	psched = 1;

	pload(me);			/* and the caller resumes as if
					   nothing had moved	*/
	return (PC_OK);
}



GLOBAL VOID pgone()
{
	REG struct pdesc *p;
	REG WORD	 seg;

	p = &pd[pnxt];
	seg = p->pd_seg;
	if (seg) {			/* 0 only if it is already the TPA */
		pgtpaswap(seg);		/* ten soutb's: the whole of what
					   MP/M's MEMMGR.ASM was for	*/
		pd[pcur].pd_seg = seg;	/* the outgoing image is on seg now */
		p->pd_seg = 0;
	}
	PCUR(pnxt);
	pload(p);
	sysstk = p->pd_stk;		/* xfer_, ccpentry_ and presume_ all
					   reset the system stack to this */
	pquant = p->pd_quant;		/* the incoming process starts a
					   WHOLE slice, not the remains of
					   the one it is replacing	*/
	if (p->pd_inc)
		pcoresume(map_adr((XADDR)&p->pd_f, 0), p->pd_ssp);
	presume(map_adr((XADDR)&p->pd_f, 0));	/* no return */
}



GLOBAL WORD pyield()
{
	REG struct pdesc *me;
	REG WORD	 nxt;

	if (pnlive < 2 || pnoyld)
		return (0);
	if ((nxt = pnext(pcur)) == pcur)
		return (0);

	me = &pd[pcur];
	pnspget(map_adr((XADDR)&me->pd_f, 0));
	psave(me);
	me->pd_inc = 1;
	pnxt = nxt;
	pcopark(map_adr((XADDR)&me->pd_ssp, 0));	/* ... time passes ... */
	me->pd_inc = 0;
	return (1);
}



GLOBAL WORD pwait(why, obj, dl)
WORD	why, obj;
LONG	dl;
{
	REG WORD ran;

	pblock(why, obj, dl);
	ran = pyield();
	punblk();
	return (ran);
}




GLOBAL VOID plock()
{
	while (lkdep != 0 && lkown != pcur) {
		if (pnlive < 2)
			break;		/* nobody else is live, so nobody
					   else can be holding it; do not
					   wait on an impossibility	*/
		pwait(PW_LOCK, 0, 0L);
	}
	lkown = pcur;
	lkdep++;
}

GLOBAL VOID punlock()
{
	if (--lkdep <= 0) {
		lkdep = 0;
		lkown = -1;
		pwake(PW_LOCK, 0);	/* the outermost release is the event
					   a blocked plock() is waiting for */
	}
}

GLOBAL VOID plkdrop()
{
	if (lkdep != 0 && lkown == pcur) {
		lkdep = 0;
		lkown = -1;
		pwake(PW_LOCK, 0);
	}
}



GLOBAL VOID pdisp(frame)
XADDR	frame;
{
	REG WORD nxt;

	/*  A dispatch has been asked for, so the slice is spent whatever
	    the answer is.  Reloading it here rather than only in pgone()
	    is what stops a dispatch that DECLINES to switch -- one live
	    process, or nobody else runnable -- from being re-asked on
	    every following tick.  */
	pquant = pd[pcur].pd_quant;

	if (pnlive < 2)
		return;
	if ((nxt = pnext(pcur)) == pcur)
		return;

	/*  Save.  36 bytes off the system stack, then the two control
	    registers the hardware banks a non-segmented program's r14
	    and r15 in.  This is the whole of the save half.  */
	cpy_in(frame, &pd[pcur].pd_f, 36L);
	pnspget(map_adr((XADDR)&pd[pcur].pd_f, 0));
	psave(&pd[pcur]);
	pd[pcur].pd_inc = 0;		/* parked at the gate: pd_f is all of
					   it, and this process's supervisor
					   stack is empty		*/

	pnxt = nxt;
	pgone();			/* no return */
}



GLOBAL WORD procdead()
{
	REG WORD nxt, seg;

	plkdrop();			/* whatever this program was holding
					   when it died, it is not holding
					   now.  This runs for the foreground
					   warm boot too, where it is a no-op
					   unless a BDOS error killed the
					   program inside a locked region */
		return (0);

	/*  This process is about to stop existing, so its scratch segments
	    have to go back NOW, while pgcur still names it.  The BIOS's
	    ordinary warm boot does this through pgrelall(), but a background
	    process never reaches it -- presume() below does not return -- and
	    a descriptor index that has been freed and reused would otherwise
	    inherit slots the allocator still thinks are spoken for.  */
	pgrelproc();

	nxt = pnextany(pcur);		/* NOT pnext(): see pnextany()	*/
	seg = pd[nxt].pd_seg;

	pgtpaswap(seg);			/* nxt's image becomes the TPA and
					   the dead one goes out to seg	*/
	pd[nxt].pd_seg = 0;
	pghold(seg, 0);
	pgfree(seg);			/* the dead page, back to the pool */

	if (pd[pcur].pd_split)
		pnsplit--;
	punblk();			/* a process cannot die blocked: the
					   count is resident and the slot is
					   about to be handed to somebody */
	pd[pcur].pd_state = PS_FREE;
	pd[pcur].pd_seg   = 0;
	pnlive--;
	PCUR(nxt);
	psched = (pnlive > 1);
	pload(&pd[nxt]);
	sysstk = pd[nxt].pd_stk;
	pquant = pd[nxt].pd_quant;
	if (pd[nxt].pd_inc)
		pcoresume(map_adr((XADDR)&pd[nxt].pd_f, 0), pd[nxt].pd_ssp);
	presume(map_adr((XADDR)&pd[nxt].pd_f, 0));	/* no return */
	return (1);			/* not reached */
}



GLOBAL WORD proccnt()
{
	return (pnlive ? pnlive : 1);
}



/*  All three call padopt() first.  A program that asks which console it
    is on may be the only thing that has ever run -- no fn 144 has
    happened, so the table is still bss zeros and descriptor 0 does not
    yet say PS_LIVE.  padopt() is the routine whose whole job is making
    the table describe the machine, and it is idempotent, so the answer
    is right instead of being right only after somebody has forked.  */

GLOBAL WORD pconget()
{
	padopt();
	return (pd[pcur].pd_con);
}

GLOBAL WORD pconset(con)
WORD	con;
{
	padopt();
	pd[pcur].pd_con = con;
	return (con);
}

GLOBAL WORD pconname(name, con)
BYTE	*name;
WORD	con;
{
	REG WORD i, j;

	padopt();

	for (i = 0; i < PNPROC; i++) {
		if (pd[i].pd_state != PS_LIVE)
			continue;
		for (j = 0; j < 8; j++)
			if (pd[i].pd_name[j] != name[j])
				break;
		if (j == 8) {
			pd[i].pd_con = con;
			return (1);
		}
	}
	return (0);
}
