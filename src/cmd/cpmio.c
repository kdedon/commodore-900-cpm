/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/*
 * cpmio.c - CP/M-8000 platform layer and driver for E-Kermit 1.8.
 *
 *   KERMIT R          receive
 *   KERMIT S FILE.EXT send one file
 *   KERMIT RT / ST    receive or send in text mode
 *
 * Data uses the auxiliary serial port through BIOS PUNCH, READER, AUXIST and
 * AUXDEV. Console 1 is temporarily unbound from that port so BDOS console
 * polling cannot consume protocol bytes; bye() restores the prior binding.
 * BIOS TICK supplies the 100 Hz timeout clock.
 *
 * CP/M records no byte length within its final 128-byte record. Binary sends
 * therefore include the complete final record; text sends stop at ^Z.
 * Received partial records are padded with ^Z.
 */
#include "cpm.h"
#include "cdefs.h"
#include "debug.h"

/*
 * The two file buffers, which in the E-Kermit distribution come from a
 * per-platform `platform.h'.  One CP/M record in, four out: the input
 * side is refilled by one BDOS read (readfile() below), and the output
 * side is the engine's decode buffer, which is drained into whole
 * records by writefile().
 */
#define	IBUFLEN		128
#define	OBUFLEN		512

#include "kermit.h"

#define	BIOS_TICK	24		/* bios900.c case 24  */
#define	BIOS_CONDEV	28		/*   ... case 28      */
#define	BIOS_PUNCH	6		/*   ... case 6, via the raw gate */
#define	BIOS_READER	7		/*   ... case 7,  "   "   "   "   */
#define	BIOS_AUXIST	31		/*   ... case 31      */
#define	BIOS_AUXDEV	32		/*   ... case 32      */

#define	CD_NONE		0		/* nothing bound to this console */
#define	CD_QUERY	(-1)		/* AUXDEV: ask, do not bind	 */

#define	HZ		100		/* tick900.c: the tick's rate	*/

#define	RXTMO		(2 * HZ)	/* how long readpkt waits for a byte
					   before giving the engine a 0 to
					   retry on -- until the far end has
					   said how long it wants	*/
#define	MAXIDLE		90		/* seconds of complete silence, before
					   any packet has arrived, after which
					   we give up			*/

UCHAR o_buf[OBUFLEN + 8];		/* file output buffer (engine's) */
UCHAR i_buf[IBUFLEN + 8];		/* file input buffer  (engine's) */

static struct k_data k;
static struct k_response r;

static int  blk[5];			/* fn 50 block: {code,P1,P1,P2,P2} */
static char dma[SECLEN];		/* our own record buffer	   */

static struct fcb xfcb;			/* the file being sent or received */
static int  xopen;			/* nonzero: xfcb is open	   */
static int  xtext;			/* nonzero: text mode (^Z ends it) */
static int  xeof;			/* input file has run out	   */
static long xsize;			/* size in bytes, rounded to 128   */

static int  con1was = -1;		/* what console 1 was bound to	   */

/*
 * The BIOS through BDOS function 50.  bioscl() (sys/iosys.c) answers
 * 0xffffffff for a code it refuses, and 24/28/31/32 are all on its
 * allowed list; 6 and 7 are NOT and never will be (it refuses 2-7
 * outright, and that refusal predates this program), so those two go
 * through the raw SC #3 gate __bios() the way every stock DRI BIOS trap
 * does.  Two gates, named apart, so that which one a call uses is
 * visible at the call site.
 *
 * __bios()'s FIRST argument is a WORD and its other two are LONGs
 * (src/cmd/biossc.s: `ld r3, rr14(8)' then `ldl rr4, rr14(10)').  A
 * `(long)' cast on the function code pushes two extra bytes and slides
 * both parameters, which is a BIOS call to a function nobody named with
 * arguments nobody wrote -- it cost this port an afternoon.
 */
static long biosfn(code, p1, p2)
int code, p1, p2;
{
	blk[0] = code;
	blk[1] = p1 < 0 ? -1 : 0;	/* P1 as a sign-extended LONG */
	blk[2] = p1;
	blk[3] = p2 < 0 ? -1 : 0;
	blk[4] = p2;
	return (__bdosl(BDOS_BIOSCALL, (long) blk));
}

/*
 * AUX in and out, through the raw gate.  auxrd() answers 0..255 or -1
 * for "nothing waiting" -- BIOS 7 does not block and returns 0x1A both
 * for the reader's EOF and for an empty line, so AUXIST is asked first
 * and is the only thing that distinguishes them.
 */
static int auxrd()
{
	if (biosfn(BIOS_AUXIST, 0, 0) != 1L)
		return (-1);
	return ((int)(__bios(BIOS_READER, 0L, 0L) & 0xff));
}

static VOID auxwr(c)
int c;
{
	__bios(BIOS_PUNCH, (long)(c & 0xff), 0L);
}

static long ticks()
{
	return (biosfn(BIOS_TICK, 0, 0));
}

/************************************************************************/
/*	The eleven functions the engine calls out through		*/
/************************************************************************/

/*
 * inchk() -- how many bytes are waiting.  AUXIST answers one bit, not a
 * count, so this answers 1 or 0.  The engine uses it only as a
 * predicate (kermit.c consults k->ixd to decide whether it may keep
 * running), so a count would buy nothing and the ring's depth is not
 * something the BIOS publishes.
 */
int inchk(kp)
struct k_data *kp;
{
	return (biosfn(BIOS_AUXIST, 0, 0) == 1L ? 1 : 0);
}

/* Use the peer's negotiated timeout when it is sane. Before negotiation, or
   for an invalid value, use the two-second default. */
static long rxtmo(kp)
struct k_data *kp;
{
	register int t;

	t = kp->r_timo;
	if (t < 1 || t > 60)
		return ((long)RXTMO);
	return ((long)t * (long)HZ);
}

/*
 * readpkt() -- read one Kermit packet into p.
 *
 * Returns the number of bytes between the start-of-packet and the
 * terminator, 0 on a timeout (which the engine turns into a NAK and a
 * retry -- this is the whole reason the AUX read is non-blocking), and
 * -1 never: there is no "connection lost" on a raw serial line, and
 * reporting one would turn a recoverable silence into a fatal error.
 *
 * The clock is restarted at every byte that arrives, so the timeout is
 * "nothing at all for that long", not "the packet took too long".  A
 * slow line is not an error; a dead one is.
 */
int readpkt(kp, p, len)
struct k_data *kp;
UCHAR *p;
int len;
{
	register int	x;
	register int	n;
	int		flag;
	long		dl;

	if (p == (UCHAR *)0)
		return (-1);
	n = 0;
	flag = 0;
	dl = ticks() + rxtmo(kp);
	for (;;) {
		x = auxrd();
		if (x < 0) {
			if (ticks() - dl >= 0L)
				return (0);	/* nothing for that long */
			continue;
		}
		dl = ticks() + rxtmo(kp);
		if (kp->parity)
			x &= 0x7f;
		if (!flag && x != kp->r_soh)
			continue;		/* not in a packet yet */
		if (x == kp->r_soh) {
			flag = 1;		/* (re)start on every SOH */
			n = 0;
			continue;
		}
		if (x == kp->r_eom || x == '\012') {
			return (n);
		}
		if (n >= kp->r_maxlen)		/* longer than negotiated */
			return (0);
		p[n++] = (UCHAR)x;
	}
}

/*
 * tx_data() -- write n bytes to the line.  auxout() in the BIOS bounds
 * its own wait for the transmitter, so this cannot hang; a byte lost to
 * a dead line becomes a checksum failure at the far end, which is what
 * the protocol is for.
 */
int tx_data(kp, p, n)
struct k_data *kp;
UCHAR *p;
int n;
{
	while (n-- > 0)
		auxwr((int)*p++);
	return (X_OK);
}

/*
 * openfile() -- mode 1 read, 2 create, 3 append.  Append is refused:
 * CP/M sequential writes always append to the end of an open file, but
 * "resume a partial transfer at byte N" needs a byte count the
 * directory does not hold (see the banner).  Refusing it makes the far
 * end restart the file, which is correct; pretending would truncate it.
 */
int openfile(kp, s, mode)
struct k_data *kp;
UCHAR *s;
int mode;
{
	mkfcb((char *)s, &xfcb);
	xeof = 0;
	switch (mode) {
	case 1:					/* read */
		if (__bdos(BDOS_OPEN, (long)&xfcb) == 0xff)
			return (X_ERROR);
		kp->s_first = 1;
		kp->zinptr = i_buf;
		kp->zincnt = 0;
		xopen = 1;
		return (X_OK);
	case 2:					/* create */
		__bdos(BDOS_DELETE, (long)&xfcb);	/* may not exist */
		if (__bdos(BDOS_MAKE, (long)&xfcb) == 0xff)
			return (X_ERROR);
		xopen = 1;
		return (X_OK);
	}
	return (X_ERROR);			/* 3 (append): see above */
}

/*
 * fileinfo() -- the size and type of the file about to be sent.
 *
 * The size is the file's record count times 128, which is the only size
 * CP/M has.  The date is left empty: the far end treats a zero-length
 * date string as "not supplied", and a stamp read out of an XFCB would
 * be this port's date and not the file's in every case where the file
 * was written by something that does not stamp.
 *
 * The type is not scanned for (F_SCAN is off, as it is in the ELKS
 * build): a scan would have to read the whole file to answer, and on
 * this machine that is a second pass over the disk before the first
 * packet.  The mode the user asked for is the mode used.
 */
ULONG fileinfo(kp, filename, buf, buflen, type, mode)
struct k_data *kp;
UCHAR *filename;
UCHAR *buf;
int buflen;
short *type;
short mode;
{
	register long	recs;

	if (buf != (UCHAR *)0 && buflen > 0)
		buf[0] = '\0';			/* no date */
	*type = xtext ? 0 : 1;
	mkfcb((char *)filename, &xfcb);
	if (__bdos(BDOS_OPEN, (long)&xfcb) == 0xff)
		return ((ULONG)X_ERROR);
	recs = 0;
	setdma(dma);
	while (__bdos(BDOS_READSEQ, (long)&xfcb) == 0)
		recs++;
	__bdos(BDOS_CLOSE, (long)&xfcb);
	xsize = recs * (long)SECLEN;
	return ((ULONG)xsize);
}

/*
 * readfile() -- ONE BYTE of the file being sent, 0..255, or -1 at end
 * of file.
 *
 * That signature is the engine's, and it is easy to get wrong: the
 * zgetc() macro (kermit.c:35) calls this function only when its own
 * counter has run out, and USES THE RETURN VALUE AS THE CHARACTER.  A
 * readfile() that refills the buffer and returns X_OK feeds a NUL into
 * the file at every refill -- eight of them in a 1 KB file, which is
 * exactly how this was found.  So: refill if empty, then hand back the
 * first byte and step past it, as unixio.c and elksio.c both do.
 *
 * IBUFLEN is one CP/M record, so a refill is one BDOS read and one
 * copy.  In text mode a ^Z ends the file where it stands: that is the
 * only end-of-file mark a CP/M text file has, and stopping at it is why
 * a text transfer of a text file arrives byte-exact instead of padded.
 */
int readfile(kp)
struct k_data *kp;
{
	register int	i;

	if (kp->zinptr == (UCHAR *)0)
		return (X_ERROR);
	if (kp->zincnt < 1) {			/* the buffer has run out */
		if (!xopen || xeof)
			return (-1);
		setdma(dma);
		if (__bdos(BDOS_READSEQ, (long)&xfcb) != 0) {
			xeof = 1;
			return (-1);		/* end of file */
		}
		for (i = 0; i < SECLEN; i++) {
			if (xtext && dma[i] == 0x1a) {
				xeof = 1;
				break;
			}
			i_buf[i] = (UCHAR)dma[i];
		}
		if (i == 0)
			return (-1);		/* a record of nothing but ^Z */
		kp->zincnt = i;
		kp->zinptr = i_buf;
	}
	(kp->zincnt)--;
	return (*(kp->zinptr)++ & 0xff);
}

/*
 * writefile() -- n bytes into the file being received.
 *
 * The engine hands over whatever it has decoded, which is not a
 * multiple of 128, so the remainder is carried in `part' until a whole
 * record can be written.  closefile() pads the last one.
 */
static char part[SECLEN];
static int  partn;

int writefile(kp, s, n)
struct k_data *kp;
UCHAR *s;
int n;
{
	register int	i;

	if (!xopen)
		return (X_ERROR);
	for (i = 0; i < n; i++) {
		part[partn++] = (char)s[i];
		if (partn == SECLEN) {
			setdma(part);
			if (__bdos(BDOS_WRITESEQ, (long)&xfcb) != 0)
				return (X_ERROR);
			partn = 0;
		}
	}
	return (X_OK);
}

/*
 * closefile() -- mode 1 closes the input file, 2 and 3 the output one.
 * c is the packet type that got us here: a 'D' means the transfer was
 * discarded, and a discarded output file is deleted rather than left as
 * a truncated one with the right name.
 */
int closefile(kp, c, mode)
struct k_data *kp;
UCHAR c;
int mode;
{
	register int	i;

	if (!xopen)
		return (X_OK);
	if (mode == 1) {			/* input */
		__bdos(BDOS_CLOSE, (long)&xfcb);
		xopen = 0;
		return (X_OK);
	}
	if (partn > 0) {			/* pad the last record */
		for (i = partn; i < SECLEN; i++)
			part[i] = 0x1a;
		setdma(part);
		__bdos(BDOS_WRITESEQ, (long)&xfcb);
		partn = 0;
	}
	__bdos(BDOS_CLOSE, (long)&xfcb);
	xopen = 0;
	if (c == 'D')
		__bdos(BDOS_DELETE, (long)&xfcb);
	return (X_OK);
}

/************************************************************************/
/*	The control program						*/
/************************************************************************/

static VOID say(s)
char *s;
{
	cputs(s);
	cputs("\r\n");
}

/*
 * bye() -- the ONE exit.  Console 1 goes back to the device it was on
 * before we took the wire; every return path comes through here, so
 * there is no way out of this program that leaves the spare port
 * unbound.
 */
static VOID bye(msg, rc)
char *msg;
int rc;
{
	if (con1was >= 0)
		biosfn(BIOS_CONDEV, 1, con1was);
	/*  Put the DMA address back where the base page had it.  Every
	 *  file call above moved it into one of this program's buffers,
	 *  and the DMA address is a SYSTEM variable, not a per-program
	 *  one: what reads a record next is the warm boot's own load of
	 *  the CCP, and it would read it into a TPA that is about to be
	 *  overwritten.  */
	setdma((char *)_base->buff);
	if (msg != (char *)0)
		say(msg);
	__bdos(BDOS_WBOOT, 0L);
}

/*
 * The command tail arrives already split: cstart.c (src/cmd/cstart.c)
 * uppercased by the CCP and word-split by the runtime, argv[0] empty.
 */
main(argc, argv)
int argc;
char **argv;
{
	register int	status;
	register int	rxlen;
	int		started;
	long		idle;
	char		*p;
	char		*fname;
	int		action;
	short		slot;
	UCHAR		*inbuf;
	UCHAR		*flist[2];

	action = 0;
	fname = (char *)0;
	xtext = 0;
	if (argc > 1) {
		p = argv[1];
		if (*p == 'R' || *p == 'S') {
			action = *p++;
			if (*p == 'T')
				xtext = 1;
		}
		if (argc > 2)
			fname = argv[2];
	}
	if (action == 0 || (action == 'S' && fname == (char *)0)) {
		say("KERMIT: E-Kermit 1.8 for CP/M-8000");
		say("  KERMIT R          receive files");
		say("  KERMIT S FILE.EXT send one file");
		say("  KERMIT RT | ST    the same, in text mode");
		bye((char *)0, 1);
	}

	/*  Take the wire.  If console 1 is not there at all -- a machine
	 *  whose loader found one serial channel -- function 28 answers
	 *  -1 and there is no spare port to transfer over, which is a
	 *  refusal and not a silence.  */
	if (biosfn(BIOS_AUXDEV, CD_QUERY, 0) <= 0L)
		bye("KERMIT: no AUX line on this machine", 1);
	con1was = (int)biosfn(BIOS_CONDEV, 1, CD_NONE);
	if (con1was < 0) {
		con1was = -1;
		bye("KERMIT: console 1 is not this machine's to take", 1);
	}

	k.xfermode = 1;			/* manual: the user said which	*/
	k.remote = 1;			/* we are the remote end	*/
	k.binary = xtext ? 0 : 1;
	k.parity = 0;			/* 8 bits, no parity (sccinit)	*/
	k.bct = 3;			/* 16-bit CRC			*/
	k.bctf = 0;
	k.ikeep = 0;
	k.cancel = 0;
	k.filelist = (UCHAR **)0;

	k.zinbuf = i_buf;
	k.zinlen = IBUFLEN;
	k.zincnt = 0;
	k.obuf = o_buf;
	k.obuflen = OBUFLEN;
	k.obufpos = 0;

	k.rxd = readpkt;
	k.txd = tx_data;
	k.ixd = inchk;
	k.openf = openfile;
	k.finfo = fileinfo;
	k.readf = readfile;
	k.writef = writefile;
	k.closef = closefile;
	k.dbf = 0;

	if (action == 'S') {
		flist[0] = (UCHAR *)fname;
		flist[1] = (UCHAR *)0;
		k.filelist = flist;
	}

	status = kermit(K_INIT, &k, 0, 0, "", &r);
	if (status == X_ERROR)
		bye("KERMIT: init failed", 1);
	idle = 0;
	started = 0;
	if (action == 'S') {
		status = kermit(K_SEND, &k, 0, 0, "", &r);
		started = 1;		/* a sender retransmits from the first
					   silence: it is the one waiting */
	}

	/*
	 * A TIMEOUT IS NOT ALWAYS AN EVENT THE ENGINE CAN BE TOLD ABOUT,
	 * and this is the one place this control program has to differ
	 * from E-Kermit's own demo main().
	 *
	 * That demo's readpkt() blocks forever -- its comment says so, and
	 * says why: "only one Kermit needs to time out", and it assumes the
	 * one that does is the other one.  Ours cannot block, because a
	 * blocking read on this machine is a machine that has stopped.  So
	 * ours can hand the engine a zero-length packet, and the engine's
	 * answer to one depends on whether it has anything to retransmit:
	 * a SENDER resends its last packet, which is exactly right, but a
	 * RECEIVER that has not yet seen the far end's S packet has no
	 * `what' set, falls into the sender's arm, finds nothing to resend
	 * and returns X_ERROR.  A receiver waiting at the prompt for the
	 * host to start would therefore die of the silence it exists to
	 * wait in.
	 *
	 * So silence is not shown to the engine until there is something
	 * for the engine to do about it -- which for a sender is from the
	 * first packet, and for a receiver is once one packet has arrived.
	 * `idle' bounds the wait in ticks and not in tries, so that an
	 * unattended machine still comes back to the prompt after the same
	 * MAXIDLE seconds whatever timeout the far end asked for.
	 */
	while (status != X_DONE) {
		inbuf = getrslot(&k, &slot);
		rxlen = k.rxd(&k, inbuf, P_PKTLEN);
		if (rxlen < 1) {
			freerslot(&k, slot);
			if (rxlen < 0)
				bye("KERMIT: line failed", 1);
			if (!started) {
				idle += rxtmo(&k);
				if (idle > (long)MAXIDLE * (long)HZ)
					bye("KERMIT: nothing on the line", 1);
				continue;
			}
		} else {
			started = 1;
			idle = 0;
		}
		status = kermit(K_RUN, &k, slot, rxlen, "", &r);
		if (status == X_ERROR) {
			/*  The engine's state and the length of the packet
			 *  that ended it: without these two numbers a
			 *  failure on a serial line is a sentence, and with
			 *  them it says whether the line was silent (len 0)
			 *  or the protocol went somewhere it could not
			 *  come back from.  */
			cputs("KERMIT: failed, state ");
			putdec((unsigned)r.status);
			cputs(" len ");
			putdec((unsigned)(rxlen < 0 ? 0 : rxlen));
			bye("", 1);
		}
	}
	bye("KERMIT: done", 0);
}
