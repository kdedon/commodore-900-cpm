#include "romabi.h"
#include "boottrace.h"
#include "c900cfg.h"		/* TPASEG/TPABASE			*/
#include "stdio.h"		/* DRI types layer (sys/stdio.h)	*/
#include "bdosdef.h"		/* BDOS structs: dpb, dph (sys/)	*/
#include "bcb.h"		/* LRU sector cache (sys/bcb.c)		*/
#include <bootinfo.h>		/* kboot handoff block, from the kboot checkout	*/

/* The version this BIOS asks the loader for, and the block length that goes
 * with it.  It is this system's choice, not part of the handoff layout. */
#define	BI_ASK		4
#if	BI_ASK == 4
#define	BI_ASKLEN	BI_LEN4
#else
#define	BI_ASKLEN	BI_LEN3
#endif

/************************************************************************/
/*	Console								*/
/************************************************************************/


#define SCC_RR0		0x0101		/* SCC channel B: Rx/Tx status */
#define SCC_RR8		0x0111		/* SCC channel B: data */

extern int inb();
extern outb();

static int convid;	/* nonzero: video console, input = local keyboard */
/************************************************************************/
/*	AUX -- the spare line as a device, not a terminal (N2)		*/
/************************************************************************/

static int auxchan = -1;	/* channel BIOS 6/7 use; -1 = no AUX line */

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


static char iobyte;

/* LIST routes to the console iff the LST: field selects TTY: or CRT:. */
#define LSTCON()	((iobyte & 0x80) == 0)

/*
 */
{
	register int c;

	if ((inb(SCC_RR0) & RXAVAIL) == 0)
		return (0);
	return (inb(SCC_RR8) & 0x7f);
}

{
}

{
}

{
	register int c;

	return (c);
}

/************************************************************************/
/*	Disk: drives A: and B: on the raw hard disk			*/
/************************************************************************/

#define ASPT		64		/* 128-byte records per track */
/* SECLEN (128) comes from bdosdef.h */

	BI_ASK,			/* the version asked of the loader		*/
/* Deblocking runs over the LRU sector cache (sys/bcb.c), whose buffers sit
 * in the seg-0x33 buffer segment the startup code maps at phys 0x0C0000;
 * the WD controller DMAs straight to their physical addresses. */

extern mem_cpy();
extern ccpentry();		/* glue.s: reset the stack, enter the CCP */

static UBYTE dirbuf[128];



static int settrk, setsec;	/* selected track / sector (0-based)	*/
static long setdma;		/* DMA address (XADDR)			*/
static int dskerr;

/*
 * The buffer used last.  Four 128-byte records share one physical sector,
 * so consecutive record transfers keep asking for the same buffer: naming
 * it here answers them with one comparison, and only a change of sector
 * goes to the cache proper.  The named buffer is always the cache's most
 * recently used one, so it cannot be the buffer a later bcbfind() evicts.
 */
static long curblk;
static int curix = -1;

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

static long seldsk(dsk)
int dsk;
{
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
extern rtcinit();

/************************************************************************/
/*	Memory Region Table (fn 18)					*/
/************************************************************************/

/*
 * Layout shared with the readers: bdosmisc.c bdosinit ({WORD nmbr;
 * XADDR low; LONG length;}) and go.c/pgmld.c (struct m_rt, NREGIONS = 2)
 * -- count at 0, first region at 2/6, second at 10/14, 18 bytes total.
 * Two slots are declared so pgmld's m_reg[1] (split-I/D) read stays
 * inside the object; only the first is valid (count = 1).
 */
struct mrt {
	int	count;
	struct mrtreg {
		long	tpalow;		/* XADDR of region base */
		long	tpalen;
	} regions[2];
};

/* One region: the 64 KB TPA at seg TPASEG offset 0. */
static struct mrt memtab = {
	1,
	{ { TPABASE, 0x10000L }, { 0L, 0L } }
};

/************************************************************************/
/*	map_adr (space-code address mapping)				*/
/************************************************************************/

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
	return (adr);
}

/************************************************************************/
/*	Exception vectors (fn 22) + trap panic printer			*/
/************************************************************************/

/*
 * 48-entry trap-vector table, M20 CP/M-8000 numbering (biosdefs.z8k:
 * NMI 0, EPU 1, SEG 2, PRV 8, SC #n 32+n; C900 adds NVI 6, VI 7).
 * BIOS fn 22 (SETXVEC) records handlers here; the fault path (trap.s
 * faultcom_) calls a recorded handler as a segmented subroutine with
 * the register frame on the stack, and panics through panic() below
 * when no handler is recorded.
 */
long	xvec[48];

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

	/*  The AUX device (N2) starts on the SAME channel console 1 does --
	 *  the first spare port, 0x0120 on this machine.  That is not two
	 *  owners of one wire by accident: there is only one spare wire, and
	 *  which of the two owns it is the program's choice, made with
	 *  function 28 (`CONDEV(1, CD_NONE)') at the moment a transfer
	 *  starts.  Binding AUX somewhere else, or nowhere, is function 32.  */
	auxchan = -1;
		if (auxchan < 0)
			auxchan = chan;		/* ...and so is the AUX line */
biosinit()
{
	extern int mapseg();

	BTRACE("<1>");		/* binit (BIOS fn 0) entered */
	iobyte = 0;
	/* split-I/D shim banks: pgmld's loadseg copies a 0xEE0B program's
	 * D segments into SPLITDSEG before spload() runs, so both shim
	 * segments must be mapped from boot, not at scan time */
	mapseg(SPLITDSEG, SPLITDPAGE, 2);
	mapseg(SPLITTSEG, SPLITTPAGE, 2);
	rtcinit();			/* CIO #1 Port B/PC1 -> MSM58321 */
	wdinit900();
	bcbinit();
	curix = -1;
	dskerr = 0;
}

long bios(d0, d1, d2)
int d0;
long d1, d2;
{
	long oldv;

	switch (d0) {

	case 0:					/* INIT */
		biosinit();
		break;

	case 1:					/* WBOOT: back to the CCP */
		flushhst();
						 * must not leave the parser
						 * eating the CCP's output */
		ccpentry();			/* resets the stack; no return */
		break;



		break;

	case 5:					/* LIST: console or bit-bucket */
		if (LSTCON())
			putchar((int)d1);
		break;

	/*
	 * PUNCH and READER, on the AUX line (N2).  With no AUX device
	 * bound they are what they were before N2: a discard and a
	 * constant EOF.  READER does not block -- see the AUX banner --
	 * so 0x1A means either the reader's EOF or "nothing yet", and
	 * function 31 is how a caller tells those apart.
	 */
	case 6:					/* PUNCH(char) */
		auxout((int)d1);
		break;

	case 7:					/* READER */
		{
			register int c;

			c = auxin();
			return (c < 0 ? 0x1aL : (long)c);
		}

	case 8:					/* HOME */
		settrk = 0;
		break;

	case 9:					/* SELDSK */
		return (seldsk((int)d1));

	case 10:				/* SETTRK */
		settrk = (int)d1;
		break;

	case 11:				/* SETSEC */
		setsec = (int)d1;
		break;

	case 12:				/* SETDMA */
		setdma = d1;
		break;

	case 13:				/* READ */
		return (dskread());

	case 14:				/* WRITE */
		return (dskwrite((int)d1));

	case 15:				/* LISTST: always ready (both
						 * routes accept immediately) */
		return (0xffL);

	case 16:				/* SECTRAN: identity, xlt 0 */
		return (d1);

	case 18:				/* GMRTA */
		return ((long)&memtab);

	case 19:				/* GETIOB */
		return ((long)iobyte);

	case 20:				/* SETIOB */
		iobyte = (char)d1;
		break;

	case 21:				/* FLUSH */
		dskerr = 0;
		flushhst();
		return ((long)dskerr);

	case 22:				/* SETXVEC: record + return old */
		if ((int)d1 >= 0 && (int)d1 < 48) {
			oldv = xvec[(int)d1];
			xvec[(int)d1] = d2;
			return (oldv);
		}
		return (0L);

	case 23:				/* TIME: read/set the RTC */
		return (rtctime(d1, (int)d2));

	case 31:				/* AUXIST */
		return ((long)auxist());

	/*
	 * AUXDEV: bind the AUX device to a serial channel, in the same
	 * CD_SER() numbering function 28 uses; d1 < 0 only asks.  Answers
	 * the device it was bound to, or -1 if the argument was not one.
	 * 32 is the next free code above AUXIST, and it is on bioscl()'s
	 * allowed list for the reason function 28 is: the whole point is
	 * that a transient program decides who owns the spare wire.
	 */
	case 32:				/* AUXDEV(device) */
		return ((long)auxattach((int)d1));

	/*
	 * BIOCOST-only, cpm.h BIOS_ROMCHAR/BIOS_VSETCHAR: not stock CP/M-8000
	 * BIOS functions and not on bioscl()'s (sys/iosys.c) allowed list --
	 * they exist so src/cmd/biocost.c, reaching them through the raw
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
	}
	return (0L);
}
