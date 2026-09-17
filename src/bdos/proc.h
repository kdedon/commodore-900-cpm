/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/*
 * proc.h -- the process descriptor and the shapes the switch moves.
 *
 * A "process" here is one 64 KB TPA image plus the state that does NOT
 * travel with that image.  Everything in this header exists because the
 * page swap (src/bios/pgalloc.c pgtpaswap) moves memory and nothing
 * else; src/bdos/proc.c is the part that moves a process.
 */

/* SC/timer frame layout, shared with bdosglue.s and trap.s.
   r0-r13 precede the hardware frame; NSPSEG/NSPOFF are saved separately. */

struct pframe {
	short	pf_reg[14];	/*  0..27  r0-r13			*/
	short	pf_id;		/*  28	   trap identifier (junk on IRET) */
	short	pf_fcw;		/*  30	   caller FCW			*/
	short	pf_pcseg;	/*  32	   caller PC segment word, 0xSS00 */
	short	pf_pcoff;	/*  34	   caller PC offset		*/
	short	pf_nspseg;	/*  36	   NSPSEG			*/
	short	pf_nspoff;	/*  38	   NSPOFF			*/
};

/* Loader context consumed by xfer_ and converted to pframe by proc.c.
   Assembly layout: regs 0..30, ignore 32, FCW 34, PC 36. */

struct context {
	short	regs[16];
	short	ignore;
	short	FCW;
	XADDR	PC;
};

/*  Descriptor capacity; each process also needs a TPA page and a supervisor
    stack, and both of those are the reason this number is not larger.

    SIX, raised from four by owner decision 2026-09-12, which puts the
    ceiling at FIVE user processes because the cold-boot session on the
    second console holds one descriptor for as long as the machine is up.
    The three things that had to be true for six, all checked rather than
    assumed:

      * SUPERVISOR STACKS.  PSTKOF(i) = PSTKTOP - i*PSTKSZ in segment 0x3F,
	so six stacks run 0xFC00 down to 0x3C00 and the lowest one's frames
	live in 0x3C00..0x5C00.  Segment 0x3F is a full 64 KB page (crt.s
	maps it at phys 0x0D0000 with limit 0xFF) and holds NOTHING but
	these stacks -- the ROM's own routines frame below the running SP in
	the same segment, which is why the C stack lives there at all -- so
	0x3C00 of headroom remains under the lowest stack.  SEVEN fit, not
	eight: 0xFC00/0x2000 is 7.875, so an eighth slot would get 0x1C00.
	Six is what the decision asked for.
      * TPA PAGES.  One live process owns segment TPASEG and the rest are
	parked in allocator slots, so six processes need five pool slots.
	pginit() gives as many slots as RAM backs, up to PGNSLOT (c900cfg.h):
	seven on the 1024 KB machine the suite runs, where verify-i86 already
	holds six at once, and 31 on 2560 KB (tests/pgtest.c).
      * DESCRIPTOR INDEX.  Every loop in proc.c is written against PNPROC
	and pgalloc.c's pgown[] holds pgcur+1 in a char, so an index of 5
	costs nothing.  No table in the tree is dimensioned by a literal 4
	and no protocol carries a process count.

    Each descriptor costs one struct pdesc of BSS (the struct stvars copy
    inside it is most of that) and 8 KB of segment 0x3F, which is not
    resident text.  */

#define	PNPROC		6

/* Ask the BIOS for the runtime console count when validating console numbers. */

#define	PNCON		bconcnt()

/*  Process states.  PS_FREE is zero so that a bss-cleared table is a
    table of free slots and nothing has to initialise it.

    PS_RSVD is a slot pcrgen() has CLAIMED but not yet built.  It exists
    because creation yields: plock() parks the creator when another
    process holds the filesystem lock, and until the claim was written
    down a second creator resuming in that window picked the same slot
    and both of them built a process in it.  Every other loop in proc.c
    asks for PS_LIVE, so a reserved slot is invisible to scheduling,
    wakeups and death -- it is only invisible to the free-slot search
    that PS_FREE answers, which is the whole point.  */

#define	PS_FREE		0
#define	PS_LIVE		1
#define	PS_RSVD		2

/* Wait reasons: flag/queue/lock/ownership changes signal waiters; console
 * input and tick deadlines are polled by the scheduler. */

#define	PW_RUN		0	/* runnable				*/
#define	PW_TICK		1	/* until tickget() reaches pd_wtick	*/
#define	PW_FLAG		2	/* for flag pd_wobj to be set		*/
#define	PW_QRD		3	/* for queue pd_wobj to be non-empty	*/
#define	PW_QWR		4	/* for queue pd_wobj to have room	*/
#define	PW_CON		5	/* for a character on console pd_wobj	*/
#define	PW_LOCK		6	/* for the file-system lock to be free	*/
#define	PW_CATT		7	/* for console pd_wobj to be DETACHED by
				   whoever owns it (C10).  SIGNALLED, not
				   polled: the event is another process's
				   fn 147, or its warm boot, and proc.c
				   pcondet()/pconrel() hand the console
				   STRAIGHT to one waiter rather than
				   leaving it free for a race.		*/

struct pdesc {
	short	pd_state;	/* PS_FREE / PS_LIVE / PS_RSVD		*/
	short	pd_seg;		/* the pool segment PARKING this process's
				   64 KB image, or 0 while this process is
				   the one segment TPASEG points at.  Exactly
				   one live descriptor may hold 0.	*/
	short	pd_split;	/* this process is a split-I/D program:
				   splitld.c's spflag, saved.  Its two banks
				   are single fixed pages that no swap moves,
				   which is why pcreate() allows at most one
				   such process at a time.		*/
	short	pd_con;		/* console this process is attached to.  The
				   C900 has three console devices and the
				   BIOS drives one, so this is 0 for every
				   process today and is here because the
				   moment there are two it is per-process. */
	char	pd_name[8];	/* the 8-character process name MP/M's
				   Assign Console (fn 149, NUCLEUS/TH.ASM
				   assign:) matches on.  Filled from the
				   FCB pcreate() loaded from; the adopted
				   process 0 has no file to be named after
				   and carries eight blanks, which is a
				   name a caller can still ask for.	*/
	short	pd_sess;	/* session: reload the CCP at warm boot instead of freeing this process */
	short	pd_prio;	/* MP/M's priority byte.  Recorded, not yet
				   consulted: the ready list is round robin
				   and stays that way until preemption (S5)
				   makes priority mean something.	*/
	XADDR	pd_dma0;	/* basepage.buff address restored by function 13 */
	struct pframe pd_f;	/* where it was, and what it was doing	*/
	struct stvars pd_gbl;	/* the BDOS's per-process state.  DRI factored
				   it out and said so (bdosdef.h struct stvars,
				   "so that each process can have a separate
				   dirbuf"); at a gate-boundary switch it is
				   correct to move it by copy, because no
				   process is ever inside the BDOS when it is
				   not the running one.			*/
	UWORD	pd_rsxhead;	/* rsx.c's chain head, a TPA offset	*/
	UWORD	pd_rsxtop;	/*   and the resident fence		*/
	XADDR	pd_tpalp, pd_tpalt;	/* bdosmain.c's TPA bounds, which  */
	XADDR	pd_tpahp, pd_tpaht;	/*   rsxfence()/warmboot() move	   */
	short	pd_stk;		/* THE TOP OF THIS PROCESS'S SUPERVISOR
				   STACK, an offset in segment 0x3F.  This
				   is what S4 adds to S3: a process parked
				   INSIDE a BDOS call has live C frames, and
				   they have to be somewhere the process
				   that runs next will not write.  Fixed per
				   descriptor index (PSTKTOP - i*PSTKSZ), so
				   it never moves under a parked frame. */
	short	pd_ssp;		/* the supervisor SP this process was
				   parked at, valid only while pd_inc.
				   procasm.s's pcopark_ writes it.	*/
	short	pd_inc;		/* parked INSIDE a BDOS call: resume by
				   returning from pcopark_ (pcoresume_)
				   rather than by rebuilding pd_f and
				   IRETing (presume_).  Zero means parked at
				   the gate, which is S3's only case.	*/

	/*  THE WAIT LIST, AND IT IS AT THE END ON PURPOSE.  Every earlier
	    field was appended in the middle of what came before it, and a
	    mid-struct insertion is currently under suspicion for a
	    regression on task/V2 -- nothing in this tree reaches a pdesc
	    by a hard-coded offset, but the suspicion is cheap to respect
	    and costs nothing to honour, so these three go last.	*/

	short	pd_wait;	/* PW_RUN, or what this process is waiting
				   for.  Nonzero is what pnext() SKIPS, and
				   that skip is the whole of C5: a blocked
				   process is out of the rotation instead
				   of being handed a slice it will spend
				   re-testing a condition.		*/
	short	pd_wobj;	/* which one: the flag number, the queue
				   id, or the console.			*/
	long	pd_wtick;	/* PW_TICK's deadline, in tick900.c's own
				   counter.  A LONG because that counter is
				   one, and compared by SUBTRACTION so that
				   a wrap is not a stuck wait -- the same
				   rule xdos.c xdelay() already used.	*/

	/*  THE QUANTUM, in ticks, and it is at the end for the same
	    reason the wait fields are.  Filled once -- padopt() for the
	    adopted process 0, pcrgen() for a child -- from pd_prio, so
	    the three hand-over points (pgone, procdead, pdisp) only
	    assign it into `pquant' and the tick only decrements.	*/

	short	pd_quant;	/* how many ticks a slice of THIS process is
				   worth.  pqfor(pd_prio), proc.c.	*/

	/* Ownership is a bitmask because selecting another console does not
 * release earlier attachments. The descriptor table is the ownership map. */

	short	pd_catt;	/* bit n set: this process has ATTACHED
				   console n (XDOS 146) and has not
				   detached it (147).			*/
	short	pd_chome;	/* home console retained across session warm boots */
};

/* PNPROC 8 KB supervisor stacks occupy segment 3F from its top downwards
 * (six of them today: 0xFC00 down to 0x3C00).  Stack tops are fixed per
 * descriptor so parked C frames do not move. */

#define	PSTKTOP		0xfc00
#define	PSTKSZ		0x2000
#define	PSTKOF(i)	((short)(PSTKTOP - (i) * PSTKSZ))

/* Priority selects slice length in four bands; runnable order stays
 * round robin. PQBASE is the priority-zero slice length in ticks. */

#define	PQBASE		5

/*  pcreate() return codes.  0 is success; everything else is a refusal
    the caller can print, and every refusal is a fact about the machine
    or about this stage rather than a policy.  Values above 4 stay clear
    of pgmld.c's own 1..4 (BADHDR/NOMEM/READERR/other), which pcreate
    passes straight through when it is the load that failed.  */

/*  The one loader code pcreate() does NOT pass through: the loader's
    refusal of a second split-I/D program, which fn 144 has always
    reported as PC_SPLIT.  pgmld.c's NOSPLIT, same value.  */

#define	PL_NOSPLIT	4

#define	PC_OK		0
#define	PC_NOPD		5	/* no free process descriptor		*/
#define	PC_NOPAGE	6	/* no free 64 KB page: a 512 KB machine	*/
#define	PC_SPLIT	7	/* a second split-I/D program		*/
#define	PC_NOCON	8	/* fn 142: no such console		*/

/*  psched -- nonzero when the gate must call pdisp() on its way out.
    Read by sys/bdosglue.s at the SC #2 return, so it is a WORD and its
    name is what the assembler sees.  Zero whenever fewer than two
    processes are live, which is every system this port has shipped, and
    the whole cost of this stage on that system is one load, one test and
    one taken branch per BDOS call.  */

EXTERN short	psched;

/*  sysstk -- the offset the system stack is reset to.  It was the
    literal 0xFC00 in three places (src/bios/glue.s xfer_ and ccpentry_,
    src/bdos/procasm.s presume_); it is a word now because "the top of
    the system stack" is a per-process fact the moment a process can be
    parked with frames on one.  Set by proc.c at every switch and never
    read anywhere else.  Its initial value is PSTKTOP, so before any
    second process exists it is the constant it replaced.	*/

EXTERN short	sysstk;
