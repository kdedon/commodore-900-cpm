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

extern int	inb();
extern		outb();
extern long	tickget();	/* trap.s: one LDL of tickcnt */

#define P_MICR		0x0001
#define P_CTIV		0x0009
#define P_CT3CS		0x0019
#define P_CT3TCH	0x0035
#define P_CT3TCL	0x0037
#define P_CT3MS		0x003d

#define MICR_MIE	0x80	/* master interrupt enable, NV/VIS clear	*/
#define CT3MS_TICK	0x80	/* continuous cycle; no external output,	*/
				/*   gate, trigger or count -- CT3's pins */
				/*   are IEEE-488/Centronics (snd900.c)	 */
#define CT3CS_CLIP	0x24	/* clear IP and IUS, gate open (md.s:693) */
#define CT3CS_IEGO	0xc6	/* set IE, gate open, trigger (md.s:420) */
#define CT3CS_GO	0x06	/* gate open, trigger		(md.s:428) */

#define TICKVEC		0	/* CTIV: "Vector #0 for clock" (md.s:416).
				   Entry 0 of the PSA's vectored table
				   (crt.s), which is where crt.s puts
				   ttick_ for exactly this reason.	 */

#define TC4MHZ		20000	/* md.s:57 CLKVAL0			*/
#define TC6MHZ		30000	/* md.s:58 CLKVAL1			*/

/* ROMCONF_PP holds a CPU far pointer ((seg<<24)|offset), not an laddr.
 * rom_ctype is byte 14 of the 15-byte ROM configuration block. */
#define ROMCONF_PP	0x01000000L	/* seg 1:0 -- the ROM's far ptr	*/
#define RC_CTYPE	14

long	tickcnt;		/* bumped by trap.s ttick_, nothing else */
int	tickon;			/* nonzero once CT3 is running		 */
unsigned tickper;		/* the time constant actually programmed */

/*
 * The time constant for a 100 Hz tick on this machine.  A ROM pointer
 * that does not name segment 1 is not the ROM's, so the 6 MHz value is
 * used rather than a byte read from somewhere unknown -- 6 MHz is what
 * the emulator models and the commoner machine.
 */
static unsigned tickconst()
{
	register long pp;
	register char *rc;

	pp = *(long *)ROMCONF_PP;
	if (((pp >> 24) & 0x7fL) != 1L)
		return (TC6MHZ);
	rc = (char *)pp;
	return (rc[RC_CTYPE] ? TC6MHZ : TC4MHZ);
}

/*
 * Start the tick.  Called from biosinit() once, at cold boot, after
 * rtcinit() (which owns port B and port C bit 1 of the same chip and
 * touches no counter).  The order is md.s:414-428's: mode, vector, time
 * constant, clear any stale IP, enable the counter's interrupt, enable
 * the chip's, start counting -- and only then let the CPU take it.
 */
tickinit()
{
	tickcnt = 0L;
	tickper = tickconst();

	outb(P_CT3MS, CT3MS_TICK);
	outb(P_CTIV, TICKVEC);
	outb(P_CT3TCH, (tickper >> 8) & 0xff);
	outb(P_CT3TCL, tickper & 0xff);
	outb(P_CT3CS, CT3CS_CLIP);
	outb(P_CT3CS, CT3CS_IEGO);
	outb(P_MICR, MICR_MIE);
	outb(P_CT3CS, CT3CS_GO);
	tickon = 1;
	tickei();		/* trap.s: EI VI -- last, deliberately */
}

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
