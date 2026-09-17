/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/*
 * catt.c - Exercise console attachment and detachment.
 */

#include "cpm.h"

#define	BDOS_ATTCON	146		/* XDOS Attach Console	*/
#define	BDOS_DETCON	147		/* XDOS Detach Console	*/
#define	BDOS_SETCON	148		/* XDOS Set Console	*/
#define	BDOS_GETCON	153		/* XDOS Get Console Num	*/

static char	msg[32];
static char	line[132];

/*  "CATT: <what> <digit>", built by hand: this program must run on a
    disk that has no room for printf and must not depend on one.  */

static VOID say(what, digit)
char	*what;
int	digit;
{
	register char	*p;
	register char	*q;

	p = msg;
	*p++ = 'C'; *p++ = 'A'; *p++ = 'T'; *p++ = 'T';
	*p++ = ':'; *p++ = ' ';
	for (q = what; *q; q++)
		*p++ = *q;
	if (digit >= 0) {
		*p++ = ' ';
		*p++ = (char)('0' + digit);
	}
	*p++ = '\r'; *p++ = '\n'; *p++ = '$'; *p = 0;
	printstr(msg);
}

/*  One line through function 10, which is the call that turns a ^C at
    the start of a line into a warm boot (src/bdos/conbdos.c).  The HOLD
    mode wants exactly that and nothing else.  */

static VOID getln()
{
	line[0] = 120;
	line[1] = 0;
	__bdos(BDOS_RDCONBUF, (long) line);
}

int main(argc, argv)
int argc;
char *argv[];
{
	register int	mode;
	register int	con;
	register int	k;

	mode = (argc > 1) ? argv[1][0] : 0;
	if (mode >= 'a' && mode <= 'z')
		mode -= 'a' - 'A';

	con = __bdos(BDOS_GETCON, 0L) & 0xff;

	if (mode == 'D') {
		k = __bdos(BDOS_DETCON, 0L) & 0xff;
		say(k == 0 ? "gave" : "keeps", con);
		return (0);
	}

	if (mode == 'W' || mode == 'H') {
		if ((__bdos(BDOS_SETCON, 1L) & 0xff) != 0) {
			say("no console", 1);
			return (1);
		}
		/*  On console 1, which we do not own yet: printing is not
		    owned, and that asymmetry is deliberate -- an error
		    message that could not be printed until its console was
		    free would be an error message nobody ever saw.	*/
		say(mode == 'W' ? "waiting" : "grabbing", 1);
		if ((__bdos(BDOS_ATTCON, 0L) & 0xff) != 0) {
			__bdos(BDOS_SETCON, 0L);
			say("FAIL -- 146 refused", 1);
			return (1);
		}
		say(mode == 'W' ? "attached" : "holds", 1);
		__bdos(BDOS_SETCON, 0L);

		if (mode == 'H') {
			say("held", 1);
			for (;;)
				getln();	/* ^C is the only way out */
		}

		say("got", 1);
		__bdos(BDOS_SETCON, 1L);
		k = __bdos(BDOS_DETCON, 0L) & 0xff;
		__bdos(BDOS_SETCON, 0L);
		say(k == 0 ? "freed" : "FAIL -- 147 refused", 1);
		return (0);
	}

	/*  The round trip, on this process's own console.  */
	say("con", con);

	if ((__bdos(BDOS_ATTCON, 0L) & 0xff) != 0) {
		say("FAIL -- 146 refused", con);
		return (1);
	}
	say("own", con);

	if ((__bdos(BDOS_DETCON, 0L) & 0xff) != 0) {
		say("FAIL -- 147 refused", con);
		return (1);
	}
	say("gave", con);

	/*  The second detach must be refused.  If it succeeded, 147 would
	    be a call that always says yes and the first one would have
	    proved nothing.  */
	if ((__bdos(BDOS_DETCON, 0L) & 0xff) == 0) {
		say("FAIL -- 147 twice", con);
		return (1);
	}
	say("not mine", con);

	if ((__bdos(BDOS_ATTCON, 0L) & 0xff) != 0) {
		say("FAIL -- 146 again", con);
		return (1);
	}
	say("again", con);

	/*  And the console still reads.  Anything typed will do.  */
	k = __bdos(BDOS_CONIN, 0L) & 0xff;
	say("read", (k >= '0' && k <= '9') ? k - '0' : 0);
	return (0);
}
