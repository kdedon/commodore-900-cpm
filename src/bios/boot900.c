/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/* BIOS cold start: the drive and console tables and the devices' first
 * setup.  Linked into the boot module, which only cold boot runs. */
#include "romabi.h"
#include "boottrace.h"
#include "c900cfg.h"
#include "stdio.h"
#include "bdosdef.h"
#include "bcb.h"
#include <bootinfo.h>
#include "bios900.h"

/* The version this BIOS asks the loader for, and the block length that goes
 * with it.  It is this system's choice, not part of the handoff layout. */
#if	BI_VERSION >= 5
#define	BI_ASK		5
#define	BI_ASKLEN	BI_LEN5
#else
#define	BI_ASK		4
#define	BI_ASKLEN	BI_LEN4
#endif

/* Keep this externally patched handoff in initialized data: kboot scans
 * the staged l.out data segment for its magic before launching the image. */
struct bootinfo bootinf = {
	BI_MAGIC,
	BI_ASK,			/* the version asked of the loader		*/
	(unsigned short)BI_ASKLEN,
	BI_NPART,
	0,			/* bi_sum */
	BI_SRC_KERNEL,		/* until a loader says otherwise */
	0, 0L, 0L,		/* swapdev/bot/top: CP/M has no swap */
	{ { 0L, 0L }, { 0L, 0L }, { 0L, 0L }, { 0L, 0L },
	  { 0L, 0L }, { 0L, 0L }, { 0L, 0L }, { 0L, 0L },
	  { 0L, 0L }, { 0L, 0L }, { 0L, 0L }, { 0L, 0L },
	  { 0L, 0L }, { 0L, 0L }, { 0L, 0L }, { 0L, 0L } },
	0,			/* bi_flags */
	BI_CON_ANY,		/* bi_console */
	0			/* bi_serial: nothing was probed */
};

extern int inb();
extern outb();

/* Fallback layout: A: spans 20480 blocks at 38144; B: spans 16384 at
 * 59136. The intervening 512 blocks hold CPM.SYS. */
#define CPMABASE	38144L		/* cpma partition base block */
#define CPMABLKS	20480L		/* 2560 x 4096 = 10 MB */
#define BTRKOFF		1312L		/* B:'s old track offset within cpma */
#define CPMBBASE	(CPMABASE + BTRKOFF * 16L)
#define CPMBBLKS	16384L		/* 2048 x 4096 = 8 MB */

static struct bipart dflpart[NDRIVE] = {
	{ CPMABASE, CPMABLKS },		/* A: */
	{ CPMBBASE, CPMBBLKS },		/* B: */
	{ 0L, 0L }, { 0L, 0L }, { 0L, 0L }, { 0L, 0L }, { 0L, 0L }
};

/*
 * A slot smaller than this cannot be a CP/M drive here: DRM 511 with
 * dir_al 0xF000 reserves four 4096-byte blocks for the directory, so a
 * drive needs those plus data.  64 blocks (32 KB) is 8 allocation blocks,
 * half directory and half data -- useless but coherent, and the point of
 * the limit is to refuse a slot that would give a NEGATIVE dsm.
 */
#define MINBCNT		64L
/* dsm is a UWORD, so no drive can be longer than 65536 allocation blocks. */
#define MAXBCNT		524288L

static drvsay(i, why)
int i;
char *why;
{
	puts("BIOS: drive ");
	putchar('A' + i);
	puts(": ignored -- ");
	puts(why);
	putchar('\n');
}

/* Accept loader-supplied blocks only with a recognized version, matching
 * length, and valid checksum. Drive and console consumers validate their
 * respective fields separately. */
static int bivalid()
{
	if (bootinf.bi_src != BI_SRC_KBOOT)
		return (0);
	/* ANY version this header knows is a handoff: a block says which
	 * version it is and how long it is, and bilen() is the agreement
	 * between the two.  What is not there is decided from the LENGTH,
	 * below, and never from the number -- kboot's own rule, and the
	 * reason a v3 loader and a v4 one can both hand this BIOS a table. */
	if (bilen(bootinf.bi_version) == 0)
		return (0);
	if (bootinf.bi_len != bilen(bootinf.bi_version))
		return (0);
	if (bisum(&bootinf) != 0)
		return (0);
	return (1);
}

/* Require a drive A: slot before using the handoff's partition table.
 * A valid handoff without CP/M drives can still supply console metadata. */
static int bigood()
{
	if (!bivalid())
		return (0);
	if (bootinf.bi_npart <= BIDRV0)
		return (0);
	if (bootinf.bi_part[BIDRV0].bcount == 0L)
		return (0);
	return (1);
}

/*
 * Build the drive table.  Cold start only: a warm boot does not re-read
 * the medium's layout, and the BDOS holds pointers into dphtab[].
 */
static drvinit()
{
	register int i;
	struct bipart *pp;
	long start, count, dsm;
	int alvlen, used;
	int fromkboot;

	fromkboot = bigood();
	used = 0;
	for (i = 0; i < NDRIVE; i++) {
		drvthere[i] = 0;
		drvbase[i] = 0L;
		pp = fromkboot ? &bootinf.bi_part[BIDRV0 + i] : &dflpart[i];
		start = pp->bstart;
		count = pp->bcount;
		if (count == 0L)
			continue;		/* that letter is not there */
		if (count < MINBCNT) {
			drvsay(i, "the slot is too small to hold a directory");
			continue;
		}
		if (count > MAXBCNT)
			count = MAXBCNT;	/* CP/M cannot address the rest */
		dsm = count / BLSBLKS - 1L;
		alvlen = (int)(dsm >> 3) + 1;
		if (used + alvlen > ALVPOOL) {
			drvsay(i, "no room left for its allocation vector");
			continue;
		}
		/* BLS 4096 (bsh 5, blm 31), DRM 511 with four directory
		 * blocks reserved (dir_al 0xF000), fixed disk (CKS 0), no
		 * system tracks (trk_off 0: the base is kept here).  EXM is
		 * 1 for BLS 4096 with DSM > 255 and 3 below that, which is
		 * CP/M's own table and not a choice. */
		dpbtab[i].spt = ASPT;
		dpbtab[i].bsh = 5;
		dpbtab[i].blm = 31;
		dpbtab[i].exm = (dsm < 256L) ? 3 : 1;
		dpbtab[i].dpbdum = 0;
		dpbtab[i].dsm = (UWORD)dsm;
		dpbtab[i].drm = 511;
		dpbtab[i].dir_al = 0xF000;
		dpbtab[i].cks = 0;
		dpbtab[i].trk_off = 0;
		dphtab[i].xlt = (UBYTE *)0;
		dphtab[i].hiwater = 0;
		dphtab[i].dum1 = 0;
		dphtab[i].dum2 = 0;
		dphtab[i].dbufp = dirbuf;
		dphtab[i].dpbp = &dpbtab[i];
		dphtab[i].csv = (UBYTE *)0;
		dphtab[i].alv = &alvpool[used];
		used += alvlen;
		drvbase[i] = start;
		drvthere[i] = 1;
	}
	/* Somewhere to point before the first SELDSK.  The BDOS always
	 * selects before it transfers (iosys.c do_phio), so this is a
	 * defence against a caller that does not, not a working default. */
	curbase = drvbase[0];
}

/* Configure a spare serial channel for eight data bits, one stop bit,
 * no parity, and BRG clocking. Arm receive interrupts after enabling it. */
static sccinit(chan)
int chan;
{
	if (sccport(chan, 0) == 0)
		return;			/* no address for that channel */
	outb(sccport(chan, 4), 0x44);	/* x16 clock, 1 stop bit, no parity */
	outb(sccport(chan, 3), 0xc0);	/* Rx 8 bits/char, receiver off	    */
	outb(sccport(chan, 5), 0x60);	/* Tx 8 bits/char, transmitter off  */
	outb(sccport(chan, 11), 0x50);	/* Rx and Tx clock from the BRG	    */
	outb(sccport(chan, 12), 0x03);	/* BRG divisor low  = 38,400 baud   */
	outb(sccport(chan, 13), 0x00);	/*   and high			    */
	outb(sccport(chan, 14), 0x03);	/* BRG source = PCLK, BRG enable    */
	outb(sccport(chan, 3), 0xc1);	/* receiver ON			    */
	outb(sccport(chan, 5), 0x68);	/* transmitter ON		    */
	rxarm(chan);			/* and its receive ring (C8)	    */
}

/* Build consoles from bi_serial at cold boot. Console 0 is whatever
 * crsinit() chose; other fitted channels are bound in order up to CONMAX.
 * When console 0 is video, channel 0 (SCC-B, the ROM's console line) is a
 * free terminal and becomes console 1; the spare channels follow it.
 * Missing/older handoffs and zero bitmaps use the on-board channel map. */
#define CONDFLMAP	0x0003		/* channels 0 and 1: the on-board SCC */

static coninit()
{
	register int chan;
	unsigned int map;

	map = CONDFLMAP;
	if (bivalid() && bootinf.bi_len >= (unsigned short)BI_LEN4
	    && bootinf.bi_serial != 0)
		map = bootinf.bi_serial;
	conmap = map;

	/*  Program CIO #1 port A for the keyboard, once, here -- the ROM
	 *  does it only inside its own getchar(), which we never call.
	 *  Without this kbdpoll() reads a port that was never set up.  */
	if (convid)
		kbdinit();

	/*  CHANNEL 0 IS WRITTEN BY US, AND NOBODY MAY HAVE SET IT UP.
	 *
	 *  Two paths drive SCC channel 0 without going anywhere near the
	 *  ROM: crsr.c's crtty() for a serial console, and conout() below
	 *  for console 1 at a video console (condev == CD_ROM with
	 *  CONRAW0(), which resolves to sccport(0, ...)).  Neither is
	 *  covered by whatever the ROM did for its OWN console.
	 *
	 *  On a machine whose ROM believes it is a video console the ROM
	 *  never programmed the SCC at all, so the transmitter is disabled,
	 *  TXEMPTY never asserts, crtty()'s bounded wait expires and every
	 *  byte is discarded in silence.  conpoll() only ever READ this
	 *  channel, which is why the fault never showed on input.
	 *
	 *  crsromser() asks the ROM'S OWN flags whether the ROM is on the
	 *  serial line.  If it is, it configured this channel and we leave
	 *  its settings -- the baud above all -- alone, so a serial machine
	 *  that works today is not touched.  If it is not, nothing has
	 *  programmed the channel and this is the only reason it can ever
	 *  transmit.  rxarm() inside sccinit() refuses channel 0, so no
	 *  interrupt is armed on the ROM's line: it stays polled.
	 */
	if (!crsromser())
		sccinit(0);

	for (chan = 0; chan < CONMAX; chan++) {
		condev[chan] = CD_NONE;
		pend[chan] = 0;
	}
	condev[0] = CD_ROM;		/* whatever the ROM chose	   */
	ncon = 1;
	/*  The AUX device starts on the SAME channel console 1 does --
	 *  the first spare port, 0x0120 on this machine.  That is not two
	 *  owners of one wire by accident: there is only one spare wire, and
	 *  which of the two owns it is the program's choice, made with
	 *  function 28 (`CONDEV(1, CD_NONE)') at the moment a transfer
	 *  starts.  Binding AUX somewhere else, or nowhere, is function 32.  */
	auxchan = -1;
	/*  SCC-B as console 1 at a video console.  Not sccinit(): the
	 *  ROM configured this line, and it stays polled (CONRAW0).  AUX
	 *  still starts on the first SPARE channel below, 0x0120.  */
	if (convid && (map & 1) != 0) {
		condev[ncon] = CD_ROM;		/* channel 0, raw: CONRAW0() */
		ncon++;
	}
	for (chan = 1; chan < NCHAN && ncon < CONMAX; chan++) {
		if ((map & (1 << chan)) == 0)
			continue;
		/*
		 * bi_serial is sixteen bits and this BIOS has addresses for
		 * six channels (sccbase), so a loader that reports bit 6 or
		 * above names a port we cannot reach.  Binding it would give
		 * a console every operation is a no-op on and -- the reason
		 * the check is here rather than left to taste -- would set
		 * auxchan to an index PAST THE END of rxbuf/rxhead/rxtail,
		 * which auxist()/auxin() then read (they are indexed, not
		 * guarded).  Function 28's CONDEV path already refuses the
		 * same thing; cold boot has to as well.
		 */
		if (sccport(chan, 0) == 0)
			continue;
		condev[ncon] = CD_SER(chan);	/* console 1 = the first spare */
		sccinit(chan);			/* a no-op with no address    */
		if (auxchan < 0)
			auxchan = chan;		/* ...and so is the AUX line */
		ncon++;
	}
}

biosinit()
{
	extern int mapseg();

	BTRACE("<1>");		/* binit (BIOS fn 0) entered */
	/* Console type as kboot decided it, in bi_console, and never the
	 * ROM's flags (crsinit.c).  No handoff, or one older than
	 * version 3, carries no bi_console and is passed as BI_CON_ANY:
	 * nobody decided, so crsinit() falls back to probing the cards. */
	convid = (crsinit((bivalid() && bootinf.bi_len >= (unsigned short)BI_LEN3)
			  ? (int)bootinf.bi_console : BI_CON_ANY) != 0);
	drvinit();			/* the drive table: the loader bootinfo,
					 * or the compiled fallback */
	coninit();			/* the console table: the loader's
					 * bi_serial, or the known map */
	iobyte = 0;
	/* split-I/D shim banks: pgmld's loadseg copies a 0xEE0B program's
	 * D segments into SPLITDSEG before spload() runs, so both shim
	 * segments must be mapped from boot, not at scan time */
	mapseg(SPLITDSEG, SPLITDPAGE, 2);
	mapseg(SPLITTSEG, SPLITTPAGE, 2);
	/* size the segment pool from the ROM's memory report -- before
	 * anything can ask, and answering zero on a 512 KB machine */
#if	BI_VERSION >= 5
	if (bivalid() && bootinf.bi_len >= (unsigned short)BI_LEN5
	    && bootinf.bi_nram <= BI_NRAM)
		pginit((unsigned short *)bootinf.bi_ram, (int)bootinf.bi_nram);
	else
#endif
		pginit((unsigned short *)0, 0);
	rtcinit();			/* CIO #1 Port B/PC1 -> MSM58321 */
	tickinit();			/* CIO #1 C/T 3 -> the 100 Hz tick */
	wdinit900();
	bcbinit();
	curix = -1;
	dskerr = 0;
}
