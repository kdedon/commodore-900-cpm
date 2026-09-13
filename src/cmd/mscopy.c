/*
 * mscopy.c - exercise BDOS function 44 (set multi-sector count).
 *
 *	MSCOPY SRC DST [n]		n = records per call, default 16
 *
 * Copies SRC to DST moving n records per read and per write call, then
 * re-reads DST one record at a time and compares the two byte sums, so
 * the copy is checked on target as well as host-side.  Also checks:
 *
 *	- counts 0 and 129 are rejected with 255, 1..128 accepted
 *	- the DMA address survives a multi-record call (the second and
 *	  later calls do not set it again)
 *	- a multi-record random read leaves the FCB random record field
 *	  as the caller set it
 */

#include "cpm.h"

#define	MAXCNT	128

static struct fcb	src;
static struct fcb	dst;
static struct fcb	tmp;		/* the copy, before it is DST	*/
static char		rnbuf[36];	/* function 23 wants two FCBs	*/
static char		buf[MAXCNT * SECLEN];

static unsigned		sumbuf();
static int		setcnt();

/*  Give the scratch FCB the $$$ type, after any call that rebuilds it
    from the destination's name.  */

static VOID scratch()
{
	tmp.ftype[0] = '$';
	tmp.ftype[1] = '$';
	tmp.ftype[2] = '$';
}

/*  Give up with the destination untouched: the scratch file is all that
    is thrown away.  */

static int giveup(msg)
char *msg;
{
	cputs("mscopy: ");
	cputs(msg);
	cputs("\r\n");
	__bdos(BDOS_DELETE, (long) &tmp);
	return (1);
}

int main(argc, argv)
int argc;
char *argv[];
{
	register int	n;
	register char	*p;
	register int	r;
	unsigned	recs;
	unsigned	nrec;
	unsigned	calls;
	unsigned	sum1;
	unsigned	sum2;

	if (argc < 3) {
		cputs("usage: mscopy src dst [n]\r\n");
		return (1);
	}
	n = 0;
	if (argc > 3)
		for (p = argv[3]; *p >= '0' && *p <= '9'; p++)
			n = n * 10 + (*p - '0');
	if (n <= 0 || n > MAXCNT)
		n = 16;

	/* the count limits: 0 and 129 must be refused, n accepted */
	cputs("fn44(0)   -> ");
	putdec((unsigned) setcnt(0));
	cputs("\r\nfn44(129) -> ");
	putdec((unsigned) setcnt(129));
	cputs("\r\nfn44(");
	putdec((unsigned) n);
	cputs(")  -> ");
	putdec((unsigned) setcnt(n));
	cputs("\r\n");

	mkfcb(argv[1], &src);
	if ((__bdos(BDOS_OPEN, (long) &src) & 0xff) == 0xff) {
		cputs("mscopy: cannot open ");
		cputs(argv[1]);
		cputs("\r\n");
		return (1);
	}
	/*  The copy is built in DST's name with type $$$ and only put in
	    DST's place once it is written and CLOSED, because until then
	    there is nothing worth replacing DST with.  Deleting DST first
	    -- which this did -- meant a write error or a refused close
	    destroyed a good file and left a partial one, or none.  */
	mkfcb(argv[2], &tmp);
	if (tmp.ftype[0] == '$' && tmp.ftype[1] == '$' && tmp.ftype[2] == '$') {
		/*  It would BE the scratch file; see src/cmd/fcopy.c.  */
		cputs("mscopy: a destination of type $$$ is the scratch file's\r\n");
		cputs("        own name; copy to another name and rename it\r\n");
		return (1);
	}
	scratch();
	__bdos(BDOS_DELETE, (long) &tmp);	/* a stale one from before */
	mkfcb(argv[2], &tmp);			/* delete scrambles the FCB */
	scratch();
	if ((__bdos(BDOS_MAKE, (long) &tmp) & 0xff) == 0xff) {
		cputs("mscopy: cannot create the scratch file for ");
		cputs(argv[2]);
		cputs("\r\n");
		return (1);
	}

	setdma(buf);			/* set once, for every call */
	nrec = 0;
	calls = 0;
	sum1 = 0;
	for (;;) {
		setcnt(n);
		r = __bdos(BDOS_READSEQ, (long) &src);
		calls++;
		if (r == 0)
			recs = n;	/* a full n records came back */
		else
			recs = ((unsigned) r >> 8) & 0xff;
		if (recs == 0)
			break;		/* end of file, nothing read */
		sum1 += sumbuf(buf, recs);
		nrec += recs;
		setcnt((int) recs);	/* write back exactly what we read */
		if (__bdos(BDOS_WRITESEQ, (long) &tmp) != 0) {
			setcnt(1);
			return (giveup("write error"));
		}
		if (r != 0)
			break;		/* short read: that was the tail */
	}
	setcnt(1);
	if ((__bdos(BDOS_CLOSE, (long) &tmp) & 0xff) == 0xff)
		return (giveup("close failed"));
	__bdos(BDOS_CLOSE, (long) &src);
	setdma(_base->buff);		/* directory work off the default DMA */

	/*  Put the finished copy in DST's place.  CP/M will not rename onto
	    a name that exists, so the old file goes first; past the rename
	    the scratch file holds the only copy, so a failure leaves it
	    where it is rather than deleting it.  */
	mkfcb(argv[2], &dst);
	__bdos(BDOS_DELETE, (long) &dst);
	mkfcb(argv[2], &dst);			/* delete scrambles the FCB */
	for (r = 0; r < 16; r++) {
		rnbuf[r] = ((char *) &tmp)[r];
		rnbuf[16 + r] = ((char *) &dst)[r];
	}
	if ((__bdos(BDOS_RENAME, (long) rnbuf) & 0xff) == 0xff) {
		cputs("mscopy: cannot rename the scratch file over ");
		cputs(argv[2]);
		cputs("\r\n        the copy is in the .$$$ file\r\n");
		return (1);
	}

	cputs("copied ");
	putdec(nrec);
	cputs(" records in ");
	putdec(calls);
	cputs(" calls, sum ");
	putdec(sum1);
	cputs("\r\n");

	/* read the copy back one record at a time and compare sums */
	mkfcb(argv[2], &dst);
	if ((__bdos(BDOS_OPEN, (long) &dst) & 0xff) == 0xff) {
		cputs("mscopy: cannot reopen destination\r\n");
		return (1);
	}
	recs = 0;
	sum2 = 0;
	setdma(buf);
	while (__bdos(BDOS_READSEQ, (long) &dst) == 0) {
		sum2 += sumbuf(buf, 1);
		recs++;
	}
	__bdos(BDOS_CLOSE, (long) &dst);
	cputs("reread ");
	putdec(recs);
	cputs(" records, sum ");
	putdec(sum2);
	cputs("\r\n");
	cputs(sum1 == sum2 && nrec == recs ? "MSCOPY: sums match\r\n"
					   : "MSCOPY: SUMS DIFFER\r\n");

	/* a multi-record random read must leave the random record field
	   alone and must not disturb the DMA address either */
	mkfcb(argv[1], &src);
	__bdos(BDOS_OPEN, (long) &src);
	src.ran0 = 0;
	src.ran1 = 0;
	src.ran2 = 4;			/* CP/M-8000 keeps it big-endian */
	setcnt(n);
	r = __bdos(BDOS_READRAN, (long) &src);
	setcnt(1);
	cputs("random read of ");
	putdec((unsigned) n);
	cputs(" -> ");
	putdec((unsigned) (r & 0xff));
	cputs(", rr now ");
	putdec((unsigned) (src.ran2 & 0xff));
	cputs(src.ran0 == 0 && src.ran1 == 0 && src.ran2 == 4
		? " (preserved)\r\n" : " (CHANGED)\r\n");
	__bdos(BDOS_CLOSE, (long) &src);

	/* record 4 of the source must equal record 4 of the copy */
	sum1 = sumbuf(buf, 1);
	mkfcb(argv[2], &dst);
	__bdos(BDOS_OPEN, (long) &dst);
	dst.ran0 = 0;
	dst.ran1 = 0;
	dst.ran2 = 4;
	__bdos(BDOS_READRAN, (long) &dst);
	__bdos(BDOS_CLOSE, (long) &dst);
	sum2 = sumbuf(buf, 1);
	cputs(sum1 == sum2 ? "MSCOPY: record 4 matches\r\n"
			   : "MSCOPY: RECORD 4 DIFFERS\r\n");
	return (0);
}


/* set the multi-sector count; returns the BDOS return code */
static int setcnt(n)
int n;
{
	return (__bdos(BDOS_SETMULTI, (long) n) & 0xff);
}


/* 16-bit sum of nrec records */
static unsigned sumbuf(p, nrec)
register char *p;
unsigned nrec;
{
	register unsigned	s;
	register unsigned	i;

	s = 0;
	for (i = nrec * SECLEN; i != 0; i--)
		s += *p++ & 0xff;
	return (s);
}
