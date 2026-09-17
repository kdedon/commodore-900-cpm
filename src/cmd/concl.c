/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/*
 * concl.c - THE LOCK HOLDER THAT IS NOT ALSO A CREATOR, which is the one
 *	     thing F4's verify-concr could not have and the reason it could
 *	     not tell src/bdos/proc.c's PS_RSVD reservation from its
 *	     absence.  src/cmd/concr.c is the creators and the ballast.
 *
 * THE DEFECT (F4, P1 #7).  pcrgen() picks a free process descriptor and
 * then calls plock(), which PARKS the caller whenever another process
 * holds the file-system lock.  The slot is claimed with PS_RSVD BEFORE
 * that yield; without the claim a second creator resuming in the window
 * found the same slot still PS_FREE, picked it, and both of them built a
 * process in it.
 *
 * WHY THE HOLDER HAS TO BE A THIRD PROCESS.  verify-concr's CONCM is the
 * holder AND the second creator, so when its close() releases the lock the
 * dispatcher hands the machine to the parked creator at that call's own
 * gate return -- the parked creator COMPLETES, and CONCM then searches and
 * finds the slot live.  It is told 5 with the reservation and 5 without it.
 * Both creators have to have SEARCHED before either resumes, and that
 * needs somebody else to be holding the lock.
 *
 * THE ARRANGEMENT, which needs five live processes and a spare descriptor
 * -- constructible only since PNPROC became 6:
 *
 *   1. the cold-boot session on console 1 holds one descriptor for as
 *      long as the machine is up, and this program is the foreground, so
 *      two are gone before it does anything;
 *   2. it creates CONCR A and CONCR B, the two creators, while the lock
 *      is free -- a creation cannot complete while it is held;
 *   3. it creates as many CONCR Z ballast processes as its command tail
 *      asks for, so that exactly ONE descriptor is left free.  With two
 *      free the creators take one each and never contend; with none they
 *      are both refused before they reach the yield.  It PRINTS the live
 *      count either side of that (BDOS function 145), so the transcript
 *      says what the arrangement actually was rather than what it was
 *      meant to be;
 *   4. it closes a file whose FCB says read-only, which is the one
 *      operator prompt that comes up UNDER the lock (src/bdos/fileio.c
 *      close, src/bdos/bdosmisc.c filero), and parks there HOLDING the
 *      lock.  It is not a creator and it will not create: from here to
 *      the end of the race this process does nothing at all;
 *   5. A asks for a process, finds the lock held, parks inside pcrgen()
 *      with a descriptor picked;
 *   6. B asks for a process and parks the same way.  Nothing has
 *      released the lock, because the answer to the prompt above is held
 *      back until the BALLAST prints -- which it does last, and which is
 *      why the release must not come from a racer (src/cmd/concr.c says
 *      what happens when it does, and it was measured).
 *
 * WHAT THE ANSWERS MEAN, and this is the whole verdict:
 *
 *   with the reservation     A is answered 0 and B is answered 5.  B's
 *                            search skips A's PS_RSVD slot, finds nothing
 *                            free, and refuses cleanly without even
 *                            reaching the lock.  One MHELLO runs, with
 *                            A's command tail.
 *   without it               A is answered 0 and B is answered 0.  Both
 *                            picked the same slot and both built a
 *                            process in it, so one of the two children is
 *                            simply gone and its 64 KB page is leaked.
 *
 * TWO ZEROES ARE IMPOSSIBLE WITH ONE FREE DESCRIPTOR, so that is what the
 * target asserts, and no scheduling accident can produce it.
 */

#include "cpm.h"

#define	BDOS_CREATEPROC	144		/* XDOS create process		*/
#define	BDOS_PROCCNT	145		/* how many processes are live	*/
#define	BDOS_DELAY	141		/* XDOS delay, in ticks		*/

#define	TARGET		"CONCL.TXT"

/*  Long enough after the prompt is answered for both creators and the
    child to have reported.  This process holds a descriptor while it
    waits, which is harmless: the race is over.  */

#define	LINGER		600

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

/*  Create one CONCR in the given role, or say why not and fail the run:
    the arrangement has to be complete before the race can mean anything.  */

static int spawn(role)
char	role;
{
	register int	k;
	char		tail[4];

	tail[0] = ' ';
	tail[1] = role;
	tail[2] = 0;
	mkreq("CONCR.Z8K", tail);
	k = __bdos(BDOS_CREATEPROC, (long) &req) & 0xff;
	if (k != 0) {
		cputs("CONCL: FAIL -- cannot create CONCR ");
		lbuf[0] = role; lbuf[1] = ':'; lbuf[2] = ' '; lbuf[3] = 0;
		cputs(lbuf);
		cputs(k > 0 && k < 9 ? why[k] : "refused");
		cputs("\r\n");
		return (0);
	}
	return (1);
}


int main(argc, argv)
int argc;
char *argv[];
{
	register int	i, n;

	cputs("CONCL: start\r\n");

	numline("CONCL: live at start ", __bdos(BDOS_PROCCNT, 0L) & 0xff);

	/*  The ballast count is the caller's, because this program cannot
	    see PNPROC and the target that runs it can.  */
	n = 0;
	if (argc > 1)
		for (i = 0; argv[1][i] >= '0' && argv[1][i] <= '9'; i++)
			n = n * 10 + (argv[1][i] - '0');

	/*  The two creators first, while the lock is free.  */
	if (!spawn('A'))
		return (1);
	if (!spawn('B'))
		return (1);

	/*  Then the ballast, so that exactly one descriptor is left.  */
	for (i = 0; i < n; i++)
		if (!spawn('Z'))
			return (1);

	numline("CONCL: live before the race ", __bdos(BDOS_PROCCNT, 0L) & 0xff);

	/*  The file this process will park in the close() of.  */
	mkfcb(TARGET, &f);
	__bdos(BDOS_DELETE, (long) &f);
	mkfcb(TARGET, &f);
	if (__bdos(BDOS_MAKE, (long) &f) == 255) {
		cputs("CONCL: FAIL -- cannot create the target\r\n");
		return (1);
	}
	for (i = 0; i < SECLEN; i++)
		rec[i] = 'l';
	setdma(rec);
	if (__bdos(BDOS_WRITESEQ, (long) &f) != 0) {
		cputs("CONCL: FAIL -- write error\r\n");
		return (1);
	}

	/*  The read-only bit goes into the FCB now, AFTER the write, so
	    the only thing that can see it is close() -- src/cmd/conch.c
	    does the same for the same reason.  */
	f.ftype[0] |= 0x80;

	cputs("CONCL: closing -- the prompt below is UNDER the lock, and this"
	      " process is NOT a creator\r\n");
	__bdos(BDOS_CLOSE, (long) &f);

	cputs("CONCL: prompt answered, the lock is free again\r\n");
	__bdos(BDOS_DELAY, (long)LINGER);
	numline("CONCL: live at the end ", __bdos(BDOS_PROCCNT, 0L) & 0xff);

	/*  AND NOTHING WAS LEFT RESERVED.  A PS_RSVD slot that was claimed
	    and never given back is invisible to every other loop in
	    proc.c, so it would never be freed and never be re-used: the
	    only way to see one is to ask for a process and be refused 5
	    with descriptors plainly free.  By here A, B and the child have
	    all finished, so this must succeed.  */
	mkreq("CONCR.Z8K", " Z");
	i = __bdos(BDOS_CREATEPROC, (long) &req) & 0xff;
	numline("CONCL: a create after the race answered ", i);

	cputs("CONCL: done\r\n");
	return (0);
}
