/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/*
 * dgena.c -- F1(a): a peer's new directory entry must survive our close.
 *
 * THE DIRECTORY RECORD IS THE THING TWO PROCESSES SHARE.  pdirbuf and
 * dirsecn are per-process (src/bdos/bdosdef.h: they are inside struct
 * stvars, which src/bdos/proc.c copies at every switch), and on a drive
 * with cks == 0 -- every C900 drive -- dirget() used to hand a cached
 * record back without re-reading it.  close() then writes all 128 bytes of
 * that record, so a record cached before a peer's create and written after
 * it erases the peer's entry.
 *
 * The ordering here is arranged, not hoped for: the two processes hand each
 * other XDOS flags (BDOS functions 132 and 133), so this program has its
 * record cached before DGENB creates, and closes after it.  Nothing about
 * the finding depends on the scheduler.
 *
 * THE LAYOUT IS PART OF THE TEST.  Both entries have to be in the SAME
 * 128-byte record or the stale copy does not cover the peer's entry at all,
 * so the pad loop below drives the target's entry to slot 1 of a record and
 * DGENB then reports the slot it got.  `made=2' in the transcript is what
 * says the situation was really built; the target asserts it.
 */

#include "cpm.h"

#define	BDOS_CREATEPROC	144
#define	BDOS_FLAGWAIT	132
#define	BDOS_FLAGSET	133

#define	FLGGO	1		/* A -> B: my record is cached, go create */
#define	FLGDONE	2		/* B -> A: the file is created		 */
#define	FLGEXIT	3		/* A -> B: I have looked, you may go	 */

#define	TARGET	"F1TGT.TXT"
#define	PEERF	"F1NEW.TXT"

struct pcreq { struct fcb pq_fcb; char pq_tlen; char pq_tail[128]; };

static struct pcreq	req;
static struct fcb	tgt;		/* the file this process closes	*/
static struct fcb	pad;
static struct fcb	s;
static char		rec[SECLEN];
static char		dbuf[SECLEN];
static char		nm[16];

/*  ONE LINE, ONE BDOS CALL -- see the comment in conci.c: a switch happens
    at every BDOS call return, so a line built out of several calls has the
    other process's output spliced into it and matches nothing.	 */

static char	lnbuf[80];
static int	lnlen;

static VOID lnadd(p)
register char *p;
{
	while (*p != 0 && lnlen < (int)(sizeof lnbuf) - 1)
		lnbuf[lnlen++] = *p++;
	lnbuf[lnlen] = 0;
}

static VOID lndec(n)
unsigned n;
{
	char	tmp[8];
	register int i;

	i = 0;
	do {
		tmp[i++] = (char)('0' + n % 10);
		n /= 10;
	} while (n != 0 && i < 7);
	while (i)
		if (lnlen < (int)(sizeof lnbuf) - 1)
			lnbuf[lnlen++] = tmp[--i];
		else
			i = 0;
	lnbuf[lnlen] = 0;
}

static VOID lnout()
{
	lnadd("\r\n");
	cputs(lnbuf);
	lnlen = 0;
	lnbuf[0] = 0;
}

/*  Create `name' fresh and return its directory code (slot within the
    record), or 255.  */

static int mk(name, fp)
char *name;
struct fcb *fp;
{
	mkfcb(name, fp);
	__bdos(BDOS_DELETE, (long) fp);
	mkfcb(name, fp);
	return (__bdos(BDOS_MAKE, (long) fp));
}

int main(argc, argv)
int argc;
char *argv[];
{
	register int	i, k;

	cputs("DGENA: A start\r\n");

	/*  Drive the next create to slot 0 of a record, so the target lands
	    at slot 1 and DGENB's at slot 2 of that same record.	 */
	mkfcb(PEERF, &s);
	__bdos(BDOS_DELETE, (long) &s);
	k = 255;
	for (i = 0; i < 8 && k != 0; i++) {
		nm[0] = 'F'; nm[1] = '1'; nm[2] = 'P'; nm[3] = 'A';
		nm[4] = 'D'; nm[5] = (char)('0' + i);
		nm[6] = '.'; nm[7] = 'T'; nm[8] = 'X'; nm[9] = 'T';
		nm[10] = 0;
		k = mk(nm, &pad);
	}
	if (k != 0) {
		cputs("DGENA: FAIL -- no create reached slot 0\r\n");
		return (1);
	}
	k = mk(TARGET, &tgt);
	lnadd("DGENA: slot="); lndec((unsigned) k); lnout();
	if (k != 1) {
		cputs("DGENA: FAIL -- the target is not at slot 1\r\n");
		return (1);
	}

	/*  Give it a block, so the close has a disk map to merge.  */
	for (i = 0; i < SECLEN; i++)
		rec[i] = 'a';
	setdma(rec);
	if (__bdos(BDOS_WRITESEQ, (long) &tgt) != 0) {
		cputs("DGENA: FAIL -- write error\r\n");
		return (1);
	}

	mkfcb("DGENB.Z8K", &req.pq_fcb);
	req.pq_tlen = 0;
	for (i = 0; i < 128; i++)
		req.pq_tail[i] = 0;
	if (__bdos(BDOS_CREATEPROC, (long) &req) != 0) {
		cputs("DGENA: no second process\r\n");
		return (1);
	}

	/*  CACHE THE RECORD.  A search on an exact name is the cheapest way
	    in: the signature table (src/bdos/dskhash.c) takes the scan
	    straight to the entry, so exactly one directory record is read
	    and it is the target's.  This has to happen AFTER the create
	    above, because loading DGENB.Z8K reads the disk and that clears
	    this process's directory tag.	 */
	mkfcb(TARGET, &s);
	setdma(dbuf);
	if (__bdos(BDOS_SFIRST, (long) &s) == 255) {
		cputs("DGENA: FAIL -- the target vanished\r\n");
		return (1);
	}

	__bdos(BDOS_FLAGSET, (long) FLGGO);	/* B: create now	*/
	__bdos(BDOS_FLAGWAIT, (long) FLGDONE);	/* ... and it has	*/

	cputs("DGENA: closing over the peer's record\r\n");
	__bdos(BDOS_CLOSE, (long) &tgt);

	/*  Look at the MEDIUM, not at what this process remembers of it:
	    function 13 logs every drive off, so the next reference rebuilds
	    the allocation vector and the signature table from the disk.  */
	__bdos(BDOS_RESET, 0L);
	mkfcb(PEERF, &s);
	setdma(dbuf);
	k = __bdos(BDOS_SFIRST, (long) &s);
	lnadd("DGENA: peer="); lndec(k == 255 ? 0 : 1); lnout();
	mkfcb(TARGET, &s);
	setdma(dbuf);
	k = __bdos(BDOS_SFIRST, (long) &s);
	lnadd("DGENA: own="); lndec(k == 255 ? 0 : 1); lnout();

	__bdos(BDOS_FLAGSET, (long) FLGEXIT);
	cputs("DGENA: A done\r\n");
	return (0);
}
