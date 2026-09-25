/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/* 100 Hz tick from Z-CIO #1 counter/timer 3, continuous at PCLK/2.
 * Time constants are 20000 at 4 MHz and 30000 at 6 MHz, selected using
 * ROM rom_ctype. Programming and interrupt dismissal follow COHERENT
 * os/sys/z8001/src/md.s (clock setup and ISR).
 * trap.s owns the ISR; crt.s maps CTIV=0 to it. Vectored interrupts
 * remain enabled in user programs and system-call handlers.
 * RTC strobes use separate CIO registers, so tick interrupts do not
 * disturb their levels. Hardware tick rate still needs measurement. */
#include "stdio.h"
#include "romabi.h"

extern long	tickget();	/* trap.s: one LDL of tickcnt */

long	tickcnt;		/* bumped by trap.s ttick_, nothing else */
int	tickon;			/* nonzero once CT3 is running		 */
unsigned tickper;		/* the time constant actually programmed */

/*
 * TRUE once `dl' (a tickget() value plus a count of ticks) has passed.
 * FALSE forever if the tick is not running, so a caller's spin bound
 * stays the only bound in that case rather than the wait ending at once.
 * The subtraction, not a compare, so a counter wrap is not a stuck wait.
 */
int tickpast(dl)
long dl;
{
	return (tickon && (tickget() - dl) >= 0L);
}
