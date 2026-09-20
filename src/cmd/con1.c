/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/*
 * con1.c - Exercise console selection and per-process console routing.
 */

#include "cpm.h"

#define	BDOS_ATTCON	146		/* XDOS Attach Console		*/
#define	BDOS_DETCON	147		/* XDOS Detach Console		*/
#define	BDOS_SETCON	148		/* XDOS Set Console		*/
#define	BDOS_GETCON	153		/* XDOS Get Console Number	*/

#define	XFAIL		0xff

static char gotbuf[24];

int main(argc, argv)
int argc;
char *argv[];
{
	register int	c;
	register int	k;

	/*  With any argument at all, CON1 is just a TRANSIENT: it prints
	    one line on whatever console it was started from and exits --
	    a program that runs and reaches the warm boot on console 1,
	    showing that the session there survives its own transients.
	    It must not touch
	    the console number in that mode, which is the whole reason
	    this is one line and not a second program.  */
	if (argc > 1) {
		printstr("CON1: transient\r\n$");
		return (0);
	}

	printstr("CON1: start\r\n$");		/* console 0 */

	k = __bdos(BDOS_SETCON, 1L) & 0xff;
	if (k != 0) {
		/*  Still on console 0: a refused 148 changes nothing
		    (src/bdos/xdos.c xsetcon).  */
		printstr("CON1: FAIL -- 148 refused console 1\r\n$");
		return (1);
	}

	if ((__bdos(BDOS_GETCON, 0L) & 0xff) != 1) {
		__bdos(BDOS_SETCON, 0L);
		printstr("CON1: FAIL -- 153 did not read back 1\r\n$");
		return (1);
	}

	printstr("CON1: on 1\r\n$");		/* console 1 -- the wire */

	/*  MOVING IS NOT READING.  Function 148 above put this
	    process on console 1; it does not give it the right to take a
	    character there, because a console has an OWNER, and at cold
	    boot console 1's owner is the session sitting on it.
	    So ask for it --
	    function 146, which BLOCKS until that session detaches.

	    The line above is printed BEFORE the block on purpose: output
	    is not owned, only reading is, and that line is what the test
	    waits for before it types the `CATT D' at console 1 that makes
	    the session let go.  Announce first, then block, and the
	    hand-off has somebody to hand to.

	    The read below would have attached anyway -- conbdos.c getch()
	    does it for every program that never heard of 146 -- so this
	    call is not what makes the program correct.  It is what makes
	    it HONEST: it says where it waits, and it can say so if the
	    console it asked for turns out not to exist.	*/
	if ((__bdos(BDOS_ATTCON, 0L) & 0xff) != 0) {
		__bdos(BDOS_SETCON, 0L);
		printstr("CON1: FAIL -- 146 refused console 1\r\n$");
		return (1);
	}

	c = __bdos(BDOS_CONIN, 0L) & 0xff;	/* typed at console 1	 */

	gotbuf[0] = 'C'; gotbuf[1] = 'O'; gotbuf[2] = 'N'; gotbuf[3] = '1';
	gotbuf[4] = ':'; gotbuf[5] = ' '; gotbuf[6] = 'g'; gotbuf[7] = 'o';
	gotbuf[8] = 't'; gotbuf[9] = ' ';
	gotbuf[10] = (char)(c >= ' ' && c < 0x7f ? c : '?');
	gotbuf[11] = '\r'; gotbuf[12] = '\n'; gotbuf[13] = '$';
	gotbuf[14] = 0;
	printstr(gotbuf);			/* console 1 */

	/*  Give console 1 back before leaving it, with function 147.  The
	    warm boot would have done it anyway -- procdead() releases
	    everything a program held, which is what makes a ^C'd program
	    harmless -- but a program that takes a
	    console and returns it is the shape every program should have,
	    and this one is the example.			*/
	__bdos(BDOS_DETCON, 0L);
	__bdos(BDOS_SETCON, 0L);
	printstr("CON1: back 0\r\n$");		/* console 0 again */
	return (0);
}
