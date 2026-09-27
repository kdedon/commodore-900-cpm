/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/* BIOS dispatch, ROM console access, and WD disk deblocking.
 * DPH/DPB layouts are shared with BDOS through bdosdef.h. */
#include "romabi.h"
#include "boottrace.h"
#include "c900cfg.h"		/* TPASEG/TPABASE			*/
#include "stdio.h"		/* DRI types layer (sys/stdio.h)	*/
#include "bdosdef.h"		/* BDOS structs: dpb, dph (sys/)	*/
#include "bcb.h"		/* LRU sector cache (sys/bcb.c)		*/
#include "bios900.h"

/************************************************************************/
/*	Console								*/
/************************************************************************/

/* Console 0 uses the ROM-selected serial/video device through crsr.c.
 * Other consoles bind to raw serial channels in condev[]. The loader's
 * bi_serial bitmap supplies channels, with on-board channels as fallback.
 * pend[] is per-console lookahead (0 means empty, so NUL is dropped);
 * receive rings belong to channels and survive console rebinding. */

#define SCC_RR0		0x0101		/* SCC channel B: Rx/Tx status */
#define SCC_RR8		0x0111		/* SCC channel B: data */
#define RXAVAIL		0x01		/* RR0 D0: Rx character available */
#define TXEMPTY		0x04		/* RR0 D2: Tx buffer empty */

/* WR2 (vector) and WR9 (master interrupt control) are shared per SCC.
 * WR1 is per channel: the ROM console keeps WR1=0 and remains polled
 * while spare channels enable receive interrupts. Do not arm channel 0:
 * ROM console routines also access it. */

extern int inb();
extern outb();

/*
 * The video console's keyboard is OURS, not the ROM's.  The ROM path it
 * replaces never worked on any machine: the ROM does not set the
 * keyboard up at reset, it programs CIO #1 port A lazily inside its own
 * blocking getchar(), which a BIOS must never call, so kbd_poll() was
 * polling a port nobody had ever initialised.
 */
#include "kbd900.h"

int convid;	/* nonzero: video console, input = local keyboard */
/*
 * CD_ROM on a console OTHER than 0, on a video machine, is SCC channel B
 * as a raw line.  CD_SER(0) and CD_ROM are the same number, and on a
 * serial machine that is literally one wire; on a video machine console 0
 * is the screen and keyboard, and channel 0 is a free terminal that
 * coninit() binds as console 1.  It stays polled: rxarm() and sccrxdrn()
 * never take channel 0.
 */
#define CONRAW0(con)	((con) != 0 && convid)
int pend[CONMAX];	/* CONST lookahead, 0 = none, per console */
char condev[CONMAX];	/* console -> device; see the banner	*/
unsigned int conmap;	/* the serial map the table was built from */
int ncon = 1;		/* consoles 0..ncon-1.  coninit() sets it; 1 until
			   then, so that a console call made before cold
			   start is over reaches console 0 and not an
			   unbuilt table entry. */

/* Channel index follows the loader's bi_serial bit order and ascending
 * I/O address: 0x0100 is the ROM line, 0x0120 the spare motherboard port.
 * Keep this table synchronized with kboot's bootinfo interface.
 * sccport() returns zero for channels without a known address. */
#define NSCCBASE	6
static unsigned int sccbase[NSCCBASE] = {
	0x0100,		/* motherboard SCC U74 -- the ROM's console line	*/
	0x0120,		/*   and its spare port (COHERENT's /dev/tty51)	*/
	0x0300, 0x0320,	/* LR board SCC U31, connectors CN3 and CN4	*/
	0x0380, 0x03a0	/* LR board SCC U36, connectors CN5 and CN6	*/
};

/*
 * The port address of register `reg' of serial channel `chan', or 0 if
 * this BIOS has no address for that channel.  A Z8030 register lives at
 * base | (register << 1) | 1 -- the register number is AD4:AD1 and the
 * odd address is the byte lane the console's own constants use.
 */
sccport(chan, reg)
int chan, reg;
{
	if (chan < 0 || chan >= NSCCBASE)
		return (0);
	return ((int)(sccbase[chan] | (unsigned int)(reg << 1) | 1));
}

/************************************************************************/
/*	The receive rings (C8)						*/
/************************************************************************/

/* Rings belong to hardware channels, so rebinding a console does not move
 * pending device input. The ISR alone advances rxhead; consumers advance
 * rxtail. Word-sized indices are atomic on the target.
 * A full ring still drains the receiver to clear its interrupt level,
 * counting discarded bytes in rxlost. */
#define RXRING		64		/* characters buffered per channel */
#define RXRMASK		(RXRING - 1)	/* RXRING must be a power of two	  */

static char rxbuf[NSCCBASE][RXRING];
static int rxhead[NSCCBASE];		/* the interrupt's end		*/
static int rxtail[NSCCBASE];		/* the program's end		*/
static char rxon[NSCCBASE];		/* 1: this channel is interrupt-fed */
long rxlost;				/* characters no ring had room for */

/* Drain every armed channel on an SCC receive interrupt. The stub saves
 * r0-r13; channel 0 stays polled. Reading RR8 clears receive availability. */
sccrxdrn()
{
	register int chan;
	register int p;
	register int d;

	for (chan = 1; chan < NSCCBASE; chan++) {
		if (rxon[chan] == 0)
			continue;
		p = sccport(chan, 0);		/* RR0: status	*/
		d = sccport(chan, 8);		/* RR8: data	*/
		while (inb(p) & RXAVAIL) {
			register int h;

			h = (rxhead[chan] + 1) & RXRMASK;
			if (h == rxtail[chan]) {
				inb(d);		/* full: read it away, or
						   the level never clears */
				rxlost++;
				continue;
			}
			rxbuf[chan][rxhead[chan]] = (char)inb(d);
			rxhead[chan] = h;
		}
	}
}

/* Arm receive interrupts with vectors base|0x04 (B) or base|0x0c (A),
 * matching crt.s. WR9=0x09 enables MIE and low-position vector status
 * without resetting either channel. Returns 1 if armed, otherwise 0. */
rxarm(chan)
int chan;
{
	if (chan <= 0 || chan >= NSCCBASE || sccport(chan, 0) == 0)
		return (0);
	rxhead[chan] = rxtail[chan] = 0;
	/*
	 * rxon BEFORE the enable, and that order is the whole of it.  The
	 * other way round -- WR1/WR9 first -- a byte already sitting in the
	 * receiver raises the interrupt between the two writes, sccrxdrn()
	 * finds rxon[chan] still zero, skips the channel, drains nothing and
	 * returns to a level that is still asserted.  Setting the flag first
	 * costs nothing: sccrxdrn() only polls RR0 on a channel whose
	 * receiver is not yet enabled, reads nothing, and the ring it would
	 * fill has just been emptied above.
	 */
	rxon[chan] = 1;
	outb(sccport(chan, 2), ((chan >> 1) + 1) << 4);	/* WR2 vector base  */
	outb(sccport(chan, 1), 0x18);	/* WR1: interrupt on all Rx chars   */
	outb(sccport(chan, 9), 0x09);	/* WR9: MIE, VIS, status low	    */
	return (1);
}

/*
 * rxpoll(chan) -- put a channel back on the polled path.  WR1 = 0 is the
 * per-channel disable; WR9's master enable is left alone because it is
 * shared and another channel may still want it.  Whatever is already in
 * the ring stays there and is still read out first (conpoll()), so
 * turning the interrupt off cannot itself lose a character.
 */
static rxpoll(chan)
int chan;
{
	if (chan <= 0 || chan >= NSCCBASE || sccport(chan, 0) == 0)
		return (0);
	outb(sccport(chan, 1), 0x00);	/* WR1: every source off	*/
	rxon[chan] = 0;
	return (1);
}

/*
 * conok(n) -- clamp a console number.  The BDOS always passes one out of
 * a process descriptor, but BIOS functions 2/3/4 are reachable from a
 * transient through BDOS function 50 (iosys.c bioscl), so the number is
 * not this file's to trust.  An unknown console is console 0: a program
 * that asks for a console that is not there talks to the one that always
 * is, rather than indexing off the end of `pend'.
 */
static conok(n)
int n;
{
	return (n >= 0 && n < ncon ? n : 0);
}

/*
 * conattach(n, dev) -- bind console n to device dev, returning the
 * device it was bound to, or -1 if either argument is not one.  BIOS
 * function 28.  Console 0 cannot be unbound: it is the machine's own
 * console and the last place an error message can go.
 */
static conattach(n, dev)
int n, dev;
{
	register int was;

	if (n <= 0 || n >= ncon || dev < CD_NONE)
		return (-1);
	/* A device that is not CD_NONE is a serial channel, and the only
	 * channels this may name are the ones the machine WAS FOUND to have:
	 * conmap is the map coninit() built the table from.  Without this a
	 * program could bind a console to a channel that is not fitted, and
	 * this driver would then poll and store at a real I/O address with
	 * nothing behind it.  CD_ROM (channel 0) is always allowed: it is
	 * console 0's device and the last place an error message can go. */
	if (dev != CD_NONE && dev != CD_ROM
	    && (conmap & (1 << (dev - 1))) == 0)
		return (-1);
	was = condev[n];
	condev[n] = (char)dev;
	pend[n] = 0;			/* a character polled off the old
					   device is not the new one's */
	return (was);
}

/* BIOS 30: mode 1 arms reception, 0 selects polling, other values query.
 * Returns 1 for interrupt reception, 0 for polling, -1 for no serial port. */
static conrx(n, mode)
int n, mode;
{
	register int chan;

	if (n < 0 || n >= ncon)
		return (-1);
	if (condev[n] == CD_NONE || condev[n] == CD_ROM)
		return (-1);
	chan = condev[n] - 1;
	if (sccport(chan, 0) == 0)
		return (-1);
	if (mode == 0)
		rxpoll(chan);
	else if (mode == 1)
		rxarm(chan);
	return (rxon[chan] ? 1 : 0);
}

/************************************************************************/
/*	AUX -- the spare line as a device, not a terminal		*/
/************************************************************************/

/* AUX uses a raw, eight-bit serial channel and shares its receive ring.
 * A transfer caller must unbind any console on that channel with BIOS 28
 * to prevent console polling from consuming transfer bytes.
 * READER is nonblocking: 0x1a means no byte or a literal EOF character;
 * BIOS 31 reports availability so callers can implement their own timeout. */
int auxchan = -1;	/* channel BIOS 6/7 use; -1 = no AUX line */

/* Bind AUX using CD_SER numbering; negative values query, CD_NONE detaches.
 * CD_ROM is refused to preserve the machine's console. Returns the old
 * device or -1 for an invalid device. */
static auxattach(dev)
int dev;
{
	register int was;

	was = (auxchan < 0) ? CD_NONE : CD_SER(auxchan);
	if (dev < CD_NONE)
		return (was);		/* a query, not a change */
	if (dev == CD_NONE) {
		auxchan = -1;
		return (was);
	}
	if (dev == CD_ROM)
		return (-1);
	/* the same test conattach() makes, and for the same reason: only a
	 * channel the loader FOUND may be polled and stored at */
	if ((conmap & (1 << (dev - 1))) == 0 || sccport(dev - 1, 0) == 0)
		return (-1);
	auxchan = dev - 1;
	return (was);
}

/*
 * auxist() -- is a byte waiting on the AUX line?  1 yes, 0 no, -1 there
 * is no AUX line.  BIOS function 31.  It costs one word compare on an
 * armed channel and one IN on a polled one, so a protocol may call it in
 * a tight loop without paying for the wire.
 */
static auxist()
{
	if (auxchan < 0)
		return (-1);
	if (rxhead[auxchan] != rxtail[auxchan])
		return (1);		/* the ring first -- see conpoll() */
	if (rxon[auxchan])
		return (0);
	return ((inb(sccport(auxchan, 0)) & RXAVAIL) != 0);
}

/*
 * auxin() -- the next AUX byte, 0..255, or -1 if none is waiting.  The
 * ring is read before the rxon[] test for the reason conpoll() gives:
 * characters left in it by a channel since put back on the polled path
 * arrived first and are still first.
 */
static auxin()
{
	register int c;

	if (auxchan < 0)
		return (-1);
	if (rxhead[auxchan] != rxtail[auxchan]) {
		c = rxbuf[auxchan][rxtail[auxchan]] & 0xff;
		rxtail[auxchan] = (rxtail[auxchan] + 1) & RXRMASK;
		return (c);
	}
	if (rxon[auxchan])
		return (-1);
	if ((inb(sccport(auxchan, 0)) & RXAVAIL) == 0)
		return (-1);
	return (inb(sccport(auxchan, 8)) & 0xff);
}

/*
 * auxout(c) -- one byte out of the AUX line.  The Tx-empty wait is
 * bounded by the same count conout() uses and for the same reason: a
 * line whose far end has gone must cost the program a delay, not the
 * machine.  A byte dropped that way is the protocol's to notice, and
 * every protocol worth running over a serial line does.
 */
static auxout(c)
int c;
{
	register int i;
	register int p;

	if (auxchan < 0)
		return;
	p = sccport(auxchan, 0);
	for (i = 0; i < 20000; i++)
		if (inb(p) & TXEMPTY)
			break;
	outb(sccport(auxchan, 8), c & 0xff);
}

/* Only IOBYTE bits 7:6 (LST) affect routing: 0/1 use the ROM console,
 * 2/3 discard output. Other fields are stored but do not change devices. */
char iobyte;

/* LIST routes to the console iff the LST: field selects TTY: or CRT:. */
#define LSTCON()	((iobyte & 0x80) == 0)

/*
 * One non-blocking poll of console `con'; ASCII or 0.
 */
static conpoll(con)
int con;
{
	register int c;

	if (condev[con] == CD_NONE)
		return (0);
	if (condev[con] != CD_ROM || CONRAW0(con)) {
		register int p;
		register int chan;

		chan = condev[con] - 1;
		p = sccport(chan, 0);
		if (p == 0)
			return (0);	/* fitted, but not addressable here;
					   also the bound on `chan' below   */
		/*  The ring first, and BEFORE the rxon[] test: a channel
		 *  put back on the polled path (rxpoll()) may still have
		 *  characters in it, and they were received first.  */
		if (rxhead[chan] != rxtail[chan]) {
			c = rxbuf[chan][rxtail[chan]] & 0x7f;
			rxtail[chan] = (rxtail[chan] + 1) & RXRMASK;
			return (c);
		}
		if (rxon[chan])
			return (0);	/* armed: the ring is the only source,
					   and RR0 costs an IN per CONST	*/
		if ((inb(p) & RXAVAIL) == 0)
			return (0);
		return (inb(sccport(chan, 8)) & 0x7f);
	}
	if (convid)
		return (kbdpoll());	/* kbd900.h, not the ROM: see above */
	if ((inb(SCC_RR0) & RXAVAIL) == 0)
		return (0);
	return (inb(SCC_RR8) & 0x7f);
}

/* ROM console output uses the shared H19/Z19 parser; spare serial ports
 * send bytes unchanged. LIST bypasses the parser. Tx polling is bounded. */
static conout(c, con)
int c, con;
{
	register int i;

	register int p;

	if (condev[con] == CD_NONE)
		return;
	if (condev[con] == CD_ROM && !CONRAW0(con)) {
		crsout(c);
		return;
	}
	p = sccport(condev[con] - 1, 0);
	if (p == 0)
		return;			/* fitted, but not addressable here */
	for (i = 0; i < 20000; i++)
		if (inb(p) & TXEMPTY)
			break;
	outb(sccport(condev[con] - 1, 8), c & 0xff);
}

/* BIOS 27: output n bytes from a full XADDR. crsr.c batches LR screen
 * stores; other devices use character output. */
static conoutn(p, n, con)
long p;
int n, con;
{
	register char *s;

	if (condev[con] == CD_ROM && !CONRAW0(con)) {
		crsrun((char *)p, n);
		return;
	}
	for (s = (char *)p; n > 0; n--)
		conout(*s++, con);
}

/* The scheduler calls this directly to wake console waiters, including
 * from tick dispatch when no BDOS activation is in progress.
 * pend[] preserves the polled character for the next CONIN. */
conststat(con)
int con;
{
	if (pend[con] == 0)
		pend[con] = conpoll(con);
	return (pend[con] != 0 ? 0xff : 0x00);
}

static conin(con)
int con;
{
	register int c;

	while ((c = pend[con]) == 0)
		c = pend[con] = conpoll(con);
	pend[con] = 0;
	return (c);
}

/************************************************************************/
/*	Disk: drives A: and B: on the raw hard disk			*/
/************************************************************************/

/* Deblocking runs over the LRU sector cache (sys/bcb.c), whose buffers sit
 * in the seg-0x33 buffer segment the startup code maps at phys 0x0C0000;
 * the WD controller DMAs straight to their physical addresses. */

extern mem_cpy();
extern ccpentry();		/* glue.s: reset the stack, enter the CCP */
extern long tickget();		/* src/bios/tick900.c, trap.s	*/

/* Drives share a directory buffer but need separate live allocation maps.
 * Carve maps from a bounded pool in drive order; refuse a drive when its
 * map does not fit. A 83776-sector disk needs at most 1316 bytes for
 * disjoint partitions with 4096-byte allocation blocks. */
UBYTE dirbuf[128];
UBYTE alvpool[ALVPOOL];

/* The table itself.  drvthere[] is the answer seldsk gives; drvbase[] is
 * the absolute block dskread/dskwrite add to. */
struct dpb dpbtab[NDRIVE];
struct dph dphtab[NDRIVE];
long drvbase[NDRIVE];
char drvthere[NDRIVE];
long curbase;		/* drvbase[] of the selected drive */

static int settrk, setsec;	/* selected track / sector (0-based)	*/
static long setdma;		/* DMA address (XADDR)			*/
int dskerr;

/*
 * The buffer used last.  Four 128-byte records share one physical sector,
 * so consecutive record transfers keep asking for the same buffer: naming
 * it here answers them with one comparison, and only a change of sector
 * goes to the cache proper.  The named buffer is always the cache's most
 * recently used one, so it cannot be the buffer a later bcbfind() evicts.
 */
static long curblk;
int curix = -1;

static getbuf(blk, fill)
long blk;
int fill;
{
	if (curix >= 0 && curblk == blk)
		return (curix);
	if ((curix = bcbfind(blk, fill)) >= 0)
		curblk = blk;
	return (curix);
}

static flushhst()
{
	if (bcbflush() != 0)
		dskerr = 1;
}

static long dskread()
{
	register long rec;
	register int off, ix;

	dskerr = 0;
	rec = (long)settrk * ASPT + setsec;
	if ((ix = getbuf(curbase + (rec >> 2), 1)) < 0) {
		dskerr = 1;
		return (1L);
	}
	off = ((int)rec & 3) << 7;
	mem_cpy(BCBADR(ix) + (long)off, setdma, (long)SECLEN);
	return (0L);
}

/*
 * Write is read-modify-write into a cache buffer.  mode 1 (directory write)
 * is written through; other writes stay dirty until the buffer is reused or
 * FLUSH (fn 21) is called.  mode 2 is the first record of a freshly
 * allocated block: when it starts a physical sector, none of that sector's
 * records holds file data, so the buffer is zeroed instead of read.
 */
static long dskwrite(mode)
int mode;
{
	register long rec;
	register int off, ix;

	dskerr = 0;
	rec = (long)settrk * ASPT + setsec;
	ix = getbuf(curbase + (rec >> 2),
		    (mode == 2 && ((int)rec & 3) == 0) ? 0 : 1);
	if (ix < 0) {
		dskerr = 1;
		return (1L);
	}
	off = ((int)rec & 3) << 7;
	mem_cpy(setdma, BCBADR(ix) + (long)off, (long)SECLEN);
	bcbmark(ix);
	if (mode == 1 && bcbput(ix) != 0)
		dskerr = 1;
	return (dskerr ? 1L : 0L);
}

/* Return the selected drive's DPH, or zero for an absent drive.
 * BDOS selects before transferring and reselects when the drive changes. */
static long seldsk(dsk)
int dsk;
{
	if (dsk >= 0 && dsk < NDRIVE && drvthere[dsk]) {
		/* Naming the drive's base here is naming it once per change
		 * of drive rather than once per record: the BDOS selects
		 * before it transfers and re-selects whenever the drive
		 * changes (iosys.c do_phio, whose last_dsk cache is exactly
		 * that guarantee), so curbase always belongs to the drive
		 * the next read or write is for. */
		curbase = drvbase[dsk];
		return ((long)&dphtab[dsk]);
	}
	return (0L);			/* that letter is not on this medium */
}

/************************************************************************/
/*	Clock (fn 23)							*/
/************************************************************************/

/*
 * The MSM58321 driver lives in rtc900.c; rtctime() is the whole of BIOS
 * function 23 and returns a LONG, so it must be declared (a K&R implicit
 * declaration would truncate the status to an int).  It moves a 5-byte
 * TOD block.
 */
extern long rtctime();

/*
 * The segment allocator (src/bios/pgalloc.c), reached by BIOS function
 * 25 below and by nothing else in this file.
 */
extern int	pgalloc();
extern int	pgfree();
extern int	pgcount();
extern int	pgnslot;
extern		pgrelall();
extern int	pgtpaswap();
extern int	tpaphys;

/*
 * The dispatcher (src/bdos/proc.c).  The BIOS reaches it at exactly one
 * point, the warm boot below, because that is where a transient
 * program's life ends and therefore where a process's does.
 */
extern int	procdead();

/************************************************************************/
/*	Memory Region Table (fn 18)					*/
/************************************************************************/

/*
 * Layout shared with the readers: bdosboot.c bdosinit ({WORD nmbr;
 * XADDR low; LONG length;}) and go.c/pgmld.c (struct m_rt, NREGIONS = 2)
 * -- count at 0, first region at 2/6, second at 10/14, 18 bytes total.
 * More than one slot is declared so pgmld's m_reg[1] (split-I/D) read
 * stays inside the object; only the first is valid (count = 1).
 *
 * FIVE are declared, not two, because a PROGRAM reads this table as
 * well: function 18 hands out its address and DDT.Z8K copies the whole
 * thing out with mem_cpy -- 42 bytes, count plus FIVE regions, ignoring
 * count -- and then reads slot 4 as the segment it relocates its own 64
 * KB into, leaving the TPA to the debugee.  With two slots that copy ran
 * off the end of this object, took whatever kernel data followed it and
 * relocated the debugger on top of it.
 *
 * SLOT 4 NOW NAMES A SEGMENT OF THE ASKING PROGRAM'S OWN, out of the
 * pool (pgalloc.c).  It used to name the TPA, because the TPA was the
 * only 64 KB region this machine offered, and the consequence was that
 * debugger and debugee landed on each other: DDT copied itself over the
 * program it had just loaded and ran into a trap in the wreckage.  The
 * pool has spare 64 KB segments and a program that wants one may have
 * one, so it gets one.
 *
 * It is still NOT a second region and count is still 1.  Nothing in the
 * loader may put a program there, and nothing does: the readers listed
 * above stop at count, and the pool is not offered as a TPA region
 * (pgalloc.c's banner).  Slot 4 is a channel to one stock binary and
 * nothing else reads it.
 *
 * The segment is per-process and the table is not, so the two cannot be
 * married in the table's initialiser.  It is recorded in the process
 * descriptor (src/bdos/proc.h pd_mrtseg, filled by proc.c pmrtseg()) and
 * copied into slot 4 below at the moment the table is handed out; one
 * allocation per process, released by pmrtrel() when the program ends.
 *
 * And it is filled in only for the callers that read it.  `mrtusr' is
 * set by the SC #3 BIOS gate (src/bdos/bdosglue.s biosgate) when a
 * NON-SEGMENTED Normal-mode program -- a stock DRI binary, the only kind
 * that reads slot 4 -- asks for function 18, and by nothing else.  The
 * kernel's own readers (bdosinit, pgmld on every program load, the CCP)
 * call bios() directly, never see the flag set, and therefore never cost
 * the pool a segment.  That distinction is the whole reason the flag
 * exists rather than the allocation simply happening in case 18.
 *
 * An empty pool answers 0 and slot 4 falls back to TPABASE -- the old
 * behaviour, no worse than it was, and nothing refuses to run.
 *
 * HOW FAR THIS GETS DDT.  With a segment of its own DDT copies
 * itself into it through the SC #1 gate, resumes executing there, and
 * every gate in the translation layer follows it across without being
 * told -- bdosglue.s substitutes the CALLER'S PC SEGMENT, which is the
 * new one from the first instruction executed there, so its file opens,
 * reads and its BDOS program load all land in the right place.  The
 * debugee is loaded into the TPA correctly, base page and all.  DDT then
 * patches an SC #0 over the first word of the debugee's entry point --
 * a breakpoint -- and transfers to it.
 *
 * Its handler for that trap it records just before, as BIOS function 22
 * for vector 32 carried by BDOS function 50 -- the idiom DRI's own BDOS
 * uses (bdosmisc.c) -- and it reads the frame it is handed as DRI's
 * 40-byte one.  Both are provided now: iosys.c bioscl() passes code 22,
 * the vector table below is per process, and trap.s faultcom_ presents
 * a recorded handler with DRI's frame.  DDT.Z8K is Zilog's portable
 * debugger (Version 841128.14) as shipped with CP/M-8000, byte-identical
 * to the Olivetti M20 v1.1 disk copy; the M20 system is the reference.
 */
struct mrt {
	int	count;
	struct mrtreg {
		long	tpalow;		/* XADDR of region base */
		long	tpalen;
	} regions[5];
};

/* One region: the 64 KB TPA at seg TPASEG offset 0.  Slot 4 starts at
 * the TPA -- the fallback -- and is rewritten per asking program; see
 * above. */
static struct mrt memtab = {
	1,
	{ { TPABASE, 0x10000L }, { 0L, 0L }, { 0L, 0L }, { 0L, 0L },
	  { TPABASE, 0x10000L } }
};

/* Set by the SC #3 gate for a non-segmented Normal-mode caller's
 * function 18, consumed and cleared by case 18.  A one-shot rather than
 * a mode the gate leaves standing, so that a kernel bios(18) can never
 * pick up a flag some earlier program left behind. */
int	mrtusr;

extern int pmrtseg();		/* src/bdos/proc.c: this process's one	*/

/************************************************************************/
/*	map_adr (space-code address mapping)				*/
/************************************************************************/

/* System spaces already contain CPU far pointers. Space 0xffff records
 * the loader's user segment (-1 for a segmented image). TPA spaces map
 * 16-bit offsets into that segment, or retain full segmented addresses. */
static int usrseg = TPASEG;	/* segment recorded via space 0xffff;
				 * -1 = segmented load (pgmld passes -1L:
				 * its addresses are already full XADDRs,
				 * so the TPA spaces map as the identity) */

long map_adr(adr, space)
long adr;
UWORD space;
{
	if (space == 0xffff) {
		usrseg = (adr == -1L) ? -1 : (int)(adr >> 24) & 0x7f;
		return (adr);
	}
	if (space == 4 || space == 5 || space == 0x105)
		return (usrseg < 0 ? adr
				   : ((long)usrseg << 24) | (adr & 0xffffL));
	/*  Spaces 2 and 3 name the RESIDENT SYSTEM's data and program, and
	    the offset handed in is one the kernel produced: cpm.sys is
	    compiled non-segmented, so `(long)&object' -- what BIOS
	    function 18 returns for the memory region table, for instance --
	    is a bare 16-bit offset with a zero segment.  Returning that
	    unchanged names segment 0, the boot ROM, and a caller that then
	    mem_cpy'd from it (DDT does, to read the MRT) copied rubbish.
	    The space code says which segment the offset belongs to, so
	    supply it, exactly as the TPA spaces above do.  */
	if ((space & 0xff) == 2 || (space & 0xff) == 3)
		return (((long)((space & 0xff) == 2 ? SYSDSEG : SYSTSEG)
			 << 24) | (adr & 0xffffL));
	/*  Caller spaces (0, 1).  The address arrives with its own segment
	    -- the SC #1 gate has already substituted a non-segmented
	    caller's PC segment for the zero high word nonsegmented C
	    zero-extends into it -- and that segment is the answer.  It
	    arrives as a Z8001 SEGMENT WORD (0xB2..), whose bit 15 is not
	    part of the address; every other far pointer this system hands
	    out is written (seg << 24) with that bit clear (c900cfg.h
	    TPABASE).  Drop it, so that two physical addresses for the same
	    byte compare equal whichever path produced them.  */
	return (adr & 0x7fffffffL);
}

/************************************************************************/
/*	Exception vectors (fn 22) + trap panic printer			*/
/************************************************************************/

/*
 * 48-entry trap-vector table, M20 CP/M-8000 numbering (biosdefs.z8k:
 * NMI 0, EPU 1, SEG 2, PRV 8, SC #n 32+n; C900 adds NVI 6, VI 7).
 * BIOS fn 22 (SETXVEC) records handlers here; the fault path (trap.s
 * faultcom_) calls a recorded handler as a segmented subroutine with
 * DRI's 40-byte register frame on the stack, and panics through panic()
 * below when no handler is recorded.
 *
 * ONE ROW PER PROCESS, indexed by pgcur (pgalloc.c), the BIOS's mirror of
 * the running descriptor.  The only recorders are programs -- nothing in
 * this kernel dispatches through the table except the fault path -- and a
 * program's handler lives in that program's memory, so a single shared
 * table would let DDT on one console catch another console's SC #0 or
 * fault and jump into its own segment with a stranger's frame.  Per row,
 * two debuggers on two consoles each get their own breakpoints.  A row is
 * cleared when its process's program ends (xvclr, from proc.c procdead(),
 * on the path every termination takes) and when a descriptor is handed
 * to a new process (pcreate), so a vector can never outlive the memory it
 * points into.  XVNPROC must be at least proc.h PNPROC and match trap.s;
 * a process with no row records nothing and sees nothing recorded, which
 * is exactly today's behaviour for every program that never asks.
 */
#define XVNPROC	7		/* src/bdos/proc.h PNPROC; trap.s XVNPROC */
long	xvec[XVNPROC][48];
extern int pgcur;		/* the running process (pgalloc.c)	*/

/* Forget every vector process `p' recorded. */
xvclr(p)
int p;
{
	register int i;

	if (p >= 0 && p < XVNPROC)
		for (i = 0; i < 48; i++)
			xvec[p][i] = 0L;
}

extern UWORD xbdos();

static char hexdig[] = "0123456789ABCDEF";

static pxword(v)
int v;
{
	putchar(hexdig[(v >> 12) & 15]);
	putchar(hexdig[(v >> 8) & 15]);
	putchar(hexdig[(v >> 4) & 15]);
	putchar(hexdig[v & 15]);
}

/*
 * Unhandled trap: print the hardware frame.  A fault from a Normal-mode
 * program kills the program with a warm boot (never returns); a fault
 * from System mode returns to trap.s, which halts.
 */
panic(vec, id, fcw, pcseg, pcoff)
int vec, id, fcw, pcseg, pcoff;
{
	puts("\nTRAP vec=");
	pxword(vec);
	puts(" id=");
	pxword(id);
	puts(" fcw=");
	pxword(fcw);
	puts(" pc=");
	pxword(pcseg);
	putchar(':');
	pxword(pcoff);
	putchar('\n');
	if ((fcw & 0x4000) == 0)
		xbdos(0, 0, 0L);	/* program aborted: warm boot */
}

/************************************************************************/
/*	Init + dispatcher						*/
/************************************************************************/

/*
 * One routine per function.  bios() jumps to biostab[d0] with the
 * caller's frame, so each takes (d0, d1, d2) and saves only the
 * registers it uses.
 */
static long bnull()
{
	return (0L);
}

static long binit()				/* INIT */
{
	bootent(0);		/* biosinit() in the boot module */
	return (0L);
}

static long bwboot()				/* WBOOT: back to the CCP */
{
	procdead();			/* a BACKGROUND process ending
					 * here resumes another one and
					 * never comes back (proc.c);
					 * the foreground falls through
					 * to the ordinary warm boot */
	pgrelall();			/* the transient is over: its
					 * segments go back, mapped
					 * System-only again -- except
					 * a page a live process is
					 * parked on */
	flushhst();
	crsreset();			/* a program killed mid-escape
					 * must not leave the parser
					 * eating the CCP's output */
	ccpentry();			/* resets the stack; no return */
	return (0L);
}

/* CONST/CONIN take the console in d1; CONOUT takes it in d2.
 * Zero preserves the standard console-0 interface. */
static long bconst(d0, d1)				/* CONST(console) */
long d1;
{
	return ((long)conststat(conok((int)d1)));
}

static long bconin(d0, d1)				/* CONIN(console) */
long d1;
{
	return ((long)conin(conok((int)d1)));
}

static long bconout(d0, d1, d2)			/* CONOUT(char, console) */
long d1, d2;
{
	conout((int)d1, conok((int)d2));
	return (0L);
}

static long blist(d0, d1)				/* LIST: console or bit-bucket */
long d1;
{
	if (LSTCON())
		putchar((int)d1);
	return (0L);
}

/*
 * PUNCH and READER, on the AUX line.  With no AUX device bound
 * they are a discard and a constant EOF.
 * READER does not block -- see the AUX banner --
 * so 0x1A means either the reader's EOF or "nothing yet", and
 * function 31 is how a caller tells those apart.
 */
static long bpunch(d0, d1)				/* PUNCH(char) */
long d1;
{
	auxout((int)d1);
	return (0L);
}

static long breader()				/* READER */
{
	register int c;

	c = auxin();
	return (c < 0 ? 0x1aL : (long)c);
}

static long bhome()				/* HOME */
{
	settrk = 0;
	return (0L);
}

static long bseldsk(d0, d1)				/* SELDSK */
long d1;
{
	return (seldsk((int)d1));
}

static long bsettrk(d0, d1)				/* SETTRK */
long d1;
{
	settrk = (int)d1;
	return (0L);
}

static long bsetsec(d0, d1)				/* SETSEC */
long d1;
{
	setsec = (int)d1;
	return (0L);
}

static long bsetdma(d0, d1)				/* SETDMA */
long d1;
{
	setdma = d1;
	return (0L);
}

static long bwrite(d0, d1)				/* WRITE */
long d1;
{
	return (dskwrite((int)d1));
}

static long blistst()				/* LISTST: always ready (both
						 * routes accept immediately) */
{
	return (0xffL);
}

static long bsectran(d0, d1)			/* SECTRAN: identity, xlt 0 */
long d1;
{
	return (d1);
}

static long bgmrta()				/* GMRTA */
{
	register int seg;

	if (mrtusr) {
		mrtusr = 0;
		seg = pmrtseg();
		memtab.regions[4].tpalow = seg ?
			((long)seg << 24) : TPABASE;
	}
	return ((long)&memtab);
}

static long bgetiob()				/* GETIOB */
{
	return ((long)iobyte);
}

static long bsetiob(d0, d1)				/* SETIOB */
long d1;
{
	iobyte = (char)d1;
	return (0L);
}

static long bflush()				/* FLUSH */
{
	dskerr = 0;
	flushhst();
	return ((long)dskerr);
}

static long bsetxvec(d0, d1, d2)			/* SETXVEC: record + return old */
long d1, d2;
{
	long oldv;

	if ((int)d1 >= 0 && (int)d1 < 48
	    && pgcur >= 0 && pgcur < XVNPROC) {
		oldv = xvec[pgcur][(int)d1];
		xvec[pgcur][(int)d1] = d2;
		return (oldv);
	}
	return (0L);
}

static long btime(d0, d1, d2)			/* TIME: read/set the RTC */
long d1, d2;
{
	return (rtctime(d1, (int)d2));
}

/* SEGMENT: d1=0 allocates (segment or 0); d1=1 frees d2 (1 or 0);
 * d1=2 counts free slots; d1=3 counts all of them. Other operations
 * return 0. */
static long bsegment(d0, d1, d2)			/* SEGMENT */
long d1, d2;
{
	switch ((int)d1) {
	case 0:
		return ((long)pgalloc());
	case 1:
		return ((long)pgfree((int)d2));
	case 2:
		return ((long)pgcount());
	case 3:
		return ((long)pgnslot);
	}
	return (0L);
}

/* TPASWAP: d1=0 queries the TPA physical page; otherwise exchange it
 * with allocated segment d1 (1 on success, 0 on failure).
 * Resident callers only: code executing in the TPA cannot swap itself. */
static long btpaswap(d0, d1)			/* TPASWAP */
long d1;
{
	if ((int)d1 == 0)
		return ((long)tpaphys);
	return ((long)pgtpaswap((int)d1));
}

/* CONOUTN: d1 is the buffer XADDR; d2 packs the count in bits 15:0
 * and console number in bits 23:16. Reserved for resident callers. */
static long bconoutn(d0, d1, d2)			/* CONOUTN */
long d1, d2;
{
	conoutn(d1, (int)d2, conok((int)(d2 >> 16)));
	return (0L);
}

/* CONDEV binds console d1 to device d2, returning its old device or -1.
 * CD_NONE detaches; serial channel c is device c+1. */
static long bcondev(d0, d1, d2)			/* CONDEV(console, device) */
long d1, d2;
{
	return ((long)conattach((int)d1, (int)d2));
}

/* CONCNT returns the number of configured consoles (1..CONMAX). */
static long bconcnt()				/* CONCNT */
{
	return ((long)ncon);
}

/* CONRX: d2=1 arms, 0 polls, other values query console d1.
 * Returns 1 (interrupt), 0 (polled), or -1 (no serial channel). */
static long bconrx(d0, d1, d2)			/* CONRX(console, mode) */
long d1, d2;
{
	return ((long)conrx((int)d1, (int)d2));
}

/* AUXIST returns 1 for pending input, 0 for none, -1 for no AUX port. */
static long bauxist()				/* AUXIST */
{
	return ((long)auxist());
}

/*
 * AUXDEV: bind the AUX device to a serial channel, in the same
 * CD_SER() numbering function 28 uses; d1 < 0 only asks.  Answers
 * the device it was bound to, or -1 if the argument was not one.
 * 32 is the next free code above AUXIST, and it is on bioscl()'s
 * allowed list for the reason function 28 is: the whole point is
 * that a transient program decides who owns the spare wire.
 */
static long bauxdev(d0, d1)				/* AUXDEV(device) */
long d1;
{
	return ((long)auxattach((int)d1));
}

/*
 * CONSES: the console the cold boot starts its one extra session on,
 * or 0 for none (src/bdos/bdosboot.c pcoldses).  That is the console bound
 * to SCC-B, and only when console 0 is video; a serial operator gets
 * no cold-boot session.  Resident only: not on bioscl()'s list.
 */
static long bconses()				/* CONSES */
{
	if (convid) {
		register int n;

		for (n = 1; n < ncon; n++)
			if (condev[n] == CD_ROM)
				return ((long)n);
	}
	return (0L);
}

/* GSX: one parameter block d2 on surface d1, or on the HR screen
 * when d1 is 0.  -1 when there is no screen to draw on. */
static long bgsx(d0, d1, d2)			/* GSX(surface, pb) */
long d1, d2;
{
	return ((long)crsgsx((char *)d1, (char *)d2));
}

long (*biostab[])() = {
	binit, bwboot, bconst, bconin, bconout, blist, bpunch, breader,
	bhome, bseldsk, bsettrk, bsetsec, bsetdma, dskread, bwrite, blistst,
	bsectran, bnull, bgmrta, bgetiob, bsetiob, bflush, bsetxvec, btime,
	tickget, bsegment, btpaswap, bconoutn, bcondev, bconcnt, bconrx,
	bauxist, bauxdev, bconses, bgsx
};
int nbios = sizeof(biostab) / sizeof(biostab[0]);

/* bios() itself is in glue.s; it lands here for d0 past biostab[]. */
long biosx(d0, d1, d2)
int d0;
long d1, d2;
{
	switch (d0) {
	/*
	 * BIOCOST-only, cpm.h BIOS_ROMCHAR/BIOS_VSETCHAR: not stock CP/M-8000
	 * BIOS functions and not on bioscl()'s (sys/iosys.c) allowed list --
	 * they exist so src/tests/biocost.c, reaching them through the raw
	 * SC #3 gate, can isolate the ROM's own glyph renderer (100) from
	 * the video-RAM store crsr.c uses to erase and to park the cursor
	 * (101) without going through it.  See src/bios/crsr.c vsettest().
	 */
	case 100:				/* ROMCHAR: ROM putchar direct */
		putchar((int)d1);
		break;

	case 101:				/* VSETCHAR: direct video store */
		vsettest((int)d1);
		break;

	/* the HR console on a caller's surface, which no emulated
	 * console selects */
	case 102:				/* HRRAM(surface, string) */
		hrram((char *)d1, (char *)d2);
		break;
	}
	return (0L);
}
