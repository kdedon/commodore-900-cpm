/*
 * wd900.c -- polled WD/SASI hard-disk sector I/O for the CP/M-8000 BIOS.
 *
 * The controller executes a 16-byte class-0 command block fixed at physical
 * 0x080000 (hard disks; +0x10 is the floppy block).  That physical page is
 * our own text segment's first page, whose leading 0x600 bytes are a pad
 * (crt.s), so an RW window mapped over it (seg WDCBSEG) lets C build the
 * command block in place without touching real code.  The controller DMAs
 * sector data directly to the 24-bit physical address in bytes 6..8 of the
 * block -- the MMU is bypassed, so callers pass a physical address.
 *
 * Block layout (coherent/os/sys/z8001/drv/wd.c WDCMD, boot/src/stage2/wd.c):
 *	cb[0]     opcode: 0x08 read, 0x0A write, 0x0C set drive parameters
 *	cb[1]     unit<<5 | block bits 20-16
 *	cb[2..3]  block bits 15-0
 *	cb[4]     sector count
 *	cb[6..8]  DMA physical address, high/mid/low
 *	cb[0x0c]  completion status: preset 0xff, polled; 0x80 = done,
 *	          0x76 = busy (reissue the command), else error code
 * wdgo strobes port 0x0500 (word) to start/stop the controller.
 */
#include "romabi.h"

#define WDIO		0x0500		/* controller go/stop port (word) */
#define WDCBSEG		0x34		/* window segment over the block */
#define WDCBPAGE	0x0800		/* phys 0x080000 in 256-byte pages */
/* The command block's address is a guard-wrapped macro for the same reason
 * crsr.c's VSET is: tests/wdtest.c compiles THIS source on the host with
 * the block pointed at an array, so wdsec()'s retry behaviour can be
 * driven and asserted without a controller (verify-wdbusy). */
#ifndef WDCB
#define WDCB		((char *)0x34000000L)
#endif

extern outw();
extern mapseg();
extern long tickget();		/* src/bios/tick900.c, trap.s	*/
extern int tickpast();

/*
 * Map the command-block window.  Call once before any wdsec().
 */
wdinit900()
{
	mapseg(WDCBSEG, WDCBPAGE, 2);	/* attr 2 = system read/write */
}

/* Poll the command completion byte with a three-second deadline.
 * A spin budget also bounds the wait if the tick is unavailable;
 * check the clock every 256 polls. Timeout leaves completion at 0xff. */
#define WDWAIT		300L		/* ticks (100 Hz) -- 3 seconds	*/
/* Attempts on a 0x76 "controller busy, retry" answer.  Each one restarts
 * WDWAIT, so this also bounds the total wait: five times three seconds,
 * and then a status byte the caller can report instead of a hang. */
#define WDRETRY		5

static wdgo900()
{
	register char *cb;
	register long n;
	long dl;

	cb = WDCB;
	outw(WDIO, 1);
	dl = tickget() + WDWAIT;
	n = 2000000L;
	while ((cb[0x0c] & 0xff) == 0xff) {
		if (--n == 0)
			break;
		if ((n & 0xffL) == 0L && tickpast(dl))
			break;
	}
	outw(WDIO, 0);
}

/*
 * Transfer one 512-byte sector on unit 0.
 * op = 0x08 (read) or 0x0A (write); blk = absolute disk block number;
 * phys = 24-bit physical DMA address, 512-byte aligned.
 * Returns 0 on success, else the controller status byte (0xff = timeout).
 */
int wdsec(op, blk, phys)
int op;
long blk, phys;
{
	register char *cb;
	register int i, st;
	register int tries;

	cb = WDCB;
	for (tries = WDRETRY; ; ) {
		for (i = 0; i < 16; i++)
			cb[i] = 0;
		cb[0] = op;
		cb[1] = blk >> 16;	/* unit 0: lun bits stay clear */
		cb[2] = blk >> 8;
		cb[3] = blk;
		cb[4] = 1;		/* one sector */
		cb[6] = phys >> 16;	/* DMA target (bypasses the MMU) */
		cb[7] = phys >> 8;
		cb[8] = phys;
		cb[0x0c] = 0xff;	/* completion byte, polled */
		wdgo900();
		st = cb[0x0c] & 0xff;
		if (st == 0x80)		/* done */
			return (0);
		if (st != 0x76)		/* not "busy, retry" */
			return (st);
		/*
		 * "Busy, retry" used to be retried forever, and each attempt
		 * called wdgo900(), which starts a FRESH three-second
		 * deadline -- so a controller wedged in 0x76 hung the whole
		 * machine inside one BIOS read with no error ever reaching
		 * the BDOS.  Bound the total instead and hand 0x76 back as
		 * the status, which is a real controller byte the disk error
		 * path already knows how to report: WDRETRY attempts at up
		 * to WDWAIT ticks each is the worst case the caller waits.
		 */
		if (--tries <= 0)
			return (st);
	}
}
