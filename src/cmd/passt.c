/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/*
 * passt.c - Exercise read, write, and delete passwords on armed and unarmed
 * drives.
 */

#include "cpm.h"

#define	RDFILE	"PASSR.TXT"		/* mode 80h, password RSECRET	*/
#define	WRFILE	"PASSW.TXT"		/* mode 40h, password WSECRET	*/
#define	DLFILE	"PASSD.TXT"		/* mode 20h, password DSECRET	*/

#define	DL_PASSWD	0x80		/* xfcb.lit:11, dl$password	*/

static struct fcb	f;
static struct fcb	g;
static char		rn[64];		/* rename's two-name FCB	*/
static char		buf[SECLEN];
static char		pw[SECLEN];

static int		bad;
static int		armed;		/* this image enforces		*/

static VOID	setpw();
static VOID	nopw();
static VOID	mkren();
static VOID	makefile();
static VOID	expect();
static VOID	expectok();
static VOID	report();
static VOID	puthex();

int main(argc, argv)
int argc;
char *argv[];
{
	int	r;

	/*  PASST MAKE runs FIRST, on an ordinary image, and lays down the
	    three files the XFCBs will name.  The image builder can add an
	    XFCB to a finished image but not a file, so the files
	    have to come from a session -- and a session on an unarmed
	    drive, because there is nothing to arm yet.	*/
	if (argc > 1 && (argv[1][0] & 0x5f) == 'M') {
		makefile(RDFILE);
		makefile(WRFILE);
		makefile(DLFILE);
		cputs(bad ? "PASST: FAIL\r\n" : "PASST: made\r\n");
		return (bad != 0);
	}

	armed = !(argc > 1 && (argv[1][0] & 0x5f) == 'N');
	cputs(armed ? "PASST: armed drive\r\n" : "PASST: unarmed drive\r\n");

	/* ---- function 101 says which drive this is, and it is the only
	   thing the two images disagree about ---- */
	r = __bdos(BDOS_GETLABEL, 0L) & 0xff;
	cputs("PASST: fn 101 -> ");
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

	/* ================ 80h: read protection ================ */

	/* no password at all.  On the armed drive this is error 7 and the
	   file does not open; on the unarmed one it opens. */
	nopw();
	mkfcb(RDFILE, &f);
	r = __bdos(BDOS_OPEN, (long) &f);
	report("fn 15 read-protected, no password", r, 7);

	/* a wrong password is not better than none */
	setpw("WRONG");
	mkfcb(RDFILE, &f);
	r = __bdos(BDOS_OPEN, (long) &f);
	report("fn 15 read-protected, wrong password", r, 7);

	/* the right one opens it on either drive */
	setpw("RSECRET");
	mkfcb(RDFILE, &f);
	expectok("fn 15 read-protected, right password",
		 __bdos(BDOS_OPEN, (long) &f));
	__bdos(BDOS_CLOSE, (long) &f);

	/* ================ 40h: write protection ================ */

	/* opens WITHOUT the password on either drive -- write protection
	   is not read protection (bdos30.asm:4054-4055) ... */
	nopw();
	mkfcb(WRFILE, &f);
	expectok("fn 15 write-protected, no password",
		 __bdos(BDOS_OPEN, (long) &f));

	/* ... and is then read-only, error 3 out of function 21, which is
	   xfcb$read$only riding home in FCB byte 7 (:2481, :5112-5113) */
	setdma(buf);
	r = __bdos(BDOS_WRITESEQ, (long) &f);
	report("fn 21 on that handle", r, 3);
	__bdos(BDOS_CLOSE, (long) &f);

	/* with the password it is an ordinary writable file */
	setpw("WSECRET");
	mkfcb(WRFILE, &f);
	expectok("fn 15 write-protected, right password",
		 __bdos(BDOS_OPEN, (long) &f));
	setdma(buf);
	expectok("fn 21 on THAT handle", __bdos(BDOS_WRITESEQ, (long) &f));
	__bdos(BDOS_CLOSE, (long) &f);

	/* ================ 20h: delete protection ================ */

	/* the weakest mode does not touch an open at all */
	nopw();
	mkfcb(DLFILE, &f);
	expectok("fn 15 delete-protected, no password",
		 __bdos(BDOS_OPEN, (long) &f));
	__bdos(BDOS_CLOSE, (long) &f);

	/* but it is what erase, rename and set-attributes ask for */
	nopw();
	mkfcb(DLFILE, &f);
	r = __bdos(BDOS_SETATTR, (long) &f);
	report("fn 30 delete-protected, no password", r, 7);

	nopw();
	mkren(DLFILE, "PASSX.TXT");
	r = __bdos(BDOS_RENAME, (long) rn);
	report("fn 23 delete-protected, no password", r, 7);
	if (!armed) {
		/*  On the unarmed drive that rename SUCCEEDED -- which is
		    the whole point of the control run -- so the file the
		    next two checks need is now called something else.
		    Put it back.  (Its XFCB stayed behind on the old name:
		    ren_xfcb() is gated on the label bit too, exactly as
		    v3's `if BANKED' rename arm is.)			*/
		mkren("PASSX.TXT", DLFILE);
		if ((__bdos(BDOS_RENAME, (long) rn) & 0xff) == 0xff) {
			cputs("PASST: BAD -- cannot undo the rename\r\n");
			bad++;
		}
	}

	nopw();
	mkfcb(DLFILE, &f);
	r = __bdos(BDOS_DELETE, (long) &f);
	report("fn 19 delete-protected, no password", r, 7);

	/* a WILDCARD erase that reaches a protected file is refused too:
	   ckpass() scans every XFCB the name reaches, not just the first
	   (bdos30.asm:1638-1650 checks each one in its first pass) */
	nopw();
	mkfcb("PASS?.TXT", &f);
	r = __bdos(BDOS_DELETE, (long) &f);
	report("fn 19 wildcard over a protected file", r, 7);

	__bdos(BDOS_ERRMODE, (long) ERRMODE_DEFAULT);

	if (!armed) {
		/*  Both of those erases WENT THROUGH on this drive, and
		    the wildcard one took all three files.  That is the
		    result being asserted; it also means the rest of the
		    run has nothing left to work on, so put them back. */
		makefile(RDFILE);
		makefile(WRFILE);
		makefile(DLFILE);
	}

	/* and with the password, the erase goes through and takes the
	   XFCB with it -- otherwise the password would attach itself to
	   the next file made under that name */
	setpw("DSECRET");
	mkfcb(DLFILE, &f);
	expectok("fn 19 delete-protected, right password",
		 __bdos(BDOS_DELETE, (long) &f));

	nopw();
	mkfcb(DLFILE, &g);
	if ((__bdos(BDOS_MAKE, (long) &g) & 0xff) == 0xff) {
		cputs("PASST: BAD -- cannot remake the erased name\r\n");
		bad++;
	} else {
		__bdos(BDOS_CLOSE, (long) &g);
		mkfcb(DLFILE, &g);
		if ((__bdos(BDOS_OPEN, (long) &g) & 0xff) == 0xff) {
			cputs("PASST: BAD -- the erased file's XFCB outlived it\r\n");
			bad++;
		} else {
			cputs("PASST: the XFCB went with the file\r\n");
			__bdos(BDOS_CLOSE, (long) &g);
		}
	}

	/* ---- an UNPROTECTED file on the same drive is untouched: the
	   armed bit must cost a file with no XFCB nothing at all ---- */
	nopw();
	mkfcb("PASSU.TXT", &f);
	__bdos(BDOS_DELETE, (long) &f);
	mkfcb("PASSU.TXT", &f);
	expectok("fn 22 plain file", __bdos(BDOS_MAKE, (long) &f));
	setdma(buf);
	expectok("fn 21 plain file", __bdos(BDOS_WRITESEQ, (long) &f));
	__bdos(BDOS_CLOSE, (long) &f);
	mkfcb("PASSU.TXT", &f);
	expectok("fn 15 plain file", __bdos(BDOS_OPEN, (long) &f));
	__bdos(BDOS_CLOSE, (long) &f);
	mkfcb("PASSU.TXT", &f);
	expectok("fn 30 plain file", __bdos(BDOS_SETATTR, (long) &f));
	mkren("PASSU.TXT", "PASSV.TXT");
	expectok("fn 23 plain file", __bdos(BDOS_RENAME, (long) rn));
	mkfcb("PASSV.TXT", &f);
	expectok("fn 19 plain file", __bdos(BDOS_DELETE, (long) &f));

	setdma(buf);
	cputs(bad ? "PASST: FAIL\r\n" : "PASST: PASS\r\n");
	return (bad != 0);
}


/* Eight bytes at the DMA address, blank-padded: that is a CP/M 3
   password wherever one is passed (bdos30.asm cmp$pw, set$pw). */
static VOID setpw(s)
register char *s;
{
	register int	i;

	for (i = 0; i < 8; i++)
		pw[i] = ' ';
	for (i = 0; i < 8 && s[i] != 0; i++)
		pw[i] = s[i];
	setdma(pw);
}

static VOID nopw()
{
	setpw("");
}

static VOID makefile(name)
char *name;
{
	mkfcb(name, &f);
	__bdos(BDOS_DELETE, (long) &f);
	mkfcb(name, &f);
	setdma(buf);
	if ((__bdos(BDOS_MAKE, (long) &f) & 0xff) == 0xff) {
		cputs("PASST: BAD -- cannot create ");
		cputs(name);
		cputs("\r\n");
		bad++;
		return;
	}
	buf[0] = 'a';
	__bdos(BDOS_WRITESEQ, (long) &f);
	__bdos(BDOS_CLOSE, (long) &f);
}


static VOID puthex(v)
register int v;
{
	static char	hex[] = "0123456789abcdef";

	conout(hex[(v >> 4) & 0x0f]);
	conout(hex[v & 0x0f]);
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


/* The two-image test in one line: on the armed drive expect the
   refusal, on the unarmed one expect the call to work. */
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


/* Under error mode 0FEh a refusal is <code>/255 (set$aret puts the code
   in the high byte and 0FFh in the low one, bdos30.asm:4373-4380). */
static VOID expect(what, r, code)
char *what;
int r;
int code;
{
	cputs("PASST: ");
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
	cputs("PASST: ");
	cputs(what);
	cputs(" -> ");
	putdec((unsigned) (r & 0xff));
	if ((r & 0xff) == 0xff) {
		cputs("  BAD -- must succeed");
		bad++;
	}
	cputs("\r\n");
}
