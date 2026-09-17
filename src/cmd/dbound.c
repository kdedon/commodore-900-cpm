/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/*
 * dbound.c -- F1(c): a corrupt directory block number must not reach
 * another drive's allocation map.
 *
 * src/bios/bios900.c drvinit() carves every drive's `alv' out of ONE
 * 1536-byte pool, in drive order, so A:'s map is alvpool[0..319] (dsm 2559)
 * and B:'s starts at alvpool[320].  src/bdos/fileio.c alloc(), the drive
 * login scan, handed `setaloc' the block numbers out of each directory entry
 * with no comparison against dsm, and setaloc had no bound of its own: a
 * big-map word of 2600 on an A: entry set a bit at alvpool[325], marking one
 * of B:'s DATA BLOCKS allocated.
 *
 * The assertion needs no control run, because the run carries its own
 * control.  B: is logged in first and its free space read; A: is logged in
 * next, which is when its corrupt entry is scanned; B:'s free space is read
 * again, and B: is not logged in a second time, so nothing but the scan of
 * A: can have moved it.  b1 != b0 is the finding.
 *
 * The corrupt entry is put there by tests/dirpoke.py when the image is
 * built, and that script fails the build of the image -- not the build of
 * the system -- if it cannot find an entry to corrupt, so this program
 * cannot pass by being handed a clean disk.
 */

#include "cpm.h"

static char	dbuf[SECLEN];

/*  Function 46, free space on `drv', in 128-byte records.  Three
    little-endian bytes and a zero; B: holds 65408 free records at most, so
    two bytes carry it.	 */

static unsigned freerec(drv)
int drv;
{
	setdma(dbuf);
	__bdos(BDOS_FREESP, (long) drv);
	return ( (unsigned)(dbuf[0] & 0xff)
		 | ((unsigned)(dbuf[1] & 0xff) << 8) );
}

int main(argc, argv)
int argc;
char *argv[];
{
	unsigned	b0, b1;

	cputs("DBOUND: start\r\n");

	/*  Function 13 logs every drive off, A: included -- the CCP had it
	    logged in -- so the order of the three calls below is the order
	    the drives are logged in, which is the whole experiment.  */
	__bdos(BDOS_RESET, 0L);

	b0 = freerec(1);		/* B: logs in and builds its map	*/
	(void) freerec(0);		/* A: logs in: the corrupt entry	*/
	b1 = freerec(1);		/* B: is already in: nothing rebuilt	*/

	cputs("DBOUND: b0=");
	putdec(b0);
	cputs("\r\n");
	cputs("DBOUND: b1=");
	putdec(b1);
	cputs("\r\n");
	if (b0 == b1)
		cputs("DBOUND: peermap=intact\r\n");
	else
		cputs("DBOUND: peermap=damaged\r\n");
	cputs("DBOUND: done\r\n");
	return (0);
}
