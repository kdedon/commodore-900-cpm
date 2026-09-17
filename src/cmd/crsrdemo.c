/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/*
 * crsrdemo.c - Exercise cursor addressing, movement, erasure, and escape-
 * sequence cancellation.
 */

#include "cpm.h"

#define ESC	033

/* row 0 is the top line; column 0 is the left margin */
#define BOXTOP	4
#define BOXBOT	14
#define BOXLFT	10
#define BOXRGT	60

static esc(c)
int c;
{
	conout(ESC);
	conout(c);
}

/* ESC Y row+' ' col+' ' -- H19/VT52 direct cursor addressing */
static at(r, c)
int r, c;
{
	conout(ESC);
	conout('Y');
	conout(r + ' ');
	conout(c + ' ');
}

static say(s)
register char *s;
{
	while (*s != '\0')
		conout(*s++);
}

/* n copies of one character, left to right from wherever the cursor is */
static run(ch, n)
int ch, n;
{
	while (n-- > 0)
		conout(ch);
}

int main(argc, argv)
int argc;
char *argv[];
{
	register int	i;

	esc('E');			/* clear screen, cursor home	*/

	at(BOXTOP, BOXLFT);
	run('-', BOXRGT - BOXLFT + 1);
	at(BOXBOT, BOXLFT);
	run('-', BOXRGT - BOXLFT + 1);
	for (i = BOXTOP + 1; i < BOXBOT; i++) {
		at(i, BOXLFT);
		conout('|');
		at(i, BOXRGT);
		conout('|');
	}
	at(BOXTOP, BOXLFT);
	conout('+');
	at(BOXTOP, BOXRGT);
	conout('+');
	at(BOXBOT, BOXLFT);
	conout('+');
	at(BOXBOT, BOXRGT);
	conout('+');

	at(BOXTOP + 2, BOXLFT + 4);
	say("CP/M-8000 on the Commodore 900");
	at(BOXTOP + 4, BOXLFT + 4);
	say("the console can be addressed:");
	at(BOXTOP + 6, BOXLFT + 8);
	say("row 10, column 18 -- here.");

	/*
	 * The four one-step motions, checked the only way a screen can
	 * check them: park somewhere, step, and leave a mark.  Each
	 * letter lands one cell away from the anchor at (2, 40).
	 */
	at(2, 40);
	esc('D');	conout('L');	/* left  -> (2, 39)	*/
	at(2, 40);
	esc('C');	conout('R');	/* right -> (2, 41)	*/
	at(2, 40);
	esc('A');	conout('U');	/* up    -> (1, 40)	*/
	at(2, 40);
	esc('B');	conout('D');	/* down  -> (3, 40)	*/
	at(2, 46);
	say("<- one step each way from (2,40)");

	/*
	 * Erase to end of line: write a long line at row 16, then chop
	 * it at column 30.  Erase to end of screen: fill rows 18-20 and
	 * then erase from (19, 20) down.
	 */
	at(16, 0);
	run('x', 70);
	at(16, 30);
	esc('K');
	at(16, 0);
	say("EOL");

	at(18, 0);
	run('y', 70);
	at(19, 0);
	run('y', 70);
	at(20, 0);
	run('y', 70);
	at(19, 20);
	esc('J');
	at(18, 0);
	say("EOS");

	/*
	 * Two properties of the parser rather than of the screen.  ESC &
	 * is a sequence this layer does not claim: both bytes must reach
	 * the console untouched and the "PT" after them must still be
	 * printed, not swallowed.  ESC Y CAN is an address abandoned
	 * halfway: CAN ends it, so "CN" is text again.  If either were
	 * mishandled the two labels below would not appear at all.
	 */
	at(21, 0);
	esc('&');
	say("PT");
	at(21, 10);
	conout(ESC);
	conout('Y');
	conout(030);			/* CAN: abandon the address	*/
	say("CN");

	at(22, 0);
	say("CRSRDEMO done.");
	at(23, 0);
	return (0);
}
