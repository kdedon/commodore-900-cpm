/*
 * stampt.c - Exercise create/update stamps using deterministic BDOS clock
 * values.
 */

#include "cpm.h"

#define	DAY1	2722		/* 14 June 1985, days since 31 Dec 1977	*/
#define	DAY2	2723		/* 15 June 1985				*/

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

	/* ---- function 101: what stamping is switched on here? ---- */
	r = __bdos(BDOS_GETLABEL, 0L) & 0xff;
	cputs("STAMPT: fn 101 label mode ");
	puthex(r);
	if (!(r & DL_EXISTS)) {
		cputs(" -- no directory label on A:, nothing to test\r\n");
		return (1);
	}
	if (!(r & (DL_CREATE | DL_ACCESS)) || !(r & DL_UPDATE)) {
		cputs(" -- create+update stamping is not on\r\n");
		bad++;
	}
	cputs("\r\n");

	/* ---- functions 104/105 round-trip ---- */
	settime(DAY1, 0x12, 0x34);
	r = __bdos(BDOS_GETTIME, (long) tod) & 0xff;
	cputs("STAMPT: fn 105 -> ");
	prstamp(tod);
	cputs(" sec ");
	putbcd(r);
	if ((tod[0] & 0xff) != (DAY1 & 0xff)
	    || (tod[1] & 0xff) != ((DAY1 >> 8) & 0xff)
	    || (tod[2] & 0xff) != 0x12 || (tod[3] & 0xff) != 0x34) {
		cputs("  BAD -- fn 104 did not round-trip");
		bad++;
	}
	cputs("\r\n");

	/* ---- function 100: rewrite the label with the mode it already
	   has.  Idempotent, so every other test on this image still sees
	   the drive it expects, but it exercises the make/update path and
	   must leave the label's own update stamp at DAY1 12:34. ---- */
	mkfcb("C900A", &f);
	f.extent = (char) (DL_CREATE | DL_UPDATE);
	r = __bdos(BDOS_SETLABEL, (long) &f) & 0xff;
	cputs("STAMPT: fn 100 -> ");
	putdec((unsigned) r);
	r = __bdos(BDOS_GETLABEL, 0L) & 0xff;
	cputs("  fn 101 now ");
	puthex(r);
	if (r != (DL_CREATE | DL_UPDATE | DL_EXISTS)) {
		cputs("  BAD -- fn 100 did not set the mode");
		bad++;
	}
	cputs("\r\n");

	/* ---- create the file at DAY1 12:34 ---- */
	mkfcb("STAMPT.TXT", &f);
	__bdos(BDOS_DELETE, (long) &f);
	mkfcb("STAMPT.TXT", &f);
	setdma(buf);
	if ((__bdos(BDOS_MAKE, (long) &f) & 0xff) == 0xff) {
		cputs("STAMPT: BAD -- cannot create STAMPT.TXT\r\n");
		return (1);
	}
	buf[0] = 'a';
	__bdos(BDOS_WRITESEQ, (long) &f);
	__bdos(BDOS_CLOSE, (long) &f);
	chkstamp("after make ", DAY1, 0x12, 0x34, DAY1, 0x12, 0x34);

	/* ---- move the clock on a day and write to it again ---- */
	settime(DAY2, 0x08, 0x00);
	mkfcb("STAMPT.TXT", &f);
	setdma(buf);
	if ((__bdos(BDOS_OPEN, (long) &f) & 0xff) == 0xff) {
		cputs("STAMPT: BAD -- cannot reopen STAMPT.TXT\r\n");
		return (1);
	}
	buf[0] = 'b';
	__bdos(BDOS_WRITESEQ, (long) &f);
	__bdos(BDOS_CLOSE, (long) &f);
	chkstamp("after write", DAY1, 0x12, 0x34, DAY2, 0x08, 0x00);

	/* ---- a wildcard is error 9, not "not found" ----
	   func102 calls check$wild before it reads anything
	   (bdos30.asm:4942-4945), and check$wild reports through
	   set$aret (:1774, :4373-4380): 09FFh back, and "? in Filename"
	   on the console.  Error mode 0FEh asks for both in one call. */
	__bdos(BDOS_ERRMODE, (long) ERRMODE_DISPRET);
	mkfcb("STAMP?.TXT", &f);
	setdma(stamps);
	r = __bdos(BDOS_RDSTAMPS, (long) &f);
	__bdos(BDOS_ERRMODE, (long) ERRMODE_DEFAULT);
	cputs("STAMPT: fn 102 on a wildcard -> ");
	putdec((unsigned) ((r >> 8) & 0xff));
	cputs("/");
	putdec((unsigned) (r & 0xff));
	if (((r >> 8) & 0xff) != 9 || (r & 0xff) != 0xff) {
		cputs("  BAD -- want error 9, return 255");
		bad++;
	}
	cputs("\r\n");

	cputs(bad ? "STAMPT: FAIL\r\n" : "STAMPT: PASS\r\n");
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


/* function 102: eight bytes at the DMA address, create stamp first */
static VOID chkstamp(what, cday, chour, cmin, uday, uhour, umin)
char *what;
int cday, chour, cmin, uday, uhour, umin;
{
	mkfcb("STAMPT.TXT", &f);
	setdma(stamps);
	if ((__bdos(BDOS_RDSTAMPS, (long) &f) & 0xff) == 0xff) {
		cputs("STAMPT: BAD -- fn 102 found no file\r\n");
		bad++;
		return;
	}
	cputs("STAMPT: ");
	cputs(what);
	cputs("  create ");
	prstamp(&stamps[0]);
	cputs("  update ");
	prstamp(&stamps[4]);
	cputs("  pwmode ");
	puthex(f.extent & 0xff);
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
