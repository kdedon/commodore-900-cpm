/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/*
 * rsxt2.c - Inspect the resident chain and exercise console interception,
 * open post-processing, and delete refusal.
 */

#include "cpm.h"

#define	BDOS_CALLRSX	60
#define	RSX_QUERY	126	/* the system's chain query (sys/rsx.c)	*/
#define	UCASE_COUNT	200	/* UCASE.RSX's sub-function		*/
#define	PROT_COUNT	201	/* PROT.RSX's sub-function		*/

/* prefix offsets, sys/rsxhdr.h */
#define	H_SERIAL	0
#define	H_NEXT		0x0a
#define	H_PREV		0x0c
#define	H_WARMFLG	0x0e
#define	H_NBANK		0x0f
#define	H_NAME		0x10
#define	H_ENDCHAIN	0x18
#define	H_PATCH		0x1a
#define	H_ORG		0x1c
#define	H_LEN		0x1e

#define	GUARD	"GUARD.TXT"	/* the file the delete test protects	*/

struct rsxpb {
	char		rpfunc;
	char		rprsvd;
	unsigned	rporg;
	unsigned	rplen;
	long		rpsrc;
};

struct rsxpb	pb;
struct fcb	fcb;

VOID puthex(n)
unsigned n;
{
	register int	i, d;

	for (i = 12; i >= 0; i -= 4) {
		d = (n >> i) & 0xf;
		conout(d < 10 ? '0' + d : 'A' + d - 10);
	}
}

VOID puthex2(n)
unsigned n;
{
	register int	i, d;

	for (i = 4; i >= 0; i -= 4) {
		d = (n >> i) & 0xf;
		conout(d < 10 ? '0' + d : 'A' + d - 10);
	}
}

/*  A TPA offset as a pointer.  Every module and every transient lives in
    the one TPA segment (sys/rsx.c), so an object of this program's own
    supplies the segment and the offset is the module's `org'.  */

char *tpaptr(off)
unsigned off;
{
	return ((char *) (((long) &pb & 0xffff0000L) | (long)(unsigned) off));
}

unsigned wordat(p, off)
char *p;
int off;
{
	return (((p[off] & 0xff) << 8) | (p[off + 1] & 0xff));
}

/*  Ask the chain a sub-function.  0FFh is the system's "nobody claimed
    it" (sys/rsxhdr.h RSX_NOTHANDLED), which is also what an unreachable
    module looks like from here.  */

int rsxask(sub)
int sub;
{
	pb.rpfunc = sub;
	pb.rprsvd = 0;
	return (__bdos(BDOS_CALLRSX, (long) &pb));
}

VOID report(what, sub)
char *what;
int sub;
{
	register int	r;

	r = rsxask(sub);
	conputs("rsxt2: ");
	conputs(what);
	if (r == 0xff)
		conputs(" unclaimed\r\n");
	else {
		conputs("=");
		puthex((unsigned) r);
		conputs("\r\n");
	}
}

VOID chain()
{
	register char	*p;
	register unsigned org;
	register int	i;

	org = (unsigned) rsxask(RSX_QUERY);
	if (org == 0) {
		conputs("rsxt2: chain empty\r\n");
		return;
	}
	while (org != 0) {
		p = tpaptr(org);
		conputs("rsxt2: mod ");
		puthex(org);
		conputs(" ");
		for (i = 0; i < 8; i++)
			conout(p[H_NAME + i]);
		conputs(" ser=");
		for (i = 0; i < 6; i++)
			conout(p[H_SERIAL + i]);
		conputs(" wf="); puthex2((unsigned) p[H_WARMFLG] & 0xff);
		conputs(" nb="); puthex2((unsigned) p[H_NBANK] & 0xff);
		conputs(" ec="); puthex2((unsigned) p[H_ENDCHAIN] & 0xff);
		conputs(" pl="); puthex2((unsigned) p[H_PATCH] & 0xff);
		conputs(" prev="); puthex(wordat(p, H_PREV));
		conputs(" next="); puthex(wordat(p, H_NEXT));
		conputs(" len="); puthex(wordat(p, H_LEN));
		conputs("\r\n");
		org = wordat(p, H_NEXT);
	}
}

VOID fileop(what, func, name)
char *what;
int func;
char *name;
{
	register int	r;

	mkfcb(name, &fcb);
	r = __bdos(func, (long) &fcb);
	conputs("rsxt2: ");
	conputs(what);
	conputs("=");
	putdec((unsigned) r);
	/*  The verdict as a word as well as a number: a successful open
	    answers with a directory code, 0 to 3, which depends on where
	    the file landed and is not the same from run to run.  */
	conputs(r == 255 ? " fail\r\n" : " ok\r\n");
}

int main()
{
	chain();
	report("ucase", UCASE_COUNT);

	/*  A file to protect: function 22 is not intercepted by anything,
	    so the open that follows it always succeeds and the counters
	    below do not depend on what is on the disk.

	    The make is made SILENT rather than preceded by a delete,
	    because GUARD.TXT can already be there: PROT.RSX refuses
	    function 19 without the BDOS ever seeing it, so once a run
	    with PROT resident has been through `delete guard' the file
	    survives into the next run, and the BDOS now answers a make
	    onto an existing name with error 8 (file$exists,
	    bdos30.asm:4371-4372).  That is the right answer and
	    it costs this program nothing -- the file exists either way,
	    which is all the open needs -- but under the default error
	    mode it would put a `File Exists' report in the middle of the
	    transcript.  A delete of our own would be the other fix and
	    is the wrong one: PROT counts deletes, so it would change the
	    number this program exists to print.  */
	__bdos(BDOS_ERRMODE, (long) ERRMODE_RETURN);
	mkfcb(GUARD, &fcb);
	__bdos(BDOS_MAKE, (long) &fcb);
	__bdos(BDOS_CLOSE, (long) &fcb);
	__bdos(BDOS_ERRMODE, (long) ERRMODE_DEFAULT);

	fileop("open guard", BDOS_OPEN, GUARD);
	fileop("open nosuch", BDOS_OPEN, "NOSUCH.XXX");
	fileop("delete guard", BDOS_DELETE, GUARD);
	fileop("reopen guard", BDOS_OPEN, GUARD);
	report("prot", PROT_COUNT);

	conputs("rsxt2: htpa=");
	puthex((unsigned) (_base->htpa & 0xffffL));
	conputs("\r\n");
	return (0);
}
