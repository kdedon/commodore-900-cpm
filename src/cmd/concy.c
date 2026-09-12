/*
 * concy.c - CPU workload used by scheduler and console polling tests.
 */

#include "cpm.h"

#define	BIOS_TICK	24		/* bios900.c case 24, read through
					   the raw SC #3 gate -- concx.c says
					   why it must not be BDOS fn 50	*/

/*  16 units against CONCX's 1.  Margin, not calibration: the claim is an
    ORDER, and the two schedulers put the two `done' lines in opposite
    orders (concx.c has the argument).  */

#define	CONCYUNITS	16

static char	pad[4096];

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
	register int	i;
	long		t0, t1;

	for (i = 0; i < sizeof pad; i++)
		pad[i] = (char)(i + 0x5b);

	t0 = __bios(BIOS_TICK, 0L, 0L);
	work(CONCYUNITS);
	t1 = __bios(BIOS_TICK, 0L, 0L);

	for (i = 0; i < sizeof pad; i++)
		if (pad[i] != (char)(i + 0x5b)) {
			printstr("CONCY: MEMORY CLOBBERED\r\n$");
			return (1);
		}
	doneline("CONCY: Y done, 4096 bytes of my own intact, ticks=", t1 - t0);
	return (0);
}
