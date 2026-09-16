/* MP/M-style memory, polling, flag, queue, delay and console calls.
 * pwait() parks a process with a wait reason; the scheduler makes it ready
 * when that condition holds. Flag and queue operations fail with 0xFF if
 * no other live process can satisfy them. Timer and console waits can
 * complete with a single process.
 * Function 145 is this port's process-count query, not MP/M Set Priority. */

#include "stdio.h"
#include "bdosdef.h"
#include "biosdef.h"
#include "proc.h"

EXTERN	WORD	pyield();	/* proc.c: give the machine away, come back */
EXTERN	WORD	pwait();	/* proc.c: wait on a scheduler event */
EXTERN	VOID	pwake();	/* proc.c: put the waiters for one event
				   back in the rotation			*/
EXTERN	WORD	proccnt();	/* proc.c: live process count */
EXTERN	WORD	pconget();	/* proc.c: the running process's console    */
EXTERN	WORD	pconset();	/*   and setting it				*/
EXTERN	WORD	pconname();	/* proc.c: set a NAMED process's console    */
EXTERN	WORD	pconatt();	/* proc.c: 146, claim it -- blocks	    */
EXTERN	WORD	pcondet();	/* proc.c: 147, give it back		    */

/*  BIOS function 24 is the 100 Hz period tick (bios900.c case 24) and
    function 25 is the 64 KB segment allocator (case 25, the pgalloc.c
    pool).  Both are reached through bios(), the BDOS's only door into
    the BIOS -- this file does not call pgalloc() or tickget() directly,
    because the BDOS has never called a BIOS C function by name.	*/

#define	BTICK		24
#define	BSEG		25
#define	SEG_GET		0
#define	SEG_PUT		1
#define	SEG_COUNT	2

#define	XFAIL		0x00ff		/* MP/M's A = 0FFH		*/
#define	XOK		0

/*  How many consoles.  proc.h PNCON, which is now bconcnt() -- BIOS
    function 29, asked at the moment of the check.  It was the literal 1
    until C3 gave the BIOS SCC channel A and made it 2, and it is a
    constant no longer: C4 sizes the BIOS's console table from the serial
    map the loader hands over, so the number is the machine's, not this
    file's.  Function 148 accepts exactly the consoles that exist on the
    machine it is running on, which is what a compiled-in number could
    only manage on one machine.  */
#define	XNCON		PNCON

/* Delay in 100 Hz ticks. Subtract the deadline to tolerate counter wrap.
 * Zero ticks or an unavailable time base return immediately. */

GLOBAL WORD xdelay(ticks)
UWORD	ticks;
{
	REG LONG	dl;

	if (ticks == 0)
		return (XOK);
	dl = bios(BTICK, 0L, 0L);
	if (dl == 0L)
		return (XOK);		/* no tick: no time base to wait on */
	dl += (LONG)ticks;

	while ((bios(BTICK, 0L, 0L) - dl) < 0L)
		pwait(PW_TICK, 0, dl);
			/* Wait for the deadline; an otherwise idle system polls until it expires. */
	return (XOK);
}


/* Device poll: console input waits; console/list output are always ready. */

GLOBAL WORD xpoll(dev)
UWORD	dev;
{
	if (dev > 2)
		return (XFAIL);		/* no such device */
	if (dev != 0)
		return (XOK);		/* an output device is always ready */

	while ( ! bconstat())
		pwait(PW_CON, (WORD)concur, 0L);
			/* Out of the rotation until a character reaches
			   THIS process's console.  Same wait conbdos.c
			   getch() takes, and woken the same way: there is
			   no console interrupt on this machine, so
			   proc.c pwscan() polls.  FALSE keeps going, for
			   xdelay()'s reason -- the key comes from outside
			   and no process in here can produce it.	*/
	return (XOK);
}


/* Eight flags with one waiter per flag. Waiting consumes a set flag;
 * a second waiter or an unconsumed repeated set is refused. */

#define	XNFLAG	8

#define	FL_CLEAR	0
#define	FL_SET		1

MLOCAL	WORD	xflag[XNFLAG];		/* FL_CLEAR / FL_SET, bss-zero is
					   FL_CLEAR, which is right	*/
MLOCAL	WORD	xfwait[XNFLAG];		/* 0 or 1: the single waiter	*/

GLOBAL WORD xflgwt(n)
UWORD	n;
{
	if (n >= XNFLAG)
		return (XFAIL);
	if (xflag[n] == FL_SET) {	/* set already: take it, no block */
		xflag[n] = FL_CLEAR;
		return (XOK);
	}
	if (xfwait[n] != 0)
		return (XFAIL);		/* FLAG.ASM's "flag under run"	*/

	xfwait[n] = 1;
	while (xflag[n] != FL_SET) {
		/* Refuse when nobody else can run. This also refuses a wait that could
 * eventually be satisfied by a peer currently blocked on external input. */
		if (proccnt() < 2 || ! pwait(PW_FLAG, (WORD)n, 0L)) {
			xfwait[n] = 0;
			return (XFAIL);
		}
	}
	xflag[n] = FL_CLEAR;
	xfwait[n] = 0;
	return (XOK);
}

GLOBAL WORD xflgset(n)
UWORD	n;
{
	if (n >= XNFLAG)
		return (XFAIL);
	if (xflag[n] == FL_SET && xfwait[n] == 0)
		return (XFAIL);		/* FLAG.ASM's "flag over run"	*/
	xflag[n] = FL_SET;
	pwake(PW_FLAG, (WORD)n);	/* the waiter is out of the rotation:
					   this call is the only thing that
					   can put it back		*/
	return (XOK);
}


/* Queues reside in supervisor memory because process TPA addresses alias.
 * Requests resolve names to integer IDs. Each queue has bounded message
 * size/depth; conditional reads and writes never wait. */

#define	XNQ	4			/* queues in the system		*/
#define	XQNAME	8			/* MP/M's name length		*/
#define	XQMSGMX	16			/* longest message		*/
#define	XQDEPTH	4			/* messages a queue holds	*/

struct xq {
	WORD	q_used;			/* 0 = free slot		*/
	UBYTE	q_name[XQNAME];
	WORD	q_msglen;		/* 1..XQMSGMX			*/
	WORD	q_ndep;			/* 1..XQDEPTH			*/
	WORD	q_cnt;			/* messages in it now		*/
	WORD	q_in, q_out;		/* the ring's two ends		*/
	UBYTE	q_buf[XQDEPTH][XQMSGMX];
};

MLOCAL	struct xq	xq[XNQ];

/*  The function 134 request, and the 135 one, as they arrive from the
    caller's TPA.  Copied in whole, because a far pointer may only be
    read through cpy_in (tests/farptrcheck.py enforces exactly that).  */

struct xqmake {
	UBYTE	qm_name[XQNAME];
	WORD	qm_msglen;
	WORD	qm_ndep;
};

struct xqopen {
	UBYTE	qo_name[XQNAME];
	WORD	qo_id;			/* filled in by function 135	*/
};

/*  137-140's block: the queue id and the message buffer, in the caller's
    memory.  A read copies out into it and a write copies in from it.
    XQMSGMX bytes always, whatever the queue's own message length -- a
    program that made a 2-byte queue still declares the full block, which
    is what makes the block one fixed shape for all four calls.	*/

struct xqmsg {
	WORD	qx_id;
	UBYTE	qx_msg[XQMSGMX];
};

MLOCAL WORD xqfind(name)
REG UBYTE	*name;
{
	REG WORD	i, j;

	for (i = 0; i < XNQ; i++) {
		if ( ! xq[i].q_used)
			continue;
		for (j = 0; j < XQNAME; j++)
			if (xq[i].q_name[j] != name[j])
				break;
		if (j == XQNAME)
			return (i);
	}
	return (-1);
}

/*  Function 134, Make Queue (QUEUE.ASM:84-227).  Making a queue that
    already exists is not an error in MP/M -- makeq walks QLR and returns
    if the name is there -- and it is not one here either.  */

GLOBAL WORD xqmakef(infop)
XADDR	infop;
{
	LOCAL struct xqmake	m;
	REG struct xq		*q;
	REG WORD		i, j;

	cpy_in(infop, &m, (long)sizeof m);
	if (m.qm_msglen < 1 || m.qm_msglen > XQMSGMX)
		return (XFAIL);
	if (m.qm_ndep < 1 || m.qm_ndep > XQDEPTH)
		return (XFAIL);
	if (xqfind(m.qm_name) >= 0)
		return (XOK);		/* already made: MP/M says nothing */

	for (i = 0; i < XNQ; i++)
		if ( ! xq[i].q_used)
			break;
	if (i >= XNQ)
		return (XFAIL);		/* no free queue slot		*/

	q = &xq[i];
	for (j = 0; j < XQNAME; j++)
		q->q_name[j] = m.qm_name[j];
	q->q_msglen = m.qm_msglen;
	q->q_ndep   = m.qm_ndep;
	q->q_cnt    = 0;
	q->q_in     = 0;
	q->q_out    = 0;
	q->q_used   = 1;
	return (XOK);
}

/*  Function 135, Open Queue (QUEUE.ASM:243-318): resolve a name.  MP/M
    fills the user QCB with the system QCB's address; this fills it with
    the queue's index, which is the same fact in the size this system's
    queues actually need.  */

GLOBAL WORD xqopenf(infop)
XADDR	infop;
{
	LOCAL struct xqopen	o;
	REG WORD		i;

	cpy_in(infop, &o, (long)sizeof o);
	if ((i = xqfind(o.qo_name)) < 0)
		return (XFAIL);
	o.qo_id = i;
	cpy_out(&o, infop, (long)sizeof o);
	return (XOK);
}

/*  Function 136, Delete Queue (QUEUE.ASM:333-403).  Any message still in
    it goes with it, which is MP/M's behaviour: deletq unlinks the QCB and
    does not drain it.  A process blocked in 137 or 139 on this queue sees
    q_used go to zero on its next trip round the robin and fails, rather
    than reading a slot that is now somebody else's queue.  */

GLOBAL WORD xqdelf(infop)
XADDR	infop;
{
	LOCAL struct xqopen	o;
	REG WORD		i;

	cpy_in(infop, &o, (long)sizeof o);
	if ((i = xqfind(o.qo_name)) < 0)
		return (XFAIL);
	xq[i].q_used = 0;
	pwake(PW_QRD, i);		/* a reader and a writer blocked on
					   this queue would otherwise wait for
					   an event that can no longer happen:
					   wake them to find q_used zero and
					   fail, which is what the comment
					   above promises		*/
	pwake(PW_QWR, i);
	return (XOK);
}

/*  137 Read Queue / 138 Conditional Read Queue.  `cond' is TRUE for 138:
    return 0FFh instead of blocking on an empty queue.  QUEUE.ASM:477-752
    and 767-823.  */

GLOBAL WORD xqread(infop, cond)
XADDR	infop;
WORD	cond;
{
	LOCAL struct xqmsg	x;
	REG struct xq		*q;
	REG WORD		i, n;

	cpy_in(infop, &x, (long)sizeof x);
	if (x.qx_id < 0 || x.qx_id >= XNQ || ! xq[x.qx_id].q_used)
		return (XFAIL);
	q = &xq[x.qx_id];

	while (q->q_cnt == 0) {
		if (cond)
			return (XFAIL);		/* 138: never blocks	*/
		if (proccnt() < 2 || ! pwait(PW_QRD, x.qx_id, 0L))
			return (XFAIL);		/* nobody else can write:
						   xflgwt() above says why
						   this is now two tests */
		if ( ! q->q_used)
			return (XFAIL);		/* deleted under us	*/
	}

	n = q->q_msglen;
	for (i = 0; i < n; i++)
		x.qx_msg[i] = q->q_buf[q->q_out][i];
	if (++q->q_out >= q->q_ndep)
		q->q_out = 0;
	q->q_cnt--;
	pwake(PW_QWR, x.qx_id);		/* room in it now: a 139 blocked on a
					   full queue can go		*/
	cpy_out(&x, infop, (long)sizeof x);
	return (XOK);
}

/*  139 Write Queue / 140 Conditional Write Queue.  QUEUE.ASM:874-1124 and
    1139-1209.  */

GLOBAL WORD xqwrite(infop, cond)
XADDR	infop;
WORD	cond;
{
	LOCAL struct xqmsg	x;
	REG struct xq		*q;
	REG WORD		i, n;

	cpy_in(infop, &x, (long)sizeof x);
	if (x.qx_id < 0 || x.qx_id >= XNQ || ! xq[x.qx_id].q_used)
		return (XFAIL);
	q = &xq[x.qx_id];

	while (q->q_cnt >= q->q_ndep) {
		if (cond)
			return (XFAIL);		/* 140: never blocks	*/
		if (proccnt() < 2 || ! pwait(PW_QWR, x.qx_id, 0L))
			return (XFAIL);		/* nobody else can read:
						   xflgwt() above says why
						   this is now two tests */
		if ( ! q->q_used)
			return (XFAIL);
	}

	n = q->q_msglen;
	for (i = 0; i < n; i++)
		q->q_buf[q->q_in][i] = x.qx_msg[i];
	if (++q->q_in >= q->q_ndep)
		q->q_in = 0;
	q->q_cnt++;
	pwake(PW_QRD, x.qx_id);		/* THE WAKEUP THE BRIEF NAMES: a
					   message in the queue is what a 137
					   blocked on an empty one is waiting
					   for, and it is out of the rotation
					   until this line runs		*/
	return (XOK);
}


/* Map MP/M-style memory requests onto BIOS 64 KB page allocation.
 * md_base is a logical segment number; only one-page requests are supported. */

struct xmd {
	WORD	md_base;		/* logical segment number	*/
	WORD	md_size;		/* 64 KB pages			*/
	WORD	md_attrib;		/* MP/M's attribute word, kept
					   so the block is MP/M's shape;
					   nothing here reads it	*/
	WORD	md_bank;		/* MP/M's bank; always 0	*/
};

/*  Function 129, Relocatable Memory Request: "any page will do".  */

GLOBAL WORD xmemrq(infop, absolute)
XADDR	infop;
WORD	absolute;
{
	LOCAL struct xmd	md;
	REG WORD		seg;

	cpy_in(infop, &md, (long)sizeof md);
	if (md.md_size < 1 || md.md_size > 1)
		return (XFAIL);		/* one page is the only size	*/

	if ((seg = (WORD)bios(BSEG, (long)SEG_GET, 0L)) == 0)
		return (XFAIL);		/* none left			*/

	/*  Function 128, Absolute Memory Request, wants THIS base and no
	    other.  There is no way to ask the pool for a named page, so
	    ask for one and give it back if it is the wrong one -- which
	    is the same answer absrq gives (a base that is not free is a
	    refusal) reached by the only route this allocator has.  */

	if (absolute && seg != md.md_base) {
		bios(BSEG, (long)SEG_PUT, (long)seg);
		return (XFAIL);
	}

	md.md_base  = seg;
	md.md_size  = 1;
	md.md_bank  = 0;
	cpy_out(&md, infop, (long)sizeof md);
	return (XOK);
}

/*  Function 130, Memory Free.  MEMMGR.ASM's memfr documents no return;
    pgfree() has one worth passing on -- FALSE for a segment that was
    never ours, which includes a double free.  */

GLOBAL WORD xmemfr(infop)
XADDR	infop;
{
	LOCAL struct xmd	md;

	cpy_in(infop, &md, (long)sizeof md);
	if (bios(BSEG, (long)SEG_PUT, (long)md.md_base) == 0L)
		return (XFAIL);
	return (XOK);
}


/* Select/query a process's console; assignment resolves an eight-byte name.
 * Validate console numbers against the BIOS runtime count. */

/*  Function 149's parameter: TH.ASM:243-247's nine bytes, a console
    number and an 8-character name.  */

struct xassign {
	UBYTE	xa_con;
	UBYTE	xa_name[XQNAME];
};

GLOBAL WORD xsetcon(info)
UWORD	info;
{
	REG WORD	n;

	n = info & 0x0f;		/* the low nibble, as XDOS.ASM does */
	if (n >= XNCON)
		return (XFAIL);
	pconset(n);
	return (XOK);
}

GLOBAL WORD xgetcon()
{
	return (pconget());
}

GLOBAL WORD xassigncon(infop)
XADDR	infop;
{
	LOCAL struct xassign	a;
	REG WORD		n;

	cpy_in(infop, &a, (long)sizeof a);
	n = UBWORD(a.xa_con) & 0x0f;
	if (n >= XNCON)
		return (XFAIL);
	return (pconname(a.xa_name, n) ? XOK : XFAIL);
}


/* Attach/detach the selected console. Attach blocks behind another owner;
 * detach fails if the caller did not own it. */

GLOBAL WORD xconatt()
{
	return (pconatt(pconget()) ? XOK : XFAIL);
}

GLOBAL WORD xcondet()
{
	return (pcondet(pconget()) ? XOK : XFAIL);
}
