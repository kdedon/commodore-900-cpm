/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/*
 * seam.c -- the D3 call-gating seam between the co-linked CCP/BDOS and
 * the BIOS half.
 *
 * bdos()   -- the user-facing BDOS entry the CCP, go.c and pgmld.c call:
 *	       a plain C call into the BDOS dispatcher xbdos().  Loaded
 *	       programs reach the dispatcher through the SC #2 trap gate
 *	       instead.
 * initexc() -- (re)initialize the 18-entry exception-vector array; called
 *	       from bdosinit and every warmboot.
 */
#include "stdio.h"		/* DRI types layer (sys/stdio.h) */

EXTERN UWORD	xbdos();	/* BDOS dispatcher (sys/bdosmain.c) */

UWORD bdos(func, parm)
WORD func;
LONG parm;
{
	return (xbdos(func, (UWORD)parm, parm));
}

extern long xvec[];		/* BIOS trap-vector table (bios900.c) */

initexc(vecp)
UBYTE **vecp;
{
	REG WORD i;

	for (i = 0; i < 18; i++)
		vecp[i] = (UBYTE *)0;
	/* Clear the BDOS-managed range of the BIOS trap-vector table
	 * (the M20's initexc re-pointed vectors 2-23 and 36-47 at its
	 * own handler each boot, so program-recorded vectors died with
	 * the program; recording a zero has the same effect here --
	 * 0-1 and 24-35 stay with the BIOS, exceptn.z8k contract). */
	for (i = 2; i < 24; i++)
		xvec[i] = 0L;
	for (i = 36; i < 48; i++)
		xvec[i] = 0L;
}
