/*
 */

#include "cpm.h"

#define	PROTECTED	"HELLO.C"	/* the file the XFCB belongs to	*/
#define	PWMODE		0xe0		/* read+write+delete protected	*/

static struct fcb	f;
static char		buf[SECLEN];
static char		stamps[8];

static int		bad;

static long	freesp();
static int	scan();
static VOID	putlong();
static VOID	puthex();

int main(argc, argv)
int argc;
char *argv[];
{
	register int	r;
	int		nxfcb, nlabel, nsfcb;

	/* ---- what function 46 says, which is the number the control run
	   has to agree with ---- */
	cputs("XFCBT: free ");
	putlong(freesp());
	cputs("\r\n");

	/* ---- the raw directory, through a '?' drive search: the only way
	   a program can see entries that are not files ---- */
	nxfcb = scan(&nlabel, &nsfcb);
	cputs("XFCBT: directory has ");
	putdec((unsigned) nxfcb);
	cputs(" xfcb, ");
	putdec((unsigned) nlabel);
	cputs(" label, ");
	putdec((unsigned) nsfcb);
	cputs(" sfcb entries\r\n");

	if (argc > 1) {			/* the control image */
		if (nxfcb != 0) {
			cputs("XFCBT: BAD -- the control image has an XFCB\r\n");
			bad++;
		}
		cputs(bad ? "XFCBT: FAIL\r\n" : "XFCBT: PASS\r\n");
		return (bad != 0);
	}

	if (nxfcb != 1) {
		cputs("XFCBT: BAD -- the XFCB is not reachable\r\n");
		bad++;
	}

	/* ---- the protected file is still an ordinary file.  Two reasons,
	   and the second is the one with teeth now that passwords are
	   enforced: an XFCB must not shadow the FCB it names, AND this
	   drive's directory label does not have the password-enable bit,
	   so nothing on it is enforced at all.  That makes this image a
	   second witness for the property verify-pass's control run is
	   about -- an XFCB on an unarmed medium costs nothing. ---- */
	mkfcb(PROTECTED, &f);
	setdma(buf);
	r = __bdos(BDOS_OPEN, (long) &f) & 0xff;
	cputs("XFCBT: open ");
	cputs(PROTECTED);
	cputs(" -> ");
	puthex(r);
	if (r == 0xff) {
		cputs("  BAD -- the XFCB blocked the file");
		bad++;
	}
	cputs("\r\n");
	__bdos(BDOS_CLOSE, (long) &f);

	/* ---- function 102 reports the SFCB's password mode byte in the
	   caller's extent field (bdos30.asm:4938-4956) ---- */
	mkfcb(PROTECTED, &f);
	setdma(stamps);
	r = __bdos(BDOS_RDSTAMPS, (long) &f) & 0xff;
	setdma(buf);
	cputs("XFCBT: fn 102 pwmode ");
	puthex(f.extent & 0xff);
	if (r == 0xff) {
		cputs("  BAD -- fn 102 found no file");
		bad++;
	} else if ((f.extent & 0xff) != PWMODE) {
		cputs("  BAD -- want e0 from the SFCB sub-record");
		bad++;
	}
	cputs("\r\n");

	/* ---- claiming a directory slot must not claim the XFCB: make a
	   file and delete it again, then the Makefile compares the XFCB's
	   bytes on disk against the ones the packer wrote ---- */
	mkfcb("XFCBT.TXT", &f);
	__bdos(BDOS_DELETE, (long) &f);
	mkfcb("XFCBT.TXT", &f);
	setdma(buf);
	if ((__bdos(BDOS_MAKE, (long) &f) & 0xff) == 0xff) {
		cputs("XFCBT: BAD -- cannot create XFCBT.TXT\r\n");
		bad++;
	} else {
		buf[0] = 'x';
		__bdos(BDOS_WRITESEQ, (long) &f);
		__bdos(BDOS_CLOSE, (long) &f);
	}
	mkfcb("XFCBT.DEL", &f);
	__bdos(BDOS_DELETE, (long) &f);
	mkfcb("XFCBT.DEL", &f);
	if ((__bdos(BDOS_MAKE, (long) &f) & 0xff) != 0xff) {
		__bdos(BDOS_CLOSE, (long) &f);
		mkfcb("XFCBT.DEL", &f);
		__bdos(BDOS_DELETE, (long) &f);
	}
	if (scan(&nlabel, &nsfcb) != 1) {
		cputs("XFCBT: BAD -- the XFCB did not survive a make/delete\r\n");
		bad++;
	}

	cputs(bad ? "XFCBT: FAIL\r\n" : "XFCBT: PASS\r\n");
	return (bad != 0);
}


/*  Walk every directory entry with a '?' drive search (functions 17/18),
    which is the search CP/M 3's utilities use and the only one that
    returns entries a file name can never match.  Counts XFCBs and reports
    the label and SFCB counts through its arguments.	*/

static int scan(nlabel, nsfcb)
int *nlabel;
int *nsfcb;
{
	register int	r;
	register int	t;
	register int	i;
	int		nxfcb;

	nxfcb = *nlabel = *nsfcb = 0;
	setdma(buf);
	mkfcb("???????.???", &f);
	f.drvcode = '?';
	for (i = 0; i < 12; i++)
		((char *) &f)[i] = '?';
	r = __bdos(BDOS_SFIRST, (long) &f) & 0xff;
	while (r != 0xff) {
		t = buf[(r & 3) * 32] & 0xff;
		if (t >= 0x10 && t <= 0x1f)
			nxfcb++;
		else if (t == 0x20)
			*nlabel += 1;
		else if (t == 0x21)
			*nsfcb += 1;
		r = __bdos(BDOS_SNEXT, (long) &f) & 0xff;
	}
	return (nxfcb);
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


static VOID puthex(v)
register int v;
{
	static char	hex[] = "0123456789abcdef";

	conout(hex[(v >> 4) & 0x0f]);
	conout(hex[v & 0x0f]);
}
