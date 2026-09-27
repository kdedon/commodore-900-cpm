/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/*
 * ccpct.c - Stress the CCP reload after a program ends.
 *   CCPCT F  fill the TPA from the end of bss to just below the stack
 *   CCPCT W  overwrite record 0 of A:CCP.Z8K through the raw BIOS, so no
 *            directory entry changes; the next reload must see the damage
 */

#include "cpm.h"

static struct fcb	f;
static char		rec[SECLEN];

static struct dpb {
	unsigned spt;
	char	bsh, blm, exm, dpbdum;
	unsigned dsm, drm, dir_al, cks, trk_off;
} dpb;

static long biosc(code, p1, p2)
int code;
long p1, p2;
{
	struct biospb	pb;

	pb.code = code;
	pb.p1 = p1;
	pb.p2 = p2;
	return (__bdosl(BDOS_BIOSCALL, (long) &pb));
}

int main(argc, argv)
int argc;
char *argv[];
{
	register char	*p, *e;
	register int	i;
	unsigned	n;
	char		here;

	if (argc > 1 && argv[1][0] == 'F') {
		p = (char *) (_base->lbss + _base->bsslen);
		e = &here - 64;
		for (i = 0; p < e; i++)
			*p++ = (char) (0xE5 ^ i);
		cputs("CCPCT: filled\r\n");
		return (0);
	}
	if (argc > 1 && argv[1][0] == 'W') {
		__bdos(32, 0);
		mkfcb("A:CCP.Z8K", &f);
		if (__bdos(BDOS_OPEN, &f) > 3) {
			cputs("CCPCT: no CCP.Z8K\r\n");
			return (1);
		}
		__bdos(BDOS_GETDPB, (long) &dpb);
		n = f.dskmap[0] & 0xff;
		if (dpb.dsm > 255)
			n |= (f.dskmap[1] & 0xff) << 8;
		n <<= dpb.bsh;
		for (i = 0; i < SECLEN; i++)
			rec[i] = (char) 0xFF;
		biosc(BIOS_SELDSK, 0L, 1L);
		biosc(BIOS_SETTRK, (long) (dpb.trk_off + n / dpb.spt), 0L);
		biosc(BIOS_SETSEC, biosc(BIOS_SECTRAN, (long) (n % dpb.spt),
		      0L), 0L);
		biosc(BIOS_SETDMA, (long) rec, 0L);
		if (biosc(BIOS_WRITE, 0L, 0L) != 0L) {
			cputs("CCPCT: write failed\r\n");
			return (1);
		}
		cputs("CCPCT: wrote\r\n");
		return (0);
	}
	cputs("CCPCT: F or W\r\n");
	return (1);
}
