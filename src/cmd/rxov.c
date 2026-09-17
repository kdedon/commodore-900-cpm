/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/*
 * rxov.c - Exercise console receive overflow handling.
 */

#include "cpm.h"

#define	BDOS_ATTCON	146		/* XDOS Attach Console		*/
#define	BDOS_DETCON	147		/* XDOS Detach Console		*/
#define	BDOS_SETCON	148		/* XDOS Set Console		*/
#define	BDOS_RAWIO	6		/* direct console I/O		*/
#define	BIOS_CONRX	30		/* bios900.c case 30		*/

#define	RAW_IN		0xffL		/* fn 6: read, blocking, no echo	*/
#define	RAW_STAT	0xfeL		/* fn 6: is one waiting?	*/

#define	BURSTN		16		/* characters typed in one burst */
#define	SPIN		4000000L	/* the busy period, in iterations */

static int	blk[5];			/* fn 50 {code,P1seg,P1off,P2seg,P2off} */
static char	line[40];
static long	spin;			/* the spin's counter lives in memory
					   so that it is a real loop	*/

/*  BIOS function 30 through BDOS function 50, the way CONN.Z8K reaches
    function 29.  bioscl() answers 0xffffffff for a refused code, so a
    byte of 0xff is "this system has no such call".  */
static int biosrx(con, mode)
int con, mode;
{
	blk[0] = BIOS_CONRX;
	blk[1] = 0;
	blk[2] = con;
	blk[3] = 0;
	blk[4] = mode;
	return ((int)(__bdos(50, (long)blk) & 0xff));
}

static say(tag, n)
char *tag;
int n;
{
	register int	i;

	i = 0;
	while (*tag != 0)
		line[i++] = *tag++;
	if (n > 9) {
		line[i++] = (char)('0' + n / 10);
		n %= 10;
	}
	line[i++] = (char)('0' + n);
	line[i++] = '\r';
	line[i++] = '\n';
	line[i++] = '$';
	line[i] = 0;
	printstr(line);
}

/*  One phase.  Returns HOW MANY of the burst's characters were received,
    or -1 if what arrived was not the burst.  `mark' is what the harness
    waits for before it types.

    WHAT "not the burst" MEANS, and why it is not "consecutive".  The
    polled phase is EXPECTED to lose characters -- that is the whole
    point -- so a gap cannot be an error.  What can be: a character from
    outside the burst, a repeat, or a pair out of order.  So each
    character must be strictly greater than the last and no greater than
    the burst's last.  A phase that returns BURSTN under that rule has
    had every character, in order, because BURSTN strictly increasing
    values out of BURSTN possible ones can only be all of them.  */
static burst(mark)
char *mark;
{
	register int	c;
	register int	n;
	register int	last;

	printstr(mark);				/* on console 1 */

	c = (int)(__bdos(BDOS_RAWIO, RAW_IN) & 0xff);
	if (c != 'A')
		return (-1);			/* not the burst we sent */
	last = c;
	n = 1;

	for (spin = 0; spin < SPIN; spin++)	/* busy, and not in the BDOS */
		;

	while (n < BURSTN
	       && (__bdos(BDOS_RAWIO, RAW_STAT) & 0xff) != 0) {
		c = (int)(__bdos(BDOS_RAWIO, RAW_IN) & 0xff);
		if (c <= last || c > 'A' + BURSTN - 1)
			return (-1);
		last = c;
		n++;
	}
	return (n);
}

int main(argc, argv)
int argc;
char *argv[];
{
	int	ring;
	int	poll;
	int	mode;

	printstr("RXOV: start\r\n$");		/* console 0 */

	if (biosrx(1, -1) == 0xff) {
		printstr("RXOV: FAIL -- BIOS function 30 refused, or console\r\n$");
		printstr("      1 is not a serial channel here\r\n$");
		return (1);
	}
	if ((__bdos(BDOS_SETCON, 1L) & 0xff) != 0) {
		printstr("RXOV: FAIL -- 148 refused console 1\r\n$");
		return (1);
	}

	/*  C10: console 1 has an OWNER -- the session the cold boot starts
	    there -- and function 148 does not make this process it.  Both
	    bursts below are read through function 6, which CONSUMES
	    characters, so a session still sitting in getch() on this
	    console would take them off the wire while this program was
	    busy spinning, and the two phases would be measuring that
	    instead of the driver.  So ask for the console: 146 blocks
	    until the session detaches, and the line printed just above is
	    what the test waits for before it types the `CATT D' that
	    makes it.						*/
	printstr("RXOV: on 1\r\n$");		/* console 1 -- the wire */
	if ((__bdos(BDOS_ATTCON, 0L) & 0xff) != 0) {
		__bdos(BDOS_SETCON, 0L);
		printstr("RXOV: FAIL -- 146 refused console 1\r\n$");
		return (1);
	}

	/*  Phase 1: the ring.  If the channel cannot be armed at all there
	    is nothing to compare and the run says so rather than passing
	    on a polled driver measured twice.  */
	mode = biosrx(1, 1);
	if (mode != 1) {
		__bdos(BDOS_SETCON, 0L);
		printstr("RXOV: FAIL -- console 1 cannot be armed\r\n$");
		return (1);
	}
	ring = burst("RXOV: burst 1\r\n$");

	/*  Phase 2: the same channel, polled -- the driver C3 shipped.  */
	mode = biosrx(1, 0);
	if (mode != 0) {
		__bdos(BDOS_SETCON, 0L);
		printstr("RXOV: FAIL -- console 1 will not go polled\r\n$");
		return (1);
	}
	poll = burst("RXOV: burst 2\r\n$");

	biosrx(1, 1);				/* leave it as it was found */
	__bdos(BDOS_DETCON, 0L);		/* and give the console back
						   to whoever wants it next */
	__bdos(BDOS_SETCON, 0L);

	/*  Everything below is on console 0.  */
	if (ring < 0) {
		printstr("RXOV: FAIL -- the ring delivered the burst out of\r\n$");
		printstr("      order, or delivered something else\r\n$");
		return (1);
	}
	if (poll < 0) {
		printstr("RXOV: FAIL -- the polled phase delivered the burst\r\n$");
		printstr("      out of order, or delivered something else\r\n$");
		return (1);
	}
	say("RXOV: sent=", BURSTN);
	say("RXOV: ring=", ring);
	say("RXOV: polled=", poll);
	if (ring < BURSTN) {
		printstr("RXOV: FAIL -- the ring lost characters too\r\n$");
		return (1);
	}
	if (poll >= BURSTN) {
		printstr("RXOV: FAIL -- the polled driver lost nothing, so\r\n$");
		printstr("      nothing here distinguishes the two\r\n$");
		return (1);
	}
	printstr("RXOV: ring kept all, polled did not\r\n$");
	return (0);
}
