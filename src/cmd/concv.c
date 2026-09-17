/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/*
 * concv.c - Measure scheduler quantum with a competing workload.
 */

#include "cpm.h"

#define	BDOS_CREATEPROC	144		/* XDOS create process		*/
#define	BDOS_PROCCNT	145		/* live-process count (ours)	*/
#define	BIOS_TICK	24		/* bios900.c case 24		*/

/*  How many slices to collect, plus the partial one that is thrown away.
    CONCY is 16 units, about 430 ticks with the machine to itself and so
    about 860 shared; 21 slices of five ticks is a fifth of that, which
    leaves the whole measurement inside the child's life with room to
    spare.  It is margin, not calibration -- the assertion is on the mode
    of the list, and one slice would be enough if the child could be
    relied on to still be there.  */

#define	NRUNS		21

/*  The bail-out, in ticks, and it is deliberately larger than the whole
    measurement: it exists so a run in which the child died early ends
    with a short list the target can complain about, rather than
    hanging.  */

#define	TICKCAP		600L

/*  HOW MUCH NORMAL-MODE WORK BETWEEN TWO CLOCK READS, AND WHY THERE HAS
    TO BE ANY.  This is the one thing about this program that had to be
    found by running it, and it is a fact about the machine rather than a
    calibration of the answer.

    The SC trap's FCW is 0xD000 (src/bios/crt.s psa+24): segmented System
    mode with VIE STILL SET.  So a tick that lands while this process is
    inside the SC #3 gate DOES run ttick_ -- and ttick_ declines it,
    because the frame is System mode and this process's supervisor stack
    is not empty (the gate has 36 bytes on it).  The handler then
    dismisses the interrupt, and that tick is spent without a dispatch.

    A loop that is nothing but `sc 3' is therefore inside the gate almost
    all the time and is almost never preempted: the first version of this
    program read the clock 601 times over 601 ticks and recorded ZERO
    switches, while CONCY's own count showed it had run alone for those
    601 ticks and only then got the machine.  That is a real property of
    the port -- a tight BIOS-gate loop is very nearly non-preemptible --
    and it is why the clock has to be read from Normal-mode code that is
    mostly NOT in the gate.

    SPIN long adds between reads puts the loop in Normal mode for the
    large majority of every tick, so the large majority of ticks are seen
    by ttick_ in Normal mode and dispatch.  It must also be SMALL enough
    that the loop still reads the clock several times per tick, or a tick
    value would be missed and one slice would be recorded as two.  Both
    bounds are wide and the number sits between them; the assertion is
    still an integer (the mode) against an integer (PQBASE), and
    src/cmd/concv.c prints the sampling rate it actually achieved so a
    reader can check that both bounds held.  */

#define	SPIN		150

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

static int	runs[NRUNS];		/* ticks this process ran for	*/
static int	gaps[NRUNS];		/* and ticks it was away for	*/

static char	lbuf[160];

/*  ONE cputs() PER LINE, for src/cmd/concs.c's reason: a process is
    switched away from at a BDOS gate, so the gap between two cputs()
    calls is where another process's line lands in the middle of this
    one's.  */

static char *putn(p, n)
char	*p;
long	n;
{
	register long	d;
	register int	seen;

	seen = 0;
	for (d = 100000L; d > 0L; d /= 10L)
		if (n / d != 0L || seen || d == 1L) {
			*p++ = '0' + (int)((n / d) % 10L);
			seen = 1;
		}
	return (p);
}

static VOID numline(tag, n)
char	*tag;
long	n;
{
	register char	*p;

	for (p = lbuf; *tag != 0; )
		*p++ = *tag++;
	p = putn(p, n);
	*p++ = '\r';
	*p++ = '\n';
	*p = 0;
	cputs(lbuf);
}

static VOID listline(tag, v, n)
char	*tag;
int	*v;
int	n;
{
	register char	*p;
	register int	i;

	for (p = lbuf; *tag != 0; )
		*p++ = *tag++;
	for (i = 0; i < n; i++) {
		if (i)
			*p++ = ' ';
		p = putn(p, (long)v[i]);
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
	register int	i, k, n;
	register long	w;
	int		runlen;
	long		t, last, t0, nread;

	cputs("CONCV: start\r\n");

	mkfcb("CONCY.Z8K", &req.pq_fcb);
	req.pq_tlen = 0;
	for (i = 0; i < 128; i++)
		req.pq_tail[i] = 0;

	k = __bdos(BDOS_CREATEPROC, (long) &req);
	if (k != 0) {
		cputs("CONCV: no second process: ");
		cputs(k > 0 && k < 9 ? why[k] : "refused");
		cputs("\r\n");
		return (1);
	}

	numline("CONCV: live=", (long)(__bdos(BDOS_PROCCNT, 0L) & 0xff));

	/*  From here to the prints below there is no BDOS call at all.  */
	t0 = last = __bios(BIOS_TICK, 0L, 0L);
	runlen = 1;
	n = 0;
	nread = 0L;
	w = 0L;
	while (n < NRUNS) {
		for (i = 0; i < SPIN; i++)	/* Normal mode, no gate */
			w += (long)i;
		nread++;
		t = __bios(BIOS_TICK, 0L, 0L);
		if (t == last)
			continue;
		if (t == last + 1L)
			runlen++;
		else {
			gaps[n]   = (int)(t - last);
			runs[n++] = runlen;
			runlen = 1;
		}
		last = t;
		if (t - t0 > TICKCAP)
			break;
	}

	/*  runs[0] is the partial slice this loop started in the middle
	    of; every later one begins at a resume.  */
	numline("CONCV: slices=", (long)(n > 0 ? n - 1 : 0));
	if (n > 1) {
		listline("CONCV: runs=", &runs[1], n - 1);
		listline("CONCV: gaps=", &gaps[1], n - 1);
	}
	/*  The sampling rate actually achieved, x100 so it is an integer:
	    it must be comfortably above 100 or a tick value could have
	    been missed and a slice recorded as two.  */
	numline("CONCV: reads per tick x100 = ",
		last > t0 ? (nread * 100L) / (last - t0) : 0L);
	numline("CONCV: V done, ticks=", last - t0);
	if (w == 0x7FFFFFFFL)		/* w is not dead code */
		cputs("CONCV: impossible\r\n");
	return (0);
}
