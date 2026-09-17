/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/*
 * cpm3fn.c - Exercise CP/M 3 functions 42/43, 98, and 107-112. Function 98
 * must reclaim the allocation left by an unclosed file.
 */

#include "cpm.h"

static struct fcb	f;
static char		buf[SECLEN];
static char		freerec[4];
static char		serial[8];
static struct ccb	blk;

static long	freesp();
static VOID	putlong();

int main(argc, argv)
int argc;
char *argv[];
{
	register int	i;
	register int	r;
	long		free1;
	long		free2;
	long		free3;

	/* 42/43 - record lock/unlock, defined to succeed and do nothing */
	cputs("fn 42 (lock)   -> ");
	putdec((unsigned) __bdos(42, 0L));
	cputs("\r\nfn 43 (unlock) -> ");
	putdec((unsigned) __bdos(43, 0L));

	/* 107 - system serial number, six bytes to the given address */
	cputs("\r\nfn 107 serial  -> ");
	for (i = 0; i < 8; i++)
		serial[i] = 0;
	__bdos(BDOS_SERIAL, (long) serial);
	cputs(serial);

	/* 108 - program return code */
	__bdos(BDOS_RETCODE, 0x1234L);
	cputs("\r\nfn 108 set 1234, get -> ");
	putdec((unsigned) __bdos(BDOS_RETCODE, 0xffffL));

	/* 109 - console mode */
	cputs("\r\nfn 109 mode was ");
	putdec((unsigned) __bdos(BDOS_CONMODE, 0xffffL));
	__bdos(BDOS_CONMODE, 2L);		/* no ^S/^Q */
	cputs(", set 2 -> ");
	putdec((unsigned) __bdos(BDOS_CONMODE, 0xffffL));
	__bdos(BDOS_CONMODE, 0L);
	cputs(", restored ");
	putdec((unsigned) __bdos(BDOS_CONMODE, 0xffffL));

	/* 110 - output delimiter: switch fn 9 from '$' to '#' and back */
	cputs("\r\nfn 110 delim was ");
	putdec((unsigned) __bdos(BDOS_OUTDELIM, 0xffffL));
	__bdos(BDOS_OUTDELIM, (long) '#');
	printstr("  fn 9 now stops at a hash#");
	__bdos(BDOS_OUTDELIM, (long) '$');
	printstr("  and at a dollar again$");

	/* 111/112 - print block to console and to the list device */
	blk.cbaddr = (long) "\r\nfn 111 printed this block";
	blk.cblen = 27;
	__bdos(BDOS_PRTBLK, (long) &blk);
	blk.cbaddr = (long) "fn 112 list block\r\n";
	blk.cblen = 19;
	r = __bdos(BDOS_LSTBLK, (long) &blk);
	cputs("\r\nfn 112 (to list device) returned ");
	putdec((unsigned) r);

	/* 98 - free blocks.  Write to a file and leave it open: the block
	   is allocated in the FCB only, so relogging the drive must give
	   it back. */
	free1 = freesp();
	cputs("\r\nfree records before ");
	putlong(free1);
	mkfcb("FN98.TMP", &f);
	__bdos(BDOS_DELETE, (long) &f);
	mkfcb("FN98.TMP", &f);
	if ((__bdos(BDOS_MAKE, (long) &f) & 0xff) == 0xff) {
		cputs("\r\ncpm3fn: cannot create FN98.TMP\r\n");
		return (1);
	}
	setdma(buf);
	for (i = 0; i < 8; i++)
		if (__bdos(BDOS_WRITESEQ, (long) &f) != 0) {
			cputs("\r\ncpm3fn: write failed\r\n");
			return (1);
		}
	free2 = freesp();		/* the file is never closed */
	cputs(", with a block allocated ");
	putlong(free2);
	__bdos(BDOS_FREEBLK, 0L);
	free3 = freesp();
	cputs(", after fn 98 ");
	putlong(free3);
	cputs(free3 > free2 ? "\r\nCPM3FN: fn 98 freed the block\r\n"
			    : "\r\nCPM3FN: FN 98 FREED NOTHING\r\n");
	mkfcb("FN98.TMP", &f);
	__bdos(BDOS_DELETE, (long) &f);
	return (0);
}


/* function 46: free space as THREE LITTLE-ENDIAN BYTES of 128-byte
   record count at the DMA address, fourth byte zero -- the CP/M 3 wire
   form.  Assembled by hand: this machine is big-endian, so reading the
   buffer back as a `long' would be the very bug this form replaces. */
static long freesp()
{
	freerec[0] = freerec[1] = freerec[2] = freerec[3] = 0;
	setdma(freerec);
	__bdos(46, 0L);
	setdma(buf);
	return ( ((long) (freerec[0] & 0xff))
	       | ((long) (freerec[1] & 0xff) <<  8)
	       | ((long) (freerec[2] & 0xff) << 16) );
}


static VOID putlong(n)
long n;
{
	if (n >= 10L)
		putlong(n / 10L);
	conout((int) ('0' + n % 10L));
}
