/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/*
 * xdospol.c - Exercise a blocked console poll with a competing process.
 * Attach before waiting so the selected console can deliver input to this
 * process.
 */

#include "cpm.h"

#define	BDOS_ATTCON	146		/* XDOS Attach Console		*/
#define	BDOS_DETCON	147		/* XDOS Detach Console		*/
#define	BDOS_SETCON	148		/* XDOS Set Console		*/
#define	BDOS_XPOLL	131		/* XDOS Poll Device		*/
#define	BDOS_CREATEPROC	144		/* XDOS create process		*/
#define	BDOS_PROCCNT	145		/* how many processes are live	*/

#define	POLLCON		1		/* the console fn 131 is proved on:
					   the wire, never scripted
					   console 0 input		*/

struct pcreq {
	struct fcb	pq_fcb;
	char		pq_tlen;
	char		pq_tail[128];
};

static struct pcreq	req;

static char *why[] = {
	"",
	"no such program",
	"not enough memory in the new page",
	"read error loading it",
	"program load error",
	"no free process descriptor",
	"no free 64 KB page -- this needs a 1 MB machine",
	"a second split-I/D program would need a second data bank",
	"no such console"
};

static char lbuf[48];

/*  ONE cputs() PER LINE -- src/cmd/xdosd.c countline()'s reason: a process
    is switched away from only at a BDOS gate or by the tick in Normal mode,
    so several small calls leave a gap another process's line can land in;
    one call composing the whole line leaves none.  */

static VOID numline(tag, n)
char	*tag;
long	n;
{
	register char	*p;
	register long	d;
	register int	seen;

	for (p = lbuf; *tag != 0; )
		*p++ = *tag++;
	seen = 0;
	for (d = 100000L; d > 0L; d /= 10L)
		if (n / d != 0L || seen || d == 1L) {
			*p++ = '0' + (int)((n / d) % 10L);
			seen = 1;
		}
	*p++ = '\r';
	*p++ = '\n';
	*p = 0;
	cputs(lbuf);
}

int main(argc, argv)
int argc;
char *argv[];
{
	register int	i, k;
	int		poll;
	int		c;

	poll = (argc > 1 && (argv[1][0] == 'P' || argv[1][0] == 'p'));

	cputs(poll ? "XDOSPOL: start with a poller\r\n"
		   : "XDOSPOL: start alone\r\n");

	mkfcb("CONCY.Z8K", &req.pq_fcb);
	req.pq_tlen = 0;
	for (i = 0; i < 128; i++)
		req.pq_tail[i] = 0;

	k = __bdos(BDOS_CREATEPROC, (long)&req);
	if (k != 0) {
		cputs("XDOSPOL: no second process: ");
		cputs(k > 0 && k < 9 ? why[k] : "refused");
		cputs("\r\n");
		return (1);
	}

	/*  HOW MANY PROCESSES THE MEASURED RUN IS SHARING THE MACHINE WITH --
	    2 alone, 3 with the poller.  On console 0, before this process
	    ever touches console 1, so it lands in the same transcript every
	    time regardless of which mode this is.  */
	numline("XDOSPOL: live=", (long)(__bdos(BDOS_PROCCNT, 0L) & 0xff));

	if ( ! poll)
		return (0);

	k = __bdos(BDOS_SETCON, (long)POLLCON) & 0xff;
	if (k != 0) {
		cputs("XDOSPOL: no console 1: ");
		cputs(k > 0 && k < 9 ? why[k] : "refused");
		cputs("\r\n");
		return (1);
	}

	/*  Console 1 has an OWNER, and 148 did not make this process
	    it.  That matters twice here.  Function 131 only PEEKS, so it
	    would return on a byte the session then read instead of this
	    program; and the function 1 below CONSUMES, so it would block
	    behind the owner anyway.  So ask for the console -- 146 blocks
	    until the session detaches -- and print the marker the test
	    reacts to ONE LINE EARLIER than the wait marker, so that the
	    detach happens before, and the injected byte after, this
	    process is parked in fn 131.  The ordering the target's whole
	    proof rests on is unchanged; there is one more step in front
	    of it.						*/
	cputs("XDOSPOL: asking\r\n");		/* console 1 -- the wire */
	if ((__bdos(BDOS_ATTCON, 0L) & 0xff) != 0) {
		__bdos(BDOS_SETCON, 0L);
		cputs("XDOSPOL: no console 1: 146 refused\r\n");
		return (1);
	}

	/*  Console 1 from here on -- the wire.  The injected byte is sent
	    only once this line has been seen, which is what makes "the
	    call genuinely waited for it" provable rather than assumed.  */
	cputs("XDOSPOL: waiting\r\n");

	__bdos(BDOS_XPOLL, 0L);		/* fn 131, device 0: blocks until
					   bconstat() on THIS console (concur,
					   set by fn 148 above) is true.  A
					   peek, not a read -- proc.c xpoll()
					   never consumes the byte.	*/

	c = __bdos(BDOS_CONIN, 0L) & 0xff;	/* the byte fn 131 only found
						   waiting; read it for real */

	lbuf[0] = 'X'; lbuf[1] = 'D'; lbuf[2] = 'O'; lbuf[3] = 'S';
	lbuf[4] = 'P'; lbuf[5] = 'O'; lbuf[6] = 'L'; lbuf[7] = ':';
	lbuf[8] = ' '; lbuf[9] = 'g'; lbuf[10] = 'o'; lbuf[11] = 't';
	lbuf[12] = ' ';
	lbuf[13] = (char)(c >= ' ' && c < 0x7f ? c : '?');
	lbuf[14] = '\r'; lbuf[15] = '\n'; lbuf[16] = 0;
	cputs(lbuf);				/* console 1, still */

	__bdos(BDOS_DETCON, 0L);		/* give console 1 back	*/
	__bdos(BDOS_SETCON, 0L);
	cputs("XDOSPOL: back 0\r\n");		/* console 0 again */
	cputs("XDOSPOL: done\r\n");
	return (0);
}
