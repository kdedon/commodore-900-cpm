/*
 * astampt.c - Exercise access/update stamps at three deterministic times;
 * an open without a write must change only the access stamp.
 */

#include "cpm.h"

#define	DAY1	2722		/* 14 June 1985, days since 31 Dec 1977	*/
#define	DAY2	2723		/* 15 June 1985				*/
#define	DAY3	2724		/* 16 June 1985				*/

static struct fcb	f;
static char		buf[SECLEN];
static char		tod[4];
static char		stamps[8];

static int		bad;

static VOID	settime();
static VOID	prstamp();
static VOID	chkstamp();
static VOID	putbcd();
static VOID	puthex();

int main(argc, argv)
int argc;
char *argv[];
{
	register int	r;

	/* ---- the drive must be labelled for ACCESS, not create ---- */
	r = __bdos(BDOS_GETLABEL, 0L) & 0xff;
	cputs("ASTAMPT: fn 101 label mode ");
	puthex(r);
	cputs("\r\n");
	if (r != (DL_ACCESS | DL_UPDATE | DL_EXISTS)) {
		cputs("ASTAMPT: BAD -- this image is not labelled access,update\r\n");
		return (1);
	}

	/* ---- make at DAY1 12:34 ---- */
	settime(DAY1, 0x12, 0x34);
	mkfcb("ASTAMPT.TXT", &f);
	__bdos(BDOS_DELETE, (long) &f);
	mkfcb("ASTAMPT.TXT", &f);
	setdma(buf);
	if ((__bdos(BDOS_MAKE, (long) &f) & 0xff) == 0xff) {
		cputs("ASTAMPT: BAD -- cannot create ASTAMPT.TXT\r\n");
		return (1);
	}
	buf[0] = 'a';
	__bdos(BDOS_WRITESEQ, (long) &f);
	__bdos(BDOS_CLOSE, (long) &f);
	/* make stamps the access/create field when EITHER bit is on
	   (make3a, `mvi c,0101$0000b', bdos30.asm:4359) */
	chkstamp("after make      ", DAY1, 0x12, 0x34, DAY1, 0x12, 0x34);

	/* ---- open and WRITE at DAY2 08:00: both fields move ---- */
	settime(DAY2, 0x08, 0x00);
	mkfcb("ASTAMPT.TXT", &f);
	setdma(buf);
	if ((__bdos(BDOS_OPEN, (long) &f) & 0xff) == 0xff) {
		cputs("ASTAMPT: BAD -- cannot reopen\r\n");
		return (1);
	}
	buf[0] = 'b';
	__bdos(BDOS_WRITESEQ, (long) &f);
	__bdos(BDOS_CLOSE, (long) &f);
	chkstamp("after open+write", DAY2, 0x08, 0x00, DAY2, 0x08, 0x00);

	/* ---- open and close at DAY3 07:07, writing NOTHING.  The access
	   stamp is the only thing in CP/M 3 that can move a directory stamp
	   without a write, so this is the whole test. ---- */
	settime(DAY3, 0x07, 0x07);
	mkfcb("ASTAMPT.TXT", &f);
	setdma(buf);
	if ((__bdos(BDOS_OPEN, (long) &f) & 0xff) == 0xff) {
		cputs("ASTAMPT: BAD -- cannot reopen for the access stamp\r\n");
		return (1);
	}
	__bdos(BDOS_CLOSE, (long) &f);
	chkstamp("after open only ", DAY3, 0x07, 0x07, DAY2, 0x08, 0x00);

	cputs(bad ? "ASTAMPT: FAIL\r\n" : "ASTAMPT: PASS\r\n");
	return (bad != 0);
}


/* function 104 takes {date low, date high, BCD hour, BCD minute} */
static VOID settime(day, hour, min)
int day;
int hour;
int min;
{
	tod[0] = (char) (day & 0xff);
	tod[1] = (char) ((day >> 8) & 0xff);
	tod[2] = (char) hour;
	tod[3] = (char) min;
	__bdos(BDOS_SETTIME, (long) tod);
}


static VOID prstamp(p)
register char *p;
{
	putdec((unsigned) ((p[0] & 0xff) | ((p[1] & 0xff) << 8)));
	conout(' ');
	putbcd(p[2] & 0xff);
	conout(':');
	putbcd(p[3] & 0xff);
}


/* function 102: eight bytes at the DMA address, access/create stamp first */
static VOID chkstamp(what, cday, chour, cmin, uday, uhour, umin)
char *what;
int cday, chour, cmin, uday, uhour, umin;
{
	mkfcb("ASTAMPT.TXT", &f);
	setdma(stamps);
	if ((__bdos(BDOS_RDSTAMPS, (long) &f) & 0xff) == 0xff) {
		cputs("ASTAMPT: BAD -- fn 102 found no file\r\n");
		bad++;
		return;
	}
	cputs("ASTAMPT: ");
	cputs(what);
	cputs("  access ");
	prstamp(&stamps[0]);
	cputs("  update ");
	prstamp(&stamps[4]);
	if ((stamps[0] & 0xff) != (cday & 0xff)
	    || (stamps[1] & 0xff) != ((cday >> 8) & 0xff)
	    || (stamps[2] & 0xff) != chour || (stamps[3] & 0xff) != cmin
	    || (stamps[4] & 0xff) != (uday & 0xff)
	    || (stamps[5] & 0xff) != ((uday >> 8) & 0xff)
	    || (stamps[6] & 0xff) != uhour || (stamps[7] & 0xff) != umin) {
		cputs("  BAD");
		bad++;
	}
	cputs("\r\n");
	setdma(buf);
}


static VOID putbcd(v)
register int v;
{
	conout('0' + ((v >> 4) & 0x0f));
	conout('0' + (v & 0x0f));
}


static VOID puthex(v)
register int v;
{
	static char	hex[] = "0123456789abcdef";

	conout(hex[(v >> 4) & 0x0f]);
	conout(hex[v & 0x0f]);
}
