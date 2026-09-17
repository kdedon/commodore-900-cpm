/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/*
 * concx.c - Exercise process scheduling with a competing workload.
 */

#include "cpm.h"

#define	BDOS_CREATEPROC	144		/* XDOS create process	*/
#define	BIOS_TICK	24		/* bios900.c case 24	*/

/*  The unit of computation, shared with concy.c by being the same code
    with a different repeat count.  220*200 long adds is concq.c's `gap',
    which is a known quantity in this tree: about a million instructions,
    and the emulator's tick lands roughly every 15,500 (S1's measurement,
    CONCURRENT-EXTENSIONS-PLAN.md 7.2), so one unit is scores of ticks.  */

#define	CONCXUNITS	1

static long	spin;

static VOID work(units)
int units;
{
	register int	u, i, j;

	for (u = 0; u < units; u++)
		for (i = 0; i < 220; i++)
			for (j = 0; j < 200; j++)
				spin += (long)j;
}

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
	"a second split-I/D program would need a second data bank"
};

static char lbuf[64];

static VOID doneline(tag, n)
char	*tag;
long	n;
{
	register char	*p;
	register long	d;
	register int	seen;

	for (p = lbuf; *tag != 0; )
		*p++ = *tag++;
	seen = 0;
	for (d = 100000L; d > 0L; d /= 10L) {
		if (n / d != 0L || seen || d == 1L) {
			*p++ = '0' + (int)((n / d) % 10L);
			seen = 1;
		}
	}
	*p++ = '\r';
	*p++ = '\n';
	*p++ = '$';
	*p = 0;
	printstr(lbuf);
}


int main(argc, argv)
int argc;
char *argv[];
{
	register int	i, k;
	long		t0, t1;

	printstr("CONCX: X start\r\n$");

	mkfcb("CONCY.Z8K", &req.pq_fcb);
	req.pq_tlen = 0;
	for (i = 0; i < 128; i++)
		req.pq_tail[i] = 0;

	k = __bdos(BDOS_CREATEPROC, (long) &req);
	if (k != 0) {
		cputs("CONCX: no second process: ");
		cputs(k > 0 && k < 8 ? why[k] : "refused");
		cputs("\r\n");
		return (1);
	}

	/*  From here to the print below there is no BDOS call at all.  */
	t0 = __bios(BIOS_TICK, 0L, 0L);
	work(CONCXUNITS);
	t1 = __bios(BIOS_TICK, 0L, 0L);

	doneline("CONCX: X done, ticks=", t1 - t0);
	return (0);
}
