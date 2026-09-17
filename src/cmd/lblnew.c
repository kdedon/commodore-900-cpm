/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/*
 * lblnew.c - Create a directory label above existing files, verify its
 * stamps, and reject stamping on a drive without SFCBs.
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
static int	setlabel();
static int	getlabel();
static VOID	makefile();
static VOID	writefile();
static VOID	puthex();

int main(argc, argv)
int argc;
char *argv[];
{
	register int	r;
	register int	i;

	if (argc > 1 && (argv[1][0] & 0x5f) == 'P') {
		/* ---- a label THIS BDOS did not write ----
		   The password-enable bit reaches drvlbl[] through the
		   login scan, which copies byte 12 of the type-20h entry
		   out of the directory verbatim, and function 101 must
		   report it: it is the switch the BDOS arms every
		   password check off, so a program that asks whether this
		   drive requires passwords has to be told the truth.
		   v3 answers through get$dir$mode, which masks the bit
		   off ONLY in the build that has no password support
		   (bdos30.asm:3119-3126, `if not BANKED / ani 7fh').  We
		   used to be that build and are not any more.

		   The image this runs on is labelled by
		   tools/mkcpmfs.py --label-mode ...,password, and the
		   Makefile proves host-side that the bit really is on
		   the medium -- otherwise this would pass on any disk. */
		r = getlabel();
		cputs("LBLNEW: fn 101 on a password-labelled drive -> ");
		puthex(r);
		if (!(r & DL_EXISTS)) {
			cputs("  BAD -- no label on this image");
			bad++;
		} else if (!(r & DL_PASSWD)) {
			cputs("  BAD -- want the password bit reported");
			bad++;
		}
		cputs("\r\n");
		cputs(bad ? "LBLNEW: FAIL\r\n" : "LBLNEW: PASS\r\n");
		return (bad != 0);
	}

	if (getlabel() != 0) {
		cputs("LBLNEW: BAD -- this image already has a label\r\n");
		return (1);
	}
	cputs("LBLNEW: fn 101 on an unlabelled drive -> 00\r\n");
	settime(DAY1, 0x12, 0x34);

	if (argc > 1) {
		/* ---- no SFCBs: stamping may not be switched on ---- */
		r = setlabel("C900X", DL_ACCESS | DL_UPDATE | DL_CREATE);
		cputs("LBLNEW: fn 100 stamping on a drive with no SFCBs -> ");
		puthex(r);
		if (r != 0xff) {
			cputs("  BAD -- must refuse");
			bad++;
		}
		cputs("\r\n");
		if ((r = getlabel()) != 0) {
			cputs("LBLNEW: BAD -- a refused fn 100 left a label ");
			puthex(r);
			cputs("\r\n");
			bad++;
		}
		/* a label with no stamping in it is still allowed */
		r = setlabel("C900X", 0);
		cputs("LBLNEW: fn 100 plain label -> ");
		puthex(r);
		cputs("  fn 101 now ");
		r = getlabel();
		puthex(r);
		if (r != DL_EXISTS) {
			cputs("  BAD -- want 01");
			bad++;
		}
		cputs("\r\n");
		makefile("LBLNEW.TXT");
		mkfcb("LBLNEW.TXT", &f);
		setdma(stamps);
		if ((__bdos(BDOS_RDSTAMPS, (long) &f) & 0xff) == 0xff) {
			cputs("LBLNEW: BAD -- fn 102 found no file\r\n");
			bad++;
		} else {
			for (i = 0; i < 8; i++)
				if (stamps[i] != 0)
					break;
			cputs("LBLNEW: fn 102 with no SFCB -> ");
			cputs(i == 8 ? "all zero\r\n" : "BAD -- stamped\r\n");
			if (i != 8)
				bad++;
		}
		setdma(buf);
		cputs(bad ? "LBLNEW: FAIL\r\n" : "LBLNEW: PASS\r\n");
		return (bad != 0);
	}

	/* ---- the file first, so the label lands above every file ---- */
	makefile("LBLNEW.TXT");

	/* ---- now make the label, with every bit asked for ---- */
	r = setlabel("C900N", DL_PASSWD | DL_ACCESS | DL_UPDATE | DL_CREATE);
	cputs("LBLNEW: fn 100 made a label -> ");
	puthex(r);
	if (r != 0) {
		cputs("  BAD -- refused\r\n");
		bad++;
		return (1);
	}
	r = getlabel();
	cputs("  fn 101 now ");
	puthex(r);
	/* every bit asked for comes back, the password bit included: it is
	   stored now, not dropped, and DL_EXISTS is added by the BDOS */
	if (r != (DL_PASSWD | DL_ACCESS | DL_UPDATE | DL_CREATE | DL_EXISTS)) {
		cputs("  BAD -- want f1");
		bad++;
	}
	cputs("\r\n");

	/* ---- the new label is live: writing the file now must stamp it,
	   and a write claims no directory entry, so the label stays the
	   topmost one on the drive ---- */
	writefile("LBLNEW.TXT");
	mkfcb("LBLNEW.TXT", &f);
	setdma(stamps);
	if ((__bdos(BDOS_RDSTAMPS, (long) &f) & 0xff) == 0xff) {
		cputs("LBLNEW: BAD -- fn 102 found no file\r\n");
		bad++;
	} else {
		cputs("LBLNEW: the new label stamps: create ");
		putdec((unsigned) ((stamps[0] & 0xff) | ((stamps[1] & 0xff) << 8)));
		cputs(" update ");
		putdec((unsigned) ((stamps[4] & 0xff) | ((stamps[5] & 0xff) << 8)));
		if ((stamps[0] & 0xff) != (DAY1 & 0xff)
		    || (stamps[1] & 0xff) != ((DAY1 >> 8) & 0xff)
		    || (stamps[2] & 0xff) != 0x12 || (stamps[3] & 0xff) != 0x34
		    || (stamps[4] & 0xff) != (DAY1 & 0xff)
		    || (stamps[5] & 0xff) != ((DAY1 >> 8) & 0xff)
		    || (stamps[6] & 0xff) != 0x12 || (stamps[7] & 0xff) != 0x34) {
			cputs("  BAD -- the file is not stamped under the new label");
			bad++;
		}
		cputs("\r\n");
	}
	setdma(buf);

	/* ---- rewrite it a day later: only the label's UPDATE stamp moves ---- */
	settime(DAY2, 0x08, 0x00);
	r = setlabel("C900N", DL_ACCESS | DL_UPDATE | DL_CREATE);
	cputs("LBLNEW: fn 100 rewrote the label -> ");
	puthex(r);
	cputs("  fn 101 now ");
	r = getlabel();
	puthex(r);
	if (r != (DL_ACCESS | DL_UPDATE | DL_CREATE | DL_EXISTS)) {
		cputs("  BAD");
		bad++;
	}
	cputs("\r\n");

	cputs(bad ? "LBLNEW: FAIL\r\n" : "LBLNEW: PASS\r\n");
	return (bad != 0);
}


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


/* function 100: the FCB names the label, its extent byte carries the mode */
static int setlabel(name, mode)
char *name;
int mode;
{
	mkfcb(name, &f);
	f.extent = (char) mode;
	return (__bdos(BDOS_SETLABEL, (long) &f) & 0xff);
}


static int getlabel()
{
	return (__bdos(BDOS_GETLABEL, 0L) & 0xff);
}


static VOID makefile(name)
char *name;
{
	mkfcb(name, &f);
	__bdos(BDOS_DELETE, (long) &f);
	mkfcb(name, &f);
	setdma(buf);
	if ((__bdos(BDOS_MAKE, (long) &f) & 0xff) == 0xff) {
		cputs("LBLNEW: BAD -- cannot create the test file\r\n");
		bad++;
		return;
	}
	buf[0] = 'a';
	__bdos(BDOS_WRITESEQ, (long) &f);
	__bdos(BDOS_CLOSE, (long) &f);
}


/*  Open an existing file and write its first record: the access stamp
    (this label has bit 40h) and the update stamp both land on it, and no
    directory slot is claimed.				*/

static VOID writefile(name)
char *name;
{
	mkfcb(name, &f);
	setdma(buf);
	if ((__bdos(BDOS_OPEN, (long) &f) & 0xff) == 0xff) {
		cputs("LBLNEW: BAD -- cannot reopen the test file\r\n");
		bad++;
		return;
	}
	buf[0] = 'b';
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
