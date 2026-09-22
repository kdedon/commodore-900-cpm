/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/*
 * xdma.c - a process's default DMA belongs to that process.
 *
 * Function 13 (reset disk system) puts the DMA address back to the running
 * program's own base page buffer, and the address it puts back is recorded
 * at load time (src/bdos/pgmld.c, proc.h pd_dma0).  The loader records it
 * in the RUNNING descriptor, which on a warm boot is the program's own --
 * but function 144 loads a child while the PARENT is running, so stock
 * wrote the child's address into the parent's descriptor and left the
 * child's whatever its recycled slot happened to hold (zero, on the first
 * spawn after a cold boot: a function 13 in the child then aimed the next
 * disk read at address zero).
 *
 * WHAT IS ASSERTED IS WHERE A DISK READ LANDS, not what a call returned --
 * there is no BDOS call that reports the DMA address, and a wrong one is
 * only visible in the bytes it moves.  Each role points the DMA somewhere
 * else on purpose (fn 26), calls fn 13, then reads one record of a known
 * file and says which buffer received it.
 *
 * Run with no argument it is the parent: it creates a second process
 * running this same program with the tail CHILD, then probes.  The CHILD
 * role is the discriminating one -- the parent's recorded address is the
 * child's base page, which for two copies of the SAME program is at the
 * same page offset, so the parent's probe passes either way and is here as
 * a guard rather than as the measurement.
 */

#include "cpm.h"

#define	BDOS_CREATEPROC	144		/* XDOS create process		*/
#define	BDOS_RESETDSK	13		/* reset disk system		*/
#define	BDOS_OPEN	15
#define	BDOS_READSEQ	20
#define	BDOS_SETDMA	26

#define	PROBEFILE	"HELLO.TXT"	/* staged on every test medium	*/

struct pcreq {
	struct fcb	pq_fcb;
	char		pq_tlen;
	char		pq_tail[128];
};

static struct pcreq	req;
static struct fcb	pf;

/*  Where the DMA is aimed before function 13, so that "function 13 did
    nothing at all" cannot pass for "function 13 was right".  */

static char		elsewhere[SECLEN];

static char *why[] = {
	"",
	"no such program",
	"not enough memory in the new page",
	"read error loading it",
	"program load error",
	"no free process descriptor",
	"no free 64 KB page -- this needs a 1 MB machine",
	"a second split-I/D program would need a second data bank"
};

/*  One line, one BDOS call: a switch happens at every BDOS call return, so
    a line built out of several calls comes back interleaved with the other
    process's (src/cmd/concf.c says the same thing at more length).  */

static char	lbuf[80];

static VOID say(a, b)
char *a, *b;
{
	register char	*p;

	p = lbuf;
	while (*a != 0)
		*p++ = *a++;
	while (*b != 0)
		*p++ = *b++;
	*p++ = '\r';
	*p++ = '\n';
	*p++ = '$';
	*p = 0;
	printstr(lbuf);
}

static int nonzero(p)
register char *p;
{
	register int i;

	for (i = 0; i < SECLEN; i++)
		if (p[i] != 0)
			return (1);
	return (0);
}

/*  Point the DMA away from the base page, reset the disk system, read one
    record, and answer where it went: 1 = our own base page buffer (right),
    0 = anywhere else, which includes "nowhere we can see".  */

static int probe()
{
	register int	i, k;

	for (i = 0; i < SECLEN; i++)
		elsewhere[i] = _base->buff[i] = 0;

	__bdos(BDOS_SETDMA, (long) elsewhere);
	__bdos(BDOS_RESETDSK, 0L);

	mkfcb(PROBEFILE, &pf);
	if ((k = __bdos(BDOS_OPEN, (long) &pf)) > 3)
		return (-1);
	if ((k = __bdos(BDOS_READSEQ, (long) &pf)) != 0)
		return (-1);

	if (nonzero(elsewhere))
		return (0);		/* fn 13 did not move it at all	*/
	return (nonzero(_base->buff) ? 1 : 0);
}

static VOID report(role)
char *role;
{
	register int	k;

	k = probe();
	if (k < 0)
		say(role, ": cannot read the probe file");
	else
		say(role, k ? ": fn13 DMA = own base page"
			    : ": fn13 DMA = NOT own base page");
}

int main(argc, argv)
int argc;
char *argv[];
{
	register int	i, k;
	int		child;

	/*  The role, BEFORE probe() clears the buffer the tail lives in.  */
	child = (argc > 1 && argv[1][0] == 'C');

	if (child) {
		report("XDMA: child");
		return (0);
	}

	say("XDMA: parent start", "");

	mkfcb("XDMA.Z8K", &req.pq_fcb);
	req.pq_tail[0] = 'C';
	req.pq_tail[1] = 'H';
	req.pq_tail[2] = 'I';
	req.pq_tail[3] = 'L';
	req.pq_tail[4] = 'D';
	req.pq_tlen = 5;
	for (i = 5; i < 128; i++)
		req.pq_tail[i] = 0;

	if ((k = __bdos(BDOS_CREATEPROC, (long) &req)) != 0) {
		say("XDMA: no second process: ",
		    k > 0 && k < 8 ? why[k] : "refused");
		return (1);
	}

	report("XDMA: parent");
	return (0);
}
