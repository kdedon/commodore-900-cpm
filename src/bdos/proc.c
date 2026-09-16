
#include "stdio.h"		/* Standard declarations		*/
#include "bdosdef.h"		/* struct stvars, GBL/BSETUP		*/
#include "biosdef.h"		/* cpy_in / cpy_out			*/
#include "c900cfg.h"		/* TPASEG, TPABASE			*/
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
		pnlive = 1;
		sysstk = pd[0].pd_stk;		/* which is PSTKTOP, the value
						   it already held	*/
	}
}



XADDR	infop;
{
	REG struct pdesc *me, *kid;
	REG WORD	i, k, seg;
	struct context	ctx;

	padopt();
	me = &pd[pcur];

	for (i = 0; i < PNPROC; i++)
		if (pd[i].pd_state == PS_FREE)
			break;
	if (i >= PNPROC)
		return (PC_NOPD);
	kid = &pd[i];


	/*  A page.  Zero means the pool is empty, which on a 512 KB
	    machine is always and by construction (pgalloc.c): there the
	    answer to "run a second program" is no, and it is a hardware
	    answer.  */
		return (PC_NOPAGE);

	plock();

	psave(me);			/* the caller, as it stands now	*/

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
		k = ldimage(map_adr((XADDR)pq.pq_fcb, 0), (WORD)pq.pq_tlen,
			    pq.pq_tail, pqfcb1, pqfcb2, &ctx);
	}

	/*  A second split-I/D program would want a second data bank and
	    a second side table, and there is one of each (c900cfg.h).
	if (k == 0 && spflag && pnsplit > 0)
		k = PC_SPLIT;

	/*  Whatever happened, the caller's page comes back.  On failure
	    the child's page goes back to the pool with it.  */
	pgtpaswap(seg);
	me->pd_seg = 0;
	pnoyld = 0;
	punlock();

	if (k != 0) {
		pgfree(seg);
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
