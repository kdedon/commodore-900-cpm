/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/*
 * truncb.c - Exercise truncation across extent and allocation-block
 * boundaries.
 */

#include "cpm.h"

static struct fcb	f;
static char		buf[SECLEN];
static char		freerec[4];

static int		bad;

static long	freesp();
static long	fsize();
static VOID	setran();
static VOID	putlong();
static VOID	docase();

int main(argc, argv)
int argc;
char *argv[];
{
	/*			 name	     nrec keep  freed blocks */
	docase("TRUNCB1.TXT",	520, 384, 5);
	docase("TRUNCB2.TXT",	300, 256, 2);
	docase("TRUNCB3.TXT",	300, 128, 6);
	docase("TRUNCB4.TXT",	 40,   1, 1);

	cputs(bad ? "TRUNCB: FAIL\r\n" : "TRUNCB: PASS\r\n");
	return (bad != 0);
}


static VOID docase(name, nrec, keep, freed)
char *name;
int nrec;
int keep;
int freed;			/* 4K blocks the truncate must give back	*/
{
	register int	i;
	long		free1, free2;
	long		sz;

	cputs("TRUNCB: ");
	cputs(name);
	cputs(" ");
	putdec((unsigned) nrec);
	cputs(" -> ");
	putdec((unsigned) keep);

	mkfcb(name, &f);
	__bdos(BDOS_DELETE, (long) &f);
	mkfcb(name, &f);
	setdma(buf);
	if ((__bdos(BDOS_MAKE, (long) &f) & 0xff) == 0xff) {
		cputs("  BAD -- cannot create\r\n");
		bad++;
		return;
	}
	for (i = 0; i < nrec; i++) {
		buf[0] = (char) (i & 0xff);
		buf[1] = (char) ((i >> 8) & 0xff);
		if (__bdos(BDOS_WRITESEQ, (long) &f) & 0xff) {
			cputs("  BAD -- write failed at record ");
			putdec((unsigned) i);
			cputs("\r\n");
			bad++;
			return;
		}
	}
	__bdos(BDOS_CLOSE, (long) &f);
	free1 = freesp();

	if (fsize(name) != (long) nrec) {
		cputs("  BAD -- size before the cut\r\n");
		bad++;
		return;
	}

	/* ---- the cut: keep records 0..keep-1 ---- */
	mkfcb(name, &f);
	setran((long) (keep - 1));
	i = __bdos(99, (long) &f) & 0xff;
	if (i == 0xff) {
		cputs("  BAD -- truncate refused\r\n");
		bad++;
		return;
	}
	sz = fsize(name);
	free2 = freesp();
	cputs("  size ");
	putlong(sz);
	cputs("  freed ");
	putlong((free2 - free1) / 32L);
	cputs(" blocks");
	if (sz != (long) keep) {
		cputs("  BAD -- size after the cut");
		bad++;
	}
	if (free2 - free1 != (long) freed * 32L) {
		cputs("  BAD -- wrong number of blocks returned");
		bad++;
	}

	/* ---- every surviving record is still itself, and stops there ---- */
	mkfcb(name, &f);
	setdma(buf);
	if ((__bdos(BDOS_OPEN, (long) &f) & 0xff) == 0xff) {
		cputs("  BAD -- cannot reopen\r\n");
		bad++;
		return;
	}
	for (i = 0; i < keep; i++) {
		buf[0] = buf[1] = 0;
		if (__bdos(BDOS_READSEQ, (long) &f) & 0xff) {
			cputs("  BAD -- read failed at record ");
			putdec((unsigned) i);
			bad++;
			break;
		}
		if ((buf[0] & 0xff) != (i & 0xff)
		    || (buf[1] & 0xff) != ((i >> 8) & 0xff)) {
			cputs("  BAD -- record ");
			putdec((unsigned) i);
			cputs(" is not itself");
			bad++;
			break;
		}
	}
	if (i == keep && (__bdos(BDOS_READSEQ, (long) &f) & 0xff) == 0) {
		cputs("  BAD -- the record after the cut is still readable");
		bad++;
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


/* function 46: free space as THREE LITTLE-ENDIAN BYTES of 128-byte
   record count at the DMA address, fourth byte zero -- the CP/M 3 wire
   form.  Assembled by hand: this machine is big-endian, so reading the
   buffer back as a `long' would be the very bug this form replaces. */
static long freesp()
{
	freerec[0] = freerec[1] = freerec[2] = freerec[3] = 0;
	setdma(freerec);
	__bdos(46, 0L);
	setdma(buf);
	return ( ((long) (freerec[0] & 0xff))
	       | ((long) (freerec[1] & 0xff) <<  8)
	       | ((long) (freerec[2] & 0xff) << 16) );
}


static VOID putlong(n)
long n;
{
	if (n >= 10L)
		putlong(n / 10L);
	conout((int) ('0' + n % 10L));
}
