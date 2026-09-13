/*
 * rot.c - A refusal must not mutate.  Four ways the BDOS reported an
 * error to the program and changed the protected object anyway:
 *
 *   (a) function 45 return-error mode.  error(5) RETURNS in modes 0FEh
 *	 and 0FFh, so delete, rename and truncate carried on past the
 *	 read-only attribute.  Each check here is in two halves: the
 *	 code the call returns, and the object afterwards.
 *   (b) function 15's user-0 SYS fallback re-scanned without a second
 *	 password check, so a read-protected user-0 file opened from
 *	 another user area with no password at all.
 *   (c) function 99 never asked chk$password, so a protected file could
 *	 be truncated by anyone.
 *   (d) function 103 was in neither dispatcher preflight group, so it
 *	 wrote an XFCB onto a drive the program had marked read-only.
 *
 * ROT MAKE lays the files down on an ordinary image (mkcpmfs.py can add
 * an XFCB to a finished image but not a file, exactly as PASST MAKE).
 * ROT then runs on the armed image and ROT NONE on the control image
 * whose label differs in the password bit alone.
 */

#include "cpm.h"

#define	BDOS_SETUSER	32
#define	BDOS_WRPROT	28		/* write protect the default drive */
#define	BDOS_FILESIZE	35
#define	BDOS_TRUNC	99
#define	BDOS_WRXFCB	103
#define	BDOS_DFLTPW	106

#define	DELFILE	"ROTD.TXT"		/* read-only, delete victim	*/
#define	RENFILE	"ROTR.TXT"		/* read-only, rename victim	*/
#define	RENNEW	"ROTRX.TXT"
#define	TRNFILE	"ROTT.TXT"		/* read-only, truncate victim	*/
#define	PWFILE	"ROTP.TXT"		/* delete-protected, fn 99	*/
#define	PWFILE2	"ROTQ.TXT"		/* the same, WITH the password	*/
#define	SYSFILE	"ROTS.TXT"		/* user 0, SYS, read-protected	*/
#define	SYSPLAIN "ROTN.TXT"		/* user 0, SYS, no password	*/
#define	XFCBFILE "ROTX.TXT"		/* fn 103 victim		*/

#define	NREC	200			/* two directory entries	*/
#define	KEEP	50			/* the cut fn 99 is asked for	*/
#define	TESTUSER 3

static struct fcb	f;
static struct fcb	g;
static char		rn[64];		/* rename's two-name FCB	*/
static char		buf[SECLEN];
static char		pw[SECLEN];

static int		bad;
static int		armed;

static VOID	makefile();
static VOID	setpw();
static VOID	nopw();
static VOID	mkren();
static VOID	setran();
static long	fsize();
static int	openf();
static VOID	expect();
static VOID	expectok();
static VOID	report();
static VOID	sizeis();
static VOID	puthex();
static VOID	putlong();

int main(argc, argv)
int argc;
char *argv[];
{
	register int	r;

	setdma(buf);

	if (argc > 1 && (argv[1][0] & 0x5f) == 'M') {
		makefile(DELFILE, 1);
		makefile(RENFILE, 1);
		makefile(TRNFILE, NREC);
		makefile(PWFILE, NREC);
		makefile(PWFILE2, NREC);
		makefile(SYSFILE, 1);
		makefile(SYSPLAIN, 1);
		makefile(XFCBFILE, 1);
		cputs(bad ? "ROT: FAIL\r\n" : "ROT: made\r\n");
		return (bad != 0);
	}

	armed = !(argc > 1 && (argv[1][0] & 0x5f) == 'N');
	cputs(armed ? "ROT: armed drive\r\n" : "ROT: unarmed drive\r\n");
	r = __bdos(BDOS_GETLABEL, 0L) & 0xff;
	cputs("ROT: fn 101 -> ");
	puthex(r);
	if (armed && !(r & DL_PASSWD)) {
		cputs("  BAD -- the label does not arm passwords");
		bad++;
	}
	if (!armed && (r & DL_PASSWD)) {
		cputs("  BAD -- this control image IS armed");
		bad++;
	}
	cputs("\r\n");

	__bdos(BDOS_ERRMODE, (long) ERRMODE_DISPRET);

	/* ================ (a) the read-only ATTRIBUTE ================
	   The three files carry R on the disk, put there by the fixture.
	   Arming has nothing to do with this leg, so both runs expect
	   the same refusals -- and the same files afterwards.	*/

	nopw();
	mkfcb(DELFILE, &f);
	expect("fn 19 on a read-only file", __bdos(BDOS_DELETE, (long) &f), 3);
	if (openf(DELFILE) == 0xff) {
		cputs("ROT: BAD -- ROTD.TXT WAS DELETED by the call that refused\r\n");
		bad++;
	} else {
		cputs("ROT: ROTD.TXT is still there\r\n");
		__bdos(BDOS_CLOSE, (long) &f);
	}

	mkren(RENFILE, RENNEW);
	expect("fn 23 on a read-only file", __bdos(BDOS_RENAME, (long) rn), 3);
	if (openf(RENFILE) == 0xff) {
		cputs("ROT: BAD -- ROTR.TXT WAS RENAMED by the call that refused\r\n");
		bad++;
	} else {
		cputs("ROT: ROTR.TXT still answers to its own name\r\n");
		__bdos(BDOS_CLOSE, (long) &f);
	}
	if (openf(RENNEW) != 0xff) {
		cputs("ROT: BAD -- ROTRX.TXT exists: the rename went through\r\n");
		bad++;
		__bdos(BDOS_CLOSE, (long) &f);
	}

	sizeis("ROTT.TXT before", TRNFILE, (long) NREC);
	mkfcb(TRNFILE, &f);
	setran((long) (KEEP - 1));
	expect("fn 99 on a read-only file", __bdos(BDOS_TRUNC, (long) &f), 3);
	sizeis("ROTT.TXT after", TRNFILE, (long) NREC);

	/* ================ (c) fn 99 and the PASSWORD ================ */

	nopw();
	sizeis("ROTP.TXT before", PWFILE, (long) NREC);
	mkfcb(PWFILE, &f);
	setran((long) (KEEP - 1));
	report("fn 99 on a password-protected file",
	       __bdos(BDOS_TRUNC, (long) &f), 7);
	sizeis("ROTP.TXT after", PWFILE,
	       armed ? (long) NREC : (long) KEEP);

	/* and WITH the password it truncates on either drive: the check
	   has to be a password check, not a refusal to truncate	*/
	setpw("QSECRET");
	__bdos(BDOS_DFLTPW, (long) pw);
	nopw();
	mkfcb(PWFILE2, &f);
	setran((long) (KEEP - 1));
	expectok("fn 99 with the password", __bdos(BDOS_TRUNC, (long) &f));
	sizeis("ROTQ.TXT after", PWFILE2, (long) KEEP);

	/* ================ (b) the user-0 SYS fallback ================ */

	setpw("");
	__bdos(BDOS_DFLTPW, (long) pw);	/* no default password	*/
	nopw();
	__bdos(BDOS_SETUSER, (long) TESTUSER);

	r = openf(SYSPLAIN);
	cputs("ROT: user 3 open of an unprotected user-0 SYS file -> ");
	puthex(r);
	if (r == 0xff) {
		cputs("  BAD -- the fallback itself is broken");
		bad++;
	} else
		__bdos(BDOS_CLOSE, (long) &f);
	cputs("\r\n");

	nopw();				/* the password the caller has not got */
	mkfcb(SYSFILE, &f);
	report("fn 15 from user 3, read-protected user-0 SYS file",
	       __bdos(BDOS_OPEN, (long) &f), 7);
	if (!armed)
		__bdos(BDOS_CLOSE, (long) &f);

	/* with the default password the same open must work from user 3:
	   the fix must gate the fallback, not close it	*/
	setpw("SSECRET");
	__bdos(BDOS_DFLTPW, (long) pw);
	nopw();
	mkfcb(SYSFILE, &f);
	expectok("fn 15 from user 3 with the default password",
		 __bdos(BDOS_OPEN, (long) &f));
	__bdos(BDOS_CLOSE, (long) &f);
	setpw("");
	__bdos(BDOS_DFLTPW, (long) pw);

	__bdos(BDOS_SETUSER, 0L);
	cputs("ROT: user ");
	putdec((unsigned) (__bdos(BDOS_SETUSER, 0xffL) & 0xff));
	cputs(" again\r\n");

	/* ============ (d) fn 103 onto a read-only DRIVE ============
	   Last, because it leaves the drive read-only.  The refusal is
	   the dispatcher's, so it is error 2 and it does not depend on
	   the label: on BOTH images nothing may be written.	*/

	nopw();
	__bdos(BDOS_WRPROT, 0L);
	cputs("ROT: fn 29 -> ");
	puthex(__bdos(BDOS_ROVEC, 0L) & 0xff);
	cputs("\r\n");
	setpw("");			/* current password: none	*/
	pw[8] = 'X';			/* the new one, at DMA+8	*/
	pw[9] = 'S'; pw[10] = 'E'; pw[11] = 'C';
	pw[12] = 'R'; pw[13] = 'E'; pw[14] = 'T'; pw[15] = ' ';
	setdma(pw);
	mkfcb(XFCBFILE, &f);
	f.extent = (char) 0x81;		/* assign a password, mode READ	*/
	expect("fn 103 on a read-only drive",
	       __bdos(BDOS_WRXFCB, (long) &f), 2);

	/*  and the object: if the XFCB went down anyway it carries mode
	    READ, so this open -- with eight blanks for a password -- would
	    be refused.  The refusal has to have cost the file nothing.	*/
	nopw();
	mkfcb(XFCBFILE, &f);
	expectok("fn 15 on the file fn 103 was pointed at",
		 __bdos(BDOS_OPEN, (long) &f));
	__bdos(BDOS_CLOSE, (long) &f);
	setdma(buf);

	__bdos(BDOS_ERRMODE, (long) ERRMODE_DEFAULT);
	cputs(bad ? "ROT: FAIL\r\n" : "ROT: PASS\r\n");
	return (bad != 0);
}


/*  nrec records, each stamped with its own number so that a truncation
    that happened can be told from one that did not.	*/

static VOID makefile(name, nrec)
char *name;
int nrec;
{
	register int	i;

	mkfcb(name, &f);
	__bdos(BDOS_DELETE, (long) &f);
	mkfcb(name, &f);
	setdma(buf);
	if ((__bdos(BDOS_MAKE, (long) &f) & 0xff) == 0xff) {
		cputs("ROT: BAD -- cannot create ");
		cputs(name);
		cputs("\r\n");
		bad++;
		return;
	}
	for (i = 0; i < nrec; i++) {
		buf[0] = (char) (i & 0xff);
		buf[1] = (char) ((i >> 8) & 0xff);
		if (__bdos(BDOS_WRITESEQ, (long) &f) & 0xff) {
			cputs("ROT: BAD -- write failed on ");
			cputs(name);
			cputs("\r\n");
			bad++;
			break;
		}
	}
	__bdos(BDOS_CLOSE, (long) &f);
}


/* Eight blank-padded bytes at the DMA address: a CP/M 3 password. */
static VOID setpw(s)
register char *s;
{
	register int	i;

	for (i = 0; i < 16; i++)
		pw[i] = ' ';
	for (i = 0; i < 8 && s[i] != 0; i++)
		pw[i] = s[i];
	setdma(pw);
}

/* Eight blanks at the DMA address is what "no password" looks like to
   cmp$pw, and it is not the same thing as leaving the record buffer
   there: whatever is in the buffer would be read as a password. */
static VOID nopw()
{
	setpw("");
}


static VOID mkren(from, to)
char *from;
char *to;
{
	register int	i;

	mkfcb(from, &f);
	mkfcb(to, &g);
	for (i = 0; i < 16; i++) {
		rn[i] = ((char *) &f)[i];
		rn[16 + i] = ((char *) &g)[i];
	}
	for (i = 32; i < 36; i++)
		rn[i] = 0;
}


static VOID setran(r)
long r;
{
	f.ran0 = (char) ((r >> 16) & 0xff);
	f.ran1 = (char) ((r >> 8) & 0xff);
	f.ran2 = (char) (r & 0xff);
}


static long fsize(name)
char *name;
{
	mkfcb(name, &g);
	__bdos(BDOS_FILESIZE, (long) &g);
	return (((long) (g.ran0 & 0xff) << 16) | ((long) (g.ran1 & 0xff) << 8)
		| (long) (g.ran2 & 0xff));
}


static int openf(name)
char *name;
{
	mkfcb(name, &f);
	return (__bdos(BDOS_OPEN, (long) &f) & 0xff);
}


/*  Function 35 is the witness this program can carry: the record count
    the directory adds up to.  The disk itself is checked by the recipe
    afterwards, which does not share a BDOS with this program.	*/

static VOID sizeis(what, name, want)
char *what;
char *name;
long want;
{
	long	sz;

	sz = fsize(name);
	cputs("ROT: ");
	cputs(what);
	cputs(" -> ");
	putlong(sz);
	cputs(" records");
	if (sz != want) {
		cputs("  BAD -- want ");
		putlong(want);
		bad++;
	}
	cputs("\r\n");
}


/* Under error modes 0FEh and 0FFh a refusal is <code>/255. */
static VOID expect(what, r, code)
char *what;
int r;
int code;
{
	cputs("ROT: ");
	cputs(what);
	cputs(" -> ");
	putdec((unsigned) ((r >> 8) & 0xff));
	cputs("/");
	putdec((unsigned) (r & 0xff));
	if (((r >> 8) & 0xff) != code || (r & 0xff) != 0xff) {
		cputs("  BAD -- want ");
		putdec((unsigned) code);
		cputs("/255");
		bad++;
	}
	cputs("\r\n");
}


static VOID expectok(what, r)
char *what;
int r;
{
	cputs("ROT: ");
	cputs(what);
	cputs(" -> ");
	putdec((unsigned) (r & 0xff));
	if ((r & 0xff) == 0xff) {
		cputs("  BAD -- must succeed");
		bad++;
	}
	cputs("\r\n");
}


/* armed: the refusal.  unarmed: the same call, unenforced. */
static VOID report(what, r, code)
char *what;
int r;
int code;
{
	if (armed)
		expect(what, r, code);
	else
		expectok(what, r);
}


static VOID puthex(v)
register int v;
{
	static char	hex[] = "0123456789abcdef";

	conout(hex[(v >> 4) & 0x0f]);
	conout(hex[v & 0x0f]);
}


static VOID putlong(n)
long n;
{
	if (n >= 10L)
		putlong(n / 10L);
	conout((int) ('0' + n % 10L));
}
