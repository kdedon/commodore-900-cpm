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
 *	       from bdosinit, every warmboot and pcreate.
 */
#include "stdio.h"		/* DRI types layer (sys/stdio.h) */

EXTERN UWORD	xbdos();	/* BDOS dispatcher (sys/bdosmain.c) */

UWORD bdos(func, parm)
WORD func;
LONG parm;
{
	return (xbdos(func, (UWORD)parm, parm));
}

/* The BIOS trap-vector table is NOT touched here any more.  It is per
 * process (bios900.c xvec), and a program's recorded vectors die with the
 * program through xvclr() in proc.c procdead() -- all 48 of them, where
 * the M20's initexc reclaimed only 2-23 and 36-47 and left SC #0's vector
 * to outlive the debugger that set it.  This routine is also called from
 * pcreate(), while the running row is still the PARENT's, and clearing it
 * there would take a debugger's breakpoints away from it. */
initexc(vecp)
UBYTE **vecp;
{
	REG WORD i;

	for (i = 0; i < 18; i++)
		vecp[i] = (UBYTE *)0;
}
