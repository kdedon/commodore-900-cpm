/*
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
	if (r == 0xff)
	else {
		puthex((unsigned) r);
	}
}

VOID chain()
{
	register char	*p;
	register unsigned org;
	register int	i;

	org = (unsigned) rsxask(RSX_QUERY);
	if (org == 0) {
		return;
	}
	while (org != 0) {
		p = tpaptr(org);
		puthex(org);
		for (i = 0; i < 8; i++)
			conout(p[H_NAME + i]);
		for (i = 0; i < 6; i++)
			conout(p[H_SERIAL + i]);
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
	putdec((unsigned) r);
	/*  The verdict as a word as well as a number: a successful open
	    answers with a directory code, 0 to 3, which depends on where
	    the file landed and is not the same from run to run.  */
}

int main()
{
	chain();
	report("ucase", UCASE_COUNT);

	mkfcb(GUARD, &fcb);
	__bdos(BDOS_MAKE, (long) &fcb);
	__bdos(BDOS_CLOSE, (long) &fcb);

	fileop("open guard", BDOS_OPEN, GUARD);
	fileop("open nosuch", BDOS_OPEN, "NOSUCH.XXX");
	fileop("delete guard", BDOS_DELETE, GUARD);
	fileop("reopen guard", BDOS_OPEN, GUARD);
	report("prot", PROT_COUNT);

	puthex((unsigned) (_base->htpa & 0xffffL));
	return (0);
}
