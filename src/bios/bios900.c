#include "romabi.h"
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

biosinit()
{
	extern int mapseg();

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

		break;


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
	}
	return (0L);
}
