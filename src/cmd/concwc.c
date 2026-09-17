/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/*
 * concwc.c -- CONCWC.Z8K, the claimant.  The CCP runs it after CONCW's
 * warm boot; see src/cmd/concw.c for the shape of the test.
 *
 * It asks for every segment the pool will part with and prints them.  The
 * list is the verdict on pgrelall(): CONCW's segment must be in it (a warm
 * boot still reclaims the warm-booting program's own scratch) and CONCWB's
 * must not (CONCWB is still alive and still using it).
 */

#include "cpm.h"

#define	BDOS_FLAGSET	133
#define	FLAG_CDONE	2

#define	MAXSEG		8		/* more than the pool can hold	*/

static struct biospb	pb;

static long segcall(p1, p2)
long p1, p2;
{
	pb.code = BIOS_SEGMENT;
	pb.p1 = p1;
	pb.p2 = p2;
	return (__bdosl(BDOS_BIOSCALL, (long) &pb));
}

static char hexd[] = "0123456789ABCDEF";

static VOID phex2(v)
int v;
{
	conout(hexd[(v >> 4) & 0xf]);
	conout(hexd[v & 0xf]);
}

int main(argc, argv)
int argc;
char *argv[];
{
	register int	i, n, seg;

	cputs("CONCWC: got");
	n = 0;
	for (i = 0; i < MAXSEG; i++) {
		seg = (int) segcall(SEG_GET, 0L);
		if (seg == 0)
			break;
		conout(' ');
		phex2(seg);
		n++;
	}
	if (n == 0)
		cputs(" nothing");
	cputs("\r\n");

	/*  Release CONCWB, which has been waiting for exactly this.  The
	    segments taken above are not handed back: the warm boot at the
	    end of this program is what reclaims them, and that it does so
	    is the other half of what the target checks.  */
	__bdos(BDOS_FLAGSET, (long) FLAG_CDONE);
	return (0);
}
