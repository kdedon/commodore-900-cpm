/* RSX modules intercept SC 2 through a TPA chain. rsxtop fences program
 * loads below resident images; warm boot removes flagged modules.
 * Images have fixed link origins and are attached by GENCOM loading
 * or function 60. */

#include "stdio.h"		/* Standard I/O declarations		*/

#include "bdosdef.h"		/* Type and structure declarations	*/

#include "biosdef.h"		/* cpy_in / cpy_out			*/

#include "c900cfg.h"		/* TPASEG / TPABASE			*/

#include "rsxhdr.h"		/* the module prefix and fn-60 block	*/

EXTERN UBYTE	serial[];	/* system serial number (bdosmisc.c)	*/
EXTERN XADDR	tpa_ht;		/* TPA upper boundary (bdosmain.c)	*/
EXTERN XADDR	tpa_hp;

/*  The ceiling every module stacks down from.  v3's is `osbase' -- the
    base of the operating system, one page above the top of the TPA
    (ccp3.asm:1798-1800, `lhld osbase ! dcr h').  Ours is the top of the
    segment itself, because none of the system's own state lives in the
    TPA any more: the CCP's state page used to be reserved at 0xFA00 and
    is resident per-process storage now (src/ccp/ccpsv.h).

    ZERO MEANS 0x10000.  rsxtop is a UWORD of TPA offsets and the top of
    the segment does not fit in one, so the empty-chain value is 0 and
    every reader turns it back into SEGLEN.  rsxchk() already had that
    convention.  Nothing else compares rsxtop without going through those
    readers except the SC-2 gate (src/bdos/bdosglue.s rsxenter), and the
    gate is reached only when rsxhead is non-zero -- that is, only when a
    module really is resident and rsxtop really is its base.  */

#define	RSXCEIL		((UWORD)0)	/* the whole segment: see above	*/

#define	SEGLEN		0x10000L	/* one Z8001 segment		*/
#define	BPLEN		256		/* sizeof (struct b_page)	*/
#define	DEFSTACK	0x100		/* pgmld.c's default stack	*/

/* TPA offsets: rsxhead is the newest/lowest module (zero if empty);
 * rsxtop is its base, or 0 -- meaning the top of the segment -- when empty. */

GLOBAL UWORD	rsxhead = 0;
GLOBAL UWORD	rsxtop = RSXCEIL;


/****************************************************
**
** rsxres() -- bytes reserved at the top of the TPA.
**		pgmld.c subtracts this from every
**		segment limit it computes.
**
****************************************************/

UWORD rsxres()
{
    if (rsxtop == 0)		/* no module: nothing is reserved, and
				   the whole segment is the program's	*/
	return (0);
    return ((UWORD)(SEGLEN - (long)rsxtop));
}


/****************************************************
**
** rsxfence() -- republish the fence: tpa_ht/tpa_hp,
**		and through them @MXTPA (scb.c:191).
**		This is `fixchain2' + `setmaxb'
**		(loader3.asm:302-314).
**
****************************************************/

MLOCAL VOID rsxfence()
{
    tpa_hp = tpa_ht = TPABASE + (rsxtop ? (long)rsxtop : SEGLEN);
}


/****************************************************
**
** rsxget()/rsxput() -- read or write one module's
**		prefix.  The modules live in the TPA
**		segment, which the system reaches only
**		as data through mem_cpy.
**
****************************************************/

MLOCAL VOID rsxget(off, hp)
UWORD off;
struct rsxhdr *hp;
{
    cpy_in(TPABASE + (long)off, hp, (long)RSXHDRLEN);
}

MLOCAL VOID rsxput(off, hp)
UWORD off;
struct rsxhdr *hp;
{
    cpy_out(hp, TPABASE + (long)off, (long)RSXHDRLEN);
}


/* Check placement before copying the image below the current RSX fence,
 * leaving space for the base page and default stack. */

GLOBAL UWORD rsxchk(org, len)
UWORD org;
UWORD len;
{
    XADDR	top;

    if (len < RSXHDRLEN || org == 0)
	return (RSX_EPB);

    /*  The room the running program needs is its base page and stack,
	which pgmld put directly below the current fence.  */

    top = rsxtop ? (XADDR)rsxtop : SEGLEN;
    if ((XADDR)org + (XADDR)len > top - BPLEN - DEFSTACK)
	return (RSX_EROOM);
    return (0);
}


/* Validate the loaded prefix, link the image at the chain head and
 * publish the lowered TPA fence. */

GLOBAL UWORD rsxlink(org, len)
UWORD org;
UWORD len;
{
    struct rsxhdr	h;
    struct rsxhdr	oldh;
    REG WORD		i;

    rsxget(org, &h);
    if (h.magic != RSXMAGIC)
	return (RSX_EMAGIC);
    if (h.org != org || h.len != len)
	return (RSX_EORG);

    /*  The module arrives unlinked and the loader owns every chain
	field.  */

    for (i = 0; i < 6; i++)		/* loader3.asm:276-280	*/
	h.serial[i] = serial[i];
    h.prev = 0;
    h.next = rsxhead;			/* 0 = the BDOS itself	*/
    h.endchain = (UBYTE)(rsxhead ? 0 : 0xff);
    rsxput(org, &h);

    if (rsxhead) {			/* chain the old head back */
	rsxget(rsxhead, &oldh);
	oldh.prev = org;
	rsxput(rsxhead, &oldh);
    }

    rsxhead = org;
    rsxtop = org;
    rsxfence();
    return (0);
}


/****************************************************
**
** rsxattach() -- function 60 sub-function 127.
**		Place, check and link one module.
**
****************************************************/

MLOCAL UWORD rsxattach(pb)
REG struct rsxpb *pb;
{
    REG UWORD	r;

    if ((r = rsxchk(pb->rporg, pb->rplen)) != 0)
	return (r);
    mem_cpy(pb->rpsrc, TPABASE + (long)pb->rporg, (long)pb->rplen);
    return (rsxlink(pb->rporg, pb->rplen));
}


/* Handle unclaimed function-60 calls: attach, query, or return not handled. */

UWORD rsxfn(param, xparam)
UWORD param;
XADDR xparam;
{
    struct rsxpb	pb;

    cpy_in(xparam, &pb, (long) sizeof pb);
    if (pb.rpfunc == RSX_ATTACH)
	return (rsxattach(&pb));
    if (pb.rpfunc == RSX_QUERY)
	return (rsxhead);
    return (RSX_NOTHANDLED);
}


/****************************************************
**
** rsxwboot() -- drop the modules flagged for removal.
**		`rsx$chain', loader3.asm:326-380.
**
****************************************************/

VOID rsxwboot()
{
    struct rsxhdr	h;
    REG UWORD		cur, next;
    REG UWORD		keep, low;

    if (rsxhead == 0)
	return;

    /*  v3 unlinks in place, patching the neighbours' next/prev fields.
	Walking once and rebuilding the chain from the survivors reaches
	the same state with no case analysis: the order is the memory
	order, which is the only order the chain ever has.  */

    keep = 0;			/* newest survivor so far (the head)	*/
    low = 0;
    for (cur = rsxhead; cur != 0; cur = next) {
	rsxget(cur, &h);
	next = h.next;
	if (h.warmflg != 0)
	    continue;		/* any nonzero byte requests removal; UBYTE is signed here */
	h.prev = 0;
	h.next = 0;
	h.endchain = 0xff;	/* until something links above it	*/
	rsxput(cur, &h);
	if (keep == 0) {
	    keep = cur;		/* the survivor nearest the bottom	*/
	    low = cur;
	} else {
	    rsxget(keep, &h);	/* link it above the previous survivor	*/
	    h.next = cur;
	    h.endchain = 0;
	    rsxput(keep, &h);
	    rsxget(cur, &h);
	    h.prev = keep;
	    rsxput(cur, &h);
	    keep = cur;
	}
    }

    rsxhead = low;
    rsxtop = low ? low : RSXCEIL;
    rsxfence();
}
