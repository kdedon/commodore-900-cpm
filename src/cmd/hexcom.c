/*
 * Portions Copyright (c) 2026 Kevin Dedon.
 */
/*
 * hexcom.c - HEXCOM, the CP/M 3 Intel HEX to .COM converter.
 *
 *	HEXCOM NAME		reads NAME.HEX, writes NAME.COM
 *
 * The .COM image starts at 0100h; gaps between records are zero-filled.
 * Records may arrive in any order above 0100h: each 128-byte output
 * record is written in place, and records never loaded before the
 * highest one are written as zeros so the file has no holes.
 */

#include "cpm.h"

#define	CR	13
#define	LF	10
#define	COMBASE	0x100

static struct fcb	hexfcb;
static struct fcb	comfcb;

static char	hbuf[SECLEN];		/* HEX input record		*/
static int	hpos = SECLEN;		/* SECLEN forces a read		*/

static char	obuf[SECLEN];		/* the .COM record at orec	*/
static int	orec = -1;
static int	nrec;			/* records on disk so far	*/

static unsigned	first;			/* 0 until the first record	*/
static unsigned	last;
static unsigned	nread;			/* data bytes read		*/
static unsigned	loadaddr;		/* this record's address	*/
static unsigned	curaddr;		/* where the next byte goes	*/
static int	cksum;
static char	rdata[256];		/* this record's bytes so far	*/

static VOID crlf()
{
	cputs("\r\n");
}

static VOID hexout(v, n)
unsigned v;
int n;
{
	while (--n >= 0)
		conout("0123456789ABCDEF"[(v >> (n * 4)) & 0xf]);
}

static VOID newline(s)
char *s;
{
	crlf();
	cputs(s);
}

static VOID quit()
{
	crlf();
	__bdos(BDOS_WBOOT, 0L);
}

static VOID fail(s)
char *s;
{
	newline("ERROR: ");
	cputs(s);
	newline("LOAD  ADDRESS ");
	hexout(curaddr, 4);
	quit();
}

/* One random-record transfer of obuf; fn is 33 or 34. */
static int ranio(fn, r)
int fn, r;
{
	comfcb.ran0 = 0;			/* high byte first */
	comfcb.ran1 = (char)(r >> 8);
	comfcb.ran2 = (char)r;
	setdma(obuf);
	return (__bdos(fn, (long)&comfcb));
}

static VOID flush()
{
	int	r;
	char	save[SECLEN];

	if (orec < 0)
		return;
	if (nrec < orec) {
		for (r = 0; r < SECLEN; r++) {
			save[r] = obuf[r];
			obuf[r] = 0;
		}
		for (; nrec < orec; nrec++)
			if (ranio(BDOS_WRITERAN, nrec) != 0)
				fail("DISK WRITE");
		for (r = 0; r < SECLEN; r++)
			obuf[r] = save[r];
	}
	if (ranio(BDOS_WRITERAN, orec) != 0)
		fail("DISK WRITE");
	if (nrec <= orec)
		nrec = orec + 1;
}

static VOID put(b)
int b;
{
	int	r, i;

	r = (curaddr - COMBASE) >> 7;
	if (r != orec) {
		flush();
		orec = r;
		if (r < nrec) {
			if (ranio(BDOS_READRAN, r) != 0)
				fail("DISK READ");
		} else
			for (i = 0; i < SECLEN; i++)
				obuf[i] = 0;
	}
	obuf[(curaddr - COMBASE) & (SECLEN - 1)] = (char)b;
	curaddr++;
}

static int getch()
{
	if (hpos >= SECLEN) {
		setdma(hbuf);
		if (__bdos(BDOS_READSEQ, (long)&hexfcb) != 0)
			fail("DISK READ");
		hpos = 0;
	}
	return (hbuf[hpos++] & 0xff);
}

/* Print the bytes of the bad record loaded so far, 16 to a line. */
static VOID hexerr(s)
char *s;
{
	unsigned	a;

	newline(s);
	newline("LOAD  ADDRESS ");
	hexout(loadaddr, 4);
	newline("ERROR ADDRESS ");
	hexout(curaddr, 4);
	newline("BYTES READ    ");
	for (a = loadaddr; a < curaddr; a++) {
		if (a == loadaddr || (a & 0xf) == 0) {
			crlf();
			hexout(a, 4);
			cputs(": ");
		}
		hexout(rdata[a - loadaddr] & 0xff, 2);
		conout(' ');
	}
	quit();
}

static int digit()
{
	int	c;

	c = getch() - '0';
	if (c < 10)
		return (c);
	c -= 7;
	if (c < 10 || c > 15)
		hexerr("INVALID HEX DIGIT");
	return (c);
}

static int getbyte()
{
	int	b;

	b = digit() << 4;
	b |= digit();
	cksum = (cksum + b) & 0xff;
	return (b);
}

int main()
{
	int		i, n;
	char		*p;

	cputs("HEXCOM\tVERS: 3.00\r\n");
	p = (char *)&_base->fcb1;
	for (i = 0; i < 33; i++) {
		((char *)&hexfcb)[i] = p[i];
		((char *)&comfcb)[i] = p[i];
	}
	hexfcb.ftype[0] = 'H'; hexfcb.ftype[1] = 'E'; hexfcb.ftype[2] = 'X';
	comfcb.ftype[0] = 'C'; comfcb.ftype[1] = 'O'; comfcb.ftype[2] = 'M';
	curaddr = COMBASE;
	if (__bdos(BDOS_OPEN, (long)&hexfcb) == 255)
		fail("CANNOT OPEN SOURCE FILE");
	__bdos(BDOS_DELETE, (long)&comfcb);
	if (__bdos(BDOS_MAKE, (long)&comfcb) == 255)
		fail("DIRECTORY FULL");

	for (;;) {
		while (getch() != ':')
			;
		cksum = 0;
		if ((n = getbyte()) == 0)
			break;
		nread += n;
		loadaddr = getbyte() << 8;
		loadaddr |= getbyte();
		curaddr = loadaddr;
		if (loadaddr < COMBASE)
			fail("LOAD ADDRESS LESS THAN 100");
		if (first == 0) {
			newline("FIRST ADDRESS ");
			hexout(first = loadaddr, 4);
		}
		(void)getbyte();
		for (i = 0; i < n; i++)
			put(rdata[i] = (char)getbyte());
		if (curaddr - 1 > last)
			last = curaddr - 1;
		(void)getbyte();
		if (cksum != 0)
			hexerr("CHECKSUM ERROR ");
	}

	flush();
	newline("LAST  ADDRESS ");
	hexout(last, 4);
	newline("BYTES READ    ");
	hexout(nread, 4);
	newline("RECORDS WRITTEN ");
	hexout((unsigned)nrec, nrec > 0xff ? 4 : 2);
	crlf();
	if (__bdos(BDOS_CLOSE, (long)&comfcb) == 255)
		fail("CANNOT CLOSE FILE");
	quit();
	return (0);
}
