/*
 * concr.c - ONE OF THE TWO CREATORS THAT MUST BOTH BE PARKED INSIDE
 *	     pcrgen() AT THE SAME TIME, or the ballast that makes the
 *	     descriptor table full enough for that to matter.
 *	     src/cmd/concl.c is the lock holder and its banner describes
 *	     the whole arrangement.
 *
 * The role is the command tail, because the three roles differ only in a
 * delay and a string and one program on the medium is one fewer thing to
 * keep in step:
 *
 *   A  the FIRST creator.  Waits for CONCL to be parked at its operator
 *      prompt holding the file-system lock, then asks for MHELLO.Z8K with
 *      a tail of its own.  pcrgen() picks a descriptor, finds the lock
 *      held, and parks -- and STAYS parked, because the prompt is not
 *      answered until B has asked too.
 *   B  the SECOND creator, and the one the whole target turns on.  It
 *      asks LATER than A, so A is already parked with a descriptor
 *      picked when B's own descriptor search runs -- and nothing has yet
 *      released the lock, because only Z can do that.  The lock is
 *      therefore still held, by a THIRD process, at the instant B
 *      searches, which is the one thing verify-concr could not arrange
 *      (docs/cpm/docs/run/F4.md: there CONCM was the holder AND the
 *      second creator, so the gate-return dispatch served the parked
 *      creator before CONCM had searched at all).
 *   Z  BALLAST, AND THE RELEASE.  It makes the number of FREE descriptors
 *      exactly one -- two free and the creators take one each and never
 *      contend, none free and both are refused before they reach the
 *      yield -- and it prints the --input-mark that lets the answer to
 *      CONCL's prompt through, last of the three.  It must be a process
 *      that is NOT racing: see the comment at the print.
 *
 * A and B ask for the SAME program with DIFFERENT tails, so the
 * transcript says which request was served: MHELLO echoes its arguments.
 */

#include "cpm.h"

#define	BDOS_CREATEPROC	144		/* XDOS create process		*/
#define	BDOS_DELAY	141		/* XDOS delay, in ticks		*/

/*  Ticks.  These are one-sided waits and not calibrations: CONCL's prompt
    stays unanswered until Z prints, so A and B stay parked however long Z
    takes.  ADELAY has only to be long enough for CONCL to get from its
    three creates to the prompt (a file create, a record write and a
    close); BDELAY only long enough after that for A to have parked, and
    ZDELAY long enough after THAT for B to have parked.  BALLAST outlives
    the rest of the run.

    THE MARGINS ARE NOT WHAT MAKES THE WINDOW: nothing can release the
    prompt until Z prints, so each wait only has to be ordered, not
    timed.  */

#define	ADELAY		300
#define	BDELAY		450
#define	ZDELAY		700
#define	BALLAST		1200

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

static char lbuf[80];

/*  One line, one BDOS call: src/cmd/conci.c says why at length.  */

static VOID numline(tag, n)
char	*tag;
int	n;
{
	register char	*p;
	register int	d, seen;

	for (p = lbuf; *tag != 0; )
		*p++ = *tag++;
	seen = 0;
	for (d = 10000; d > 0; d /= 10)
		if (n / d != 0 || seen || d == 1) {
			*p++ = '0' + (n / d) % 10;
			seen = 1;
		}
	*p++ = '\r';
	*p++ = '\n';
	*p = 0;
	cputs(lbuf);
}

static VOID tagline(role, text)
char	role, *text;
{
	register char	*p;

	p = lbuf;
	*p++ = 'C'; *p++ = 'O'; *p++ = 'N'; *p++ = 'C'; *p++ = 'R';
	*p++ = ' '; *p++ = role; *p++ = ':'; *p++ = ' ';
	while (*text != 0)
		*p++ = *text++;
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
	register char	*p;
	char		role;

	role = (argc > 1 && argv[1][0] != 0) ? argv[1][0] : '?';

	tagline(role, "alive");

	if (role == 'W') {
		/*  BALLAST AND NOTHING ELSE, for verify-concr, whose
		    release is CONCO's own print and which therefore wants
		    a filler that never speaks.  */
		__bdos(BDOS_DELAY, (long)BALLAST);
		tagline(role, "ballast done");
		return (0);
	}
	if (role == 'Z') {
		/*  BALLAST, AND THE STARTER'S PISTOL.  It holds a
		    descriptor so that exactly one is free, and it prints
		    the line that is the emulator's --input-mark -- which
		    is the whole reason the release is not B's own print.

		    THE BDOS GATE DISPATCHES ON EVERY CALL RETURN when
		    more than one process is live (src/bdos/bdosglue.s
		    tests `psched' at the SC return and calls pdisp_
		    unconditionally; the quantum gates the TIMER path, not
		    this one).  So a creator that prints the mark itself
		    loses the machine at that print's own gate return, the
		    lock holder takes the answer that has just arrived,
		    releases the lock, and the creator already parked
		    COMPLETES -- all before the printer's own descriptor
		    search has run.  That was measured, not reasoned
		    about: with the mark on B's `asking' line this target
		    passed even with PS_RSVD removed, which is F4's dead
		    end in a new place.  Releasing from a fourth process
		    that is not racing decouples the two, and then both
		    creators are certainly parked when the lock goes.  */
		__bdos(BDOS_DELAY, (long)ZDELAY);
		tagline(role, "releasing the prompt now");
		__bdos(BDOS_DELAY, (long)BALLAST);
		tagline(role, "ballast done");
		return (0);
	}
	if (role != 'A' && role != 'B') {
		/*  W and Z have returned above; anything else is a
		    mis-spelled command tail and not a silent no-op.  */
		tagline(role, "FAIL -- no role in the command tail");
		return (1);
	}

	/*  The request, built before the wait: MHELLO echoes its
	    arguments, so the tail is the evidence of WHOSE request was
	    served.  */
	mkfcb("MHELLO.Z8K", &req.pq_fcb);
	for (i = 0; i < 128; i++)
		req.pq_tail[i] = 0;
	i = 0;
	for (p = (role == 'A') ? " QA" : " QB"; *p != 0; p++)
		req.pq_tail[i++] = *p;
	req.pq_tlen = (char)i;

	__bdos(BDOS_DELAY, (long)((role == 'A') ? ADELAY : BDELAY));

	/*  The `asking' line is evidence, not synchronisation: the prompt
	    is released by Z and by nothing else, so this print cannot
	    shorten the window it is reporting.  */
	tagline(role, "asking");
	k = __bdos(BDOS_CREATEPROC, (long) &req) & 0xff;

	numline(role == 'A' ? "CONCR A: answered " : "CONCR B: answered ", k);
	if (k != 0)
		tagline(role, k > 0 && k < 9 ? why[k] : "unknown");
	tagline(role, "done");
	return (0);
}
