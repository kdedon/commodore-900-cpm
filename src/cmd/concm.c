/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/*
 * concm.c - TWO CREATORS, ONE FREE DESCRIPTOR, AND A YIELD IN THE MIDDLE
 *	     OF CREATION.  The lock holder half; src/cmd/conco.c is the
 *	     other creator.
 *
 * src/bdos/proc.c pcrgen() picks a free process descriptor and then calls
 * plock(), which PARKS the caller whenever another process holds the
 * filesystem lock (proc.c plock -> pwait(PW_LOCK) -> pyield).  Until the
 * slot was claimed before that yield, a second creator resuming in the
 * window found it still PS_FREE and built its process in it; and the
 * request itself was copied into one resident buffer shared by every
 * creator, so whichever creator went second replaced the other's FCB and
 * command tail.  The first creator then came back and loaded the other
 * program's file into its own child's page.
 *
 * THE ARRANGEMENT, which is verify-conclk's plus a second creator:
 *
 *   1. this program creates CONCO.Z8K -- done while the lock is free,
 *      because a creation cannot complete while it is held;
 *   2. it closes a file whose FCB says read-only, which is the one
 *      operator prompt that comes up UNDER the lock (src/bdos/fileio.c
 *      close, src/bdos/bdosmisc.c filero).  The answer is held back by
 *      the emulator until CONCO says it is ready, so this process parks
 *      at the prompt HOLDING the lock;
 *   3. CONCO asks for a process, finds the lock held, and parks inside
 *      pcrgen() -- with the descriptor it picked;
 *   4. the answer arrives, the close finishes, the lock is released, and
 *      THE VERY NEXT THING this process does is ask for a process of its
 *      own.  There is no BDOS call in between on purpose: the descriptor
 *      search below must run before the parked creator gets the machine
 *      back, which is the whole race.
 *
 * WHAT THE TWO PROGRAMS ASK FOR IS DIFFERENT ON PURPOSE.  CONCO asks for
 * MHELLO.Z8K with a command tail of its own; this program asks for
 * CONCB.Z8K.  Exactly one of the two can be served -- CONCO's, because it
 * got there first and there is one free descriptor -- so MHELLO's greeting
 * must be in the transcript and CONCB must never run.  If the parked
 * creator's request or its slot had been taken from under it, CONCB would
 * run instead and MHELLO would not.
 *
 * WHAT THIS ARRANGEMENT CANNOT REACH is the race itself, and
 * tests/verify.mk verify-concr says so at length: both creators would have
 * to pick the same free slot before either resumed, which needs a THIRD
 * process to be the lock holder.  This program is the holder AND a
 * creator, which is as close as four descriptors got.
 *
 * THAT TEST NOW EXISTS ELSEWHERE.  PNPROC became 6 (F14), and
 * src/cmd/concl.c plus src/cmd/concr.c build the discriminating version --
 * a holder that creates nothing, two creators, ballast enough that exactly
 * one descriptor is free, and the release printed by a fourth process so
 * that neither racer's own output ends the window.  verify-concr2 FAILS
 * with the reservation removed; this target does not, and it stays exactly
 * what it honestly is: the regression that a creator parked inside
 * pcrgen() resumes with its own program and nothing deadlocks.
 */

#include "cpm.h"

#define	BDOS_CREATEPROC	144		/* XDOS create process		*/
#define	BDOS_PROCCNT	145		/* how many processes are live	*/
#define	TARGET		"CONCM.TXT"

struct pcreq {
	struct fcb	pq_fcb;
	char		pq_tlen;
	char		pq_tail[128];
};

static struct pcreq	req;
static struct fcb	f;
static char		rec[SECLEN];

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

static VOID mkreq(name, tail)
char	*name, *tail;
{
	register int	i;
	register char	*p;

	mkfcb(name, &req.pq_fcb);
	for (i = 0; i < 128; i++)
		req.pq_tail[i] = 0;
	i = 0;
	for (p = tail; *p != 0; p++)
		req.pq_tail[i++] = *p;
	req.pq_tlen = (char)i;
}


int main(argc, argv)
int argc;
char *argv[];
{
	register int	i, k, nball;

	cputs("CONCM: M start\r\n");

	nball = 0;
	if (argc > 1)
		for (i = 0; argv[1][i] >= '0' && argv[1][i] <= '9'; i++)
			nball = nball * 10 + (argv[1][i] - '0');

	mkfcb(TARGET, &f);
	__bdos(BDOS_DELETE, (long) &f);
	mkfcb(TARGET, &f);
	if (__bdos(BDOS_MAKE, (long) &f) == 255) {
		cputs("CONCM: FAIL -- cannot create the target\r\n");
		return (1);
	}
	for (i = 0; i < SECLEN; i++)
		rec[i] = 'm';
	setdma(rec);
	if (__bdos(BDOS_WRITESEQ, (long) &f) != 0) {
		cputs("CONCM: FAIL -- write error\r\n");
		return (1);
	}

	/*  The other creator, while the lock is still free.  */
	mkreq("CONCO.Z8K", "");
	k = __bdos(BDOS_CREATEPROC, (long) &req) & 0xff;
	if (k != 0) {
		cputs("CONCM: no second creator: ");
		cputs(k > 0 && k < 9 ? why[k] : "refused");
		cputs("\r\n");
		return (1);
	}
	cputs("CONCM: O created\r\n");

	/*  BALLAST, so that exactly ONE descriptor is free when CONCO
	    parks with one picked -- which is what makes this program's own
	    request below have to be refused.  The count is the command
	    tail's, because this program cannot see PNPROC and the target
	    that runs it can: at PNPROC 4 it was zero and the arrangement
	    was implicit in the constant, which is exactly why raising
	    PNPROC to 6 broke this target (F14).  CONCR W is filler that
	    never prints anything until it is over, so it cannot disturb
	    the --input-mark this target releases its prompt on.  */
	for (i = 0; i < nball; i++) {
		mkreq("CONCR.Z8K", " W");
		k = __bdos(BDOS_CREATEPROC, (long) &req) & 0xff;
		if (k != 0) {
			cputs("CONCM: no ballast: ");
			cputs(k > 0 && k < 9 ? why[k] : "refused");
			cputs("\r\n");
			return (1);
		}
	}
	numline("CONCM: live before the race ", __bdos(BDOS_PROCCNT, 0L) & 0xff);

	/*  THIS PROGRAM'S OWN REQUEST, BUILT NOW.  Between the close
	    below and the create after it there must be no BDOS call and
	    no chance of one: see the banner.  */
	mkreq("CONCB.Z8K", "");

	/*  The read-only bit goes into the FCB now, AFTER the write, so
	    the only thing that can see it is close() -- src/cmd/conch.c
	    does the same for the same reason.  */
	f.ftype[0] |= 0x80;

	cputs("CONCM: closing -- the prompt below is UNDER the lock\r\n");
	__bdos(BDOS_CLOSE, (long) &f);
	k = __bdos(BDOS_CREATEPROC, (long) &req) & 0xff;

	numline("CONCM: my own request answered ", k);
	if (k == 5)
		cputs("CONCM: refused 5, the slot was already claimed\r\n");
	else if (k == 0)
		cputs("CONCM: CREATED -- the slot CONCO had picked was still"
		      " free to be picked again\r\n");
	else {
		cputs("CONCM: refused for another reason: ");
		cputs(k > 0 && k < 9 ? why[k] : "unknown");
		cputs("\r\n");
	}
	cputs("CONCM: M done\r\n");
	return (0);
}
