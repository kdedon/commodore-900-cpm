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

#define CONMAX		4
#define NCHAN		16	/* bi_serial is 16 bits			*/
/*
 * The device number of serial channel `c'.  CD_SCCA IS CD_SER(1) -- the
 * guest-visible numbering of BIOS function 28 is unchanged, and channels
 * past the first spare simply continue it.  CD_SER(0) is CD_ROM, which
 * is the truth: channel 0 is the ROM's own console channel, and console
 * 0 reaches it the ROM's way (through crsr.c, or the keyboard on a video
 * machine) rather than as a raw line.
 */
#define CD_SER(c)	((c) + 1)

extern int inb();
extern outb();

static int convid;	/* nonzero: video console, input = local keyboard */
static int pend[CONMAX];	/* CONST lookahead, 0 = none, per console */
static char condev[CONMAX];	/* console -> device; see the banner	*/
static unsigned int conmap;	/* the serial map the table was built from */
static int ncon = 1;	/* consoles 0..ncon-1.  coninit() sets it; 1 until
			   then, so that a console call made before cold
			   start is over reaches console 0 and not an
			   unbuilt table entry. */

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
static sccport(chan, reg)
int chan, reg;
{
	if (chan < 0 || chan >= NSCCBASE)
		return (0);
	return ((int)(sccbase[chan] | (unsigned int)(reg << 1) | 1));
}
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
	return (n >= 0 && n < ncon ? n : 0);
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

static sccinit(chan)
int chan;
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

static char iobyte;

/* LIST routes to the console iff the LST: field selects TTY: or CRT:. */
#define LSTCON()	((iobyte & 0x80) == 0)

/*
 */
{
	register int c;

		register int p;

		if (p == 0)
		if ((inb(p) & RXAVAIL) == 0)
	if ((inb(SCC_RR0) & RXAVAIL) == 0)
		return (0);
	return (inb(SCC_RR8) & 0x7f);
}

{
	register int p;

	p = sccport(condev[con] - 1, 0);
	if (p == 0)
		return;			/* fitted, but not addressable here */
		if (inb(p) & TXEMPTY)
	outb(sccport(condev[con] - 1, 8), c & 0xff);
}

long p;
{
}

conststat(con)
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
#define BLSBLKS		8		/* 512-byte blocks per 4096-byte alloc blk */
#define BIDRV0		8		/* bootinfo slot of drive A: */
#define NDRIVE		7		/* A: .. G:, slots 8..14 */
/* SECLEN (128) comes from bdosdef.h */

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

/* Deblocking runs over the LRU sector cache (sys/bcb.c), whose buffers sit
 * in the seg-0x33 buffer segment the startup code maps at phys 0x0C0000;
 * the WD controller DMAs straight to their physical addresses. */

extern mem_cpy();
extern ccpentry();		/* glue.s: reset the stack, enter the CCP */
extern long tickget();		/* src/bios/tick900.c, trap.s	*/

static UBYTE dirbuf[128];
#define ALVPOOL		1536
static UBYTE alvpool[ALVPOOL];

/* The table itself.  drvthere[] is the answer seldsk gives; drvbase[] is
 * the absolute block dskread/dskwrite add to. */
static struct dpb dpbtab[NDRIVE];
static struct dph dphtab[NDRIVE];
static long drvbase[NDRIVE];
static char drvthere[NDRIVE];
static long curbase;		/* drvbase[] of the selected drive */

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
extern rtcinit();

/*
 * The segment allocator (src/bios/pgalloc.c), reached by BIOS function
 * 25 below and by nothing else in this file.
 */
extern int	pgalloc();
extern int	pgfree();
extern int	pgcount();
extern		pginit();
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

	for (chan = 0; chan < CONMAX; chan++) {
		condev[chan] = CD_NONE;
		pend[chan] = 0;
	}
	condev[0] = CD_ROM;		/* whatever the ROM chose	   */
	ncon = 1;
	/*  The AUX device (N2) starts on the SAME channel console 1 does --
	 *  the first spare port, 0x0120 on this machine.  That is not two
	 *  owners of one wire by accident: there is only one spare wire, and
	 *  which of the two owns it is the program's choice, made with
	 *  function 28 (`CONDEV(1, CD_NONE)') at the moment a transfer
	 *  starts.  Binding AUX somewhere else, or nowhere, is function 32.  */
	auxchan = -1;
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
	pginit();
	rtcinit();			/* CIO #1 Port B/PC1 -> MSM58321 */
	tickinit();			/* CIO #1 C/T 3 -> the 100 Hz tick */
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

	case 24:				/* TICK */
		return (tickget());

	case 25:				/* SEGMENT */
		switch ((int)d1) {
		case 0:
			return ((long)pgalloc());
		case 1:
			return ((long)pgfree((int)d2));
		case 2:
			return ((long)pgcount());
		}
		return (0L);

	case 26:				/* TPASWAP */
		if ((int)d1 == 0)
			return ((long)tpaphys);
		return ((long)pgtpaswap((int)d1));

	case 27:				/* CONOUTN */
		break;

	case 29:				/* CONCNT */
		return ((long)ncon);

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
