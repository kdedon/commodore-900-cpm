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
static char		buf[MAXCNT * SECLEN];

static unsigned		sumbuf();
static int		setcnt();

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
		}
		if (r != 0)
			break;		/* short read: that was the tail */
	}
	setcnt(1);
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
