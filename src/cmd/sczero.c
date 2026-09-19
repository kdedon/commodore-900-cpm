/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/*
 * sczero.c - Issue SC #0, the trap DDT.Z8K plants as a breakpoint, from a
 * program that recorded no handler for it.  The kernel must report the
 * trap and warm boot (bios900.c panic): the line after the SC never
 * prints.  Run after a DDT session it shows that DDT's handler did not
 * outlive DDT (src/bdos/proc.c procdead, xvclr).
 */

#include "cpm.h"

extern VOID	sczero();	/* sczerosc.s */

int main()
{
	cputs("SCZERO: issuing SC #0\r\n");
	sczero();
	cputs("SCZERO: SC #0 RETURNED\r\n");
	return (0);
}
