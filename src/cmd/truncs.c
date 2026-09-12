/*
 * truncs.c - Exercise truncation of sparse files and verify surviving
 * records.
 */

#include "cpm.h"

static struct fcb	f;
static char		buf[SECLEN];

static int		bad;

static long	freesp();
static long	fsize();
static VOID	setran();
static VOID	putlong();
static VOID	docase();
static int	wrec();

int main(argc, argv)
int argc;
char *argv[];
{
	/*		 name	      r1   r2  cut  size freed */
	docase("TRUNCS1.TXT",	  0, 255, 150,  128,  1);
	docase("TRUNCS2.TXT",	200,  -1,  50,    0,  1);
	docase("TRUNCS3.TXT",	  0, 150, 150,  151,  0);

	cputs(bad ? "TRUNCS: FAIL\r\n" : "TRUNCS: PASS\r\n");
	return (bad != 0);
}


/* function 34: write record n, whose contents are n */
static int wrec(n)
int n;
{
	register int	i;

	for (i = 0; i < SECLEN; i++)
		buf[i] = 0;
	buf[0] = (char) (n & 0xff);
	buf[1] = (char) ((n >> 8) & 0xff);
	setran((long) n);
	return (__bdos(34, (long) &f) & 0xff);
}


static VOID docase(name, r1, r2, cut, want, freed)
char *name;
int r1;				/* first record written			*/
int r2;				/* second record written, -1 for none	*/
int cut;			/* keep records 0..cut			*/
int want;			/* records function 35 must report after */
int freed;			/* 4K blocks the truncate must give back */
{
	long		free1, free2;
	long		sz;
	int		i;

	cputs("TRUNCS: ");
	cputs(name);
	cputs(" holes at ");
	putdec((unsigned) r1);
	if (r2 >= 0) {
		cputs(",");
		putdec((unsigned) r2);
	}
	cputs(" cut ");
	putdec((unsigned) cut);

	mkfcb(name, &f);
	__bdos(BDOS_DELETE, (long) &f);
	mkfcb(name, &f);
	setdma(buf);
	if ((__bdos(BDOS_MAKE, (long) &f) & 0xff) == 0xff) {
		cputs("  BAD -- cannot create\r\n");
		bad++;
		return;
	}
	if (wrec(r1) != 0 || (r2 >= 0 && wrec(r2) != 0)) {
		cputs("  BAD -- random write failed\r\n");
		bad++;
		return;
	}
	__bdos(BDOS_CLOSE, (long) &f);
	free1 = freesp();

	/* the hole really is a hole: only the blocks written are allocated */
	sz = fsize(name);
	cputs("  before ");
	putlong(sz);
	if (sz != (long) ((r2 >= 0 ? r2 : r1) + 1)) {
		cputs("  BAD -- size before the cut");
		bad++;
	}

	mkfcb(name, &f);
	setran((long) cut);
	i = __bdos(99, (long) &f) & 0xff;
	if (i == 0xff) {
		cputs("  BAD -- truncate refused\r\n");
		bad++;
		return;
	}
	sz = fsize(name);
	free2 = freesp();
	cputs("  after ");
	putlong(sz);
	cputs("  freed ");
	putlong((free2 - free1) / 32L);
	cputs(" blocks");
	if (sz != (long) want) {
		cputs("  BAD -- size after the cut");
		bad++;
	}
	if (free2 - free1 != (long) freed * 32L) {
		cputs("  BAD -- wrong number of blocks returned");
		bad++;
	}

	/* the record the file still starts with must still be itself, and
	   the one the cut removed must be gone */
	mkfcb(name, &f);
	setdma(buf);
	if ((__bdos(BDOS_OPEN, (long) &f) & 0xff) == 0xff) {
		cputs("  BAD -- cannot reopen\r\n");
		bad++;
		return;
	}
	if (want > 0) {
		buf[0] = buf[1] = 0;
		setran((long) r1);
		if ((__bdos(33, (long) &f) & 0xff) != 0
		    || (buf[0] & 0xff) != (r1 & 0xff)
		    || (buf[1] & 0xff) != ((r1 >> 8) & 0xff)) {
			cputs("  BAD -- the surviving record is not itself");
			bad++;
		}
	}
	if (r2 >= 0 && r2 > cut) {
		setran((long) r2);
		if ((__bdos(33, (long) &f) & 0xff) == 0) {
			cputs("  BAD -- the record past the cut still reads");
			bad++;
		}
	}
	__bdos(BDOS_CLOSE, (long) &f);
	cputs("\r\n");
}


static VOID setran(r)
long r;
{
	f.ran0 = (char) ((r >> 16) & 0xff);	/* ran0 is the HIGH byte	*/
	f.ran1 = (char) ((r >> 8) & 0xff);	/* (sys/bdosrw.c:327)	*/
	f.ran2 = (char) (r & 0xff);
}


/* function 35: compute file size into the FCB's random record field */
static long fsize(name)
char *name;
{
	mkfcb(name, &f);
	__bdos(35, (long) &f);
	return (((long) (f.ran0 & 0xff) << 16) | ((long) (f.ran1 & 0xff) << 8)
		| (long) (f.ran2 & 0xff));
}


static long freesp()
{
	__bdos(46, 0L);
	setdma(buf);
}


static VOID putlong(n)
long n;
{
	if (n >= 10L)
		putlong(n / 10L);
	conout((int) ('0' + n % 10L));
}
