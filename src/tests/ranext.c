/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/*
 * ranext.c - A random read of an extent that does not exist returns 4 and
 * must leave the FCB on the extent it had; a random write to that extent
 * afterwards must create it, not overwrite the extent the FCB still maps.
 */

#include "cpm.h"

#define	NREC	300		/* two directory entries		*/
#define	FAR	600		/* a record in an entry that is not there */

static struct fcb	f;
static struct fcb	s;
static char		buf[SECLEN];
static char		dirbuf[SECLEN];

static int		bad;

static VOID	setran();
static int	check();
static int	opnext();

int main(argc, argv)
int argc;
char *argv[];
{
	register int	i;
	int		r, n;
	long		sz;

	/* ---- lay down NREC records, each stamped with its number ---- */
	mkfcb("RANEXT.TXT", &f);
	__bdos(BDOS_DELETE, (long) &f);
	mkfcb("RANEXT.TXT", &f);
	setdma(buf);
	if ((__bdos(BDOS_MAKE, (long) &f) & 0xff) == 0xff) {
		cputs("RANEXT: BAD -- cannot create RANEXT.TXT\r\n");
		return (1);
	}
	for (i = 0; i < NREC; i++) {
		buf[0] = (char) (i & 0xff);
		buf[1] = (char) ((i >> 8) & 0xff);
		if (__bdos(BDOS_WRITESEQ, (long) &f) & 0xff) {
			cputs("RANEXT: BAD -- write failed\r\n");
			return (1);
		}
	}
	__bdos(BDOS_CLOSE, (long) &f);

	/* ---- position on the last extent, then seek past it ---- */
	mkfcb("RANEXT.TXT", &f);
	if ((__bdos(BDOS_OPEN, (long) &f) & 0xff) == 0xff) {
		cputs("RANEXT: BAD -- cannot reopen\r\n");
		return (1);
	}
	bad += check(NREC - 1, NREC - 1);
	setran((long) FAR);
	r = __bdos(BDOS_READRAN, (long) &f) & 0xff;
	cputs("RANEXT: fn 33 on an unwritten extent -> ");
	putdec((unsigned) r);
	cputs("\r\n");
	if (r != 4) {
		cputs("RANEXT: BAD -- want 4\r\n");
		bad++;
	}

	/* ---- write there with the same FCB ---- */
	buf[0] = (char) (FAR & 0xff);
	buf[1] = (char) ((FAR >> 8) & 0xff);
	setran((long) FAR);
	r = __bdos(BDOS_WRITERAN, (long) &f) & 0xff;
	cputs("RANEXT: fn 34 -> ");
	putdec((unsigned) r);
	cputs("\r\n");
	if (r != 0) {
		cputs("RANEXT: BAD -- random write failed\r\n");
		bad++;
	}
	r = __bdos(BDOS_CLOSE, (long) &f) & 0xff;
	if (r == 0xff) {
		cputs("RANEXT: BAD -- close failed\r\n");
		bad++;
	}

	/* ---- every old record is still itself, the new one reads back ---- */
	mkfcb("RANEXT.TXT", &f);
	__bdos(BDOS_OPEN, (long) &f);
	for (i = 0; i < NREC; i++)
		if (check(i, i)) {
			bad++;
			break;
		}
	if (i == NREC)
		cputs("RANEXT: 300 old records intact\r\n");
	if (check(FAR, FAR))
		bad++;
	__bdos(BDOS_CLOSE, (long) &f);

	/* ---- three directory entries, size FAR+1 ---- */
	mkfcb("RANEXT.TXT", &s);
	s.extent = '?';
	setdma(dirbuf);
	n = 0;
	r = __bdos(BDOS_SFIRST, (long) &s) & 0xff;
	while (r != 0xff) {
		n++;
		r = __bdos(BDOS_SNEXT, 0L) & 0xff;
	}
	setdma(buf);
	cputs("RANEXT: directory entries ");
	putdec((unsigned) n);
	cputs("\r\n");
	if (n != 3) {
		cputs("RANEXT: BAD -- want 3\r\n");
		bad++;
	}
	mkfcb("RANEXT.TXT", &f);
	__bdos(35, (long) &f);
	sz = ((long) (f.ran0 & 0xff) << 16) | ((long) (f.ran1 & 0xff) << 8)
		| (long) (f.ran2 & 0xff);
	if (sz != (long) FAR + 1L) {
		cputs("RANEXT: BAD -- file size\r\n");
		bad++;
	}

	/* ---- an open on extent EX starts reading at record EX*128 ---- */
	bad += opnext(1, 128);			/* inside the first entry */
	bad += opnext(2, 256);			/* the next entry	  */
	bad += opnext(0x21, 128);		/* extent 1, module bits set */

	cputs(bad ? "RANEXT: FAIL\r\n" : "RANEXT: PASS\r\n");
	return (bad != 0);
}


static VOID setran(r)
long r;
{
	f.ran0 = (char) ((r >> 16) & 0xff);
	f.ran1 = (char) ((r >> 8) & 0xff);
	f.ran2 = (char) (r & 0xff);
}


/* open on extent ex and read one record; nonzero unless it holds stamp want */
static int opnext(ex, want)
int ex, want;
{
	int	r;

	mkfcb("RANEXT.TXT", &f);
	f.extent = (char) ex;
	if ((__bdos(BDOS_OPEN, (long) &f) & 0xff) == 0xff) {
		cputs("RANEXT: BAD -- cannot open extent ");
		putdec((unsigned) ex);
		cputs("\r\n");
		return (1);
	}
	buf[0] = buf[1] = 0;
	r = __bdos(BDOS_READSEQ, (long) &f) & 0xff;
	if (r != 0 || (buf[0] & 0xff) != (want & 0xff)
	    || (buf[1] & 0xff) != ((want >> 8) & 0xff)) {
		cputs("RANEXT: BAD -- extent ");
		putdec((unsigned) ex);
		cputs(" read ");
		putdec((unsigned) r);
		cputs(", holds ");
		putdec((unsigned) (((buf[1] & 0xff) << 8) | (buf[0] & 0xff)));
		cputs(", want ");
		putdec((unsigned) want);
		cputs("\r\n");
		return (1);
	}
	cputs("RANEXT: extent ");
	putdec((unsigned) ex);
	cputs(" opens at its own record\r\n");
	return (0);
}


/* random-read record rec; nonzero unless it holds stamp want */
static int check(rec, want)
int rec, want;
{
	int	r;

	buf[0] = buf[1] = 0;
	setran((long) rec);
	r = __bdos(BDOS_READRAN, (long) &f) & 0xff;
	if (r != 0 || (buf[0] & 0xff) != (want & 0xff)
	    || (buf[1] & 0xff) != ((want >> 8) & 0xff)) {
		cputs("RANEXT: BAD -- record ");
		putdec((unsigned) rec);
		cputs(" read ");
		putdec((unsigned) r);
		cputs(", holds ");
		putdec((unsigned) (((buf[1] & 0xff) << 8) | (buf[0] & 0xff)));
		cputs("\r\n");
		return (1);
	}
	return (0);
}
