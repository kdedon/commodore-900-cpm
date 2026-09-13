/*
 * dgenb.c -- F1(a): the peer that creates while DGENA holds a cached record.
 *
 * It does nothing until DGENA says its record is cached (flag 1), creates
 * one file, and then holds still until DGENA has finished looking (flag 3).
 * Holding still matters: if this process exited it would warm-boot, and the
 * reload would put directory traffic between DGENA's cache and its close,
 * which is exactly the thing under test.
 */

#include "cpm.h"

#define	BDOS_FLAGWAIT	132
#define	BDOS_FLAGSET	133

#define	FLGGO	1
#define	FLGDONE	2
#define	FLGEXIT	3

#define	PEERF	"F1NEW.TXT"

static struct fcb	f;

static char	lnbuf[80];
static int	lnlen;

static VOID lnadd(p)
register char *p;
{
	while (*p != 0 && lnlen < (int)(sizeof lnbuf) - 1)
		lnbuf[lnlen++] = *p++;
	lnbuf[lnlen] = 0;
}

static VOID lndec(n)
unsigned n;
{
	char	tmp[8];
	register int i;

	i = 0;
	do {
		tmp[i++] = (char)('0' + n % 10);
		n /= 10;
	} while (n != 0 && i < 7);
	while (i)
		if (lnlen < (int)(sizeof lnbuf) - 1)
			lnbuf[lnlen++] = tmp[--i];
		else
			i = 0;
	lnbuf[lnlen] = 0;
}

static VOID lnout()
{
	lnadd("\r\n");
	cputs(lnbuf);
	lnlen = 0;
	lnbuf[0] = 0;
}

int main(argc, argv)
int argc;
char *argv[];
{
	register int	k;

	cputs("DGENB: B alive\r\n");

	if (__bdos(BDOS_FLAGWAIT, (long) FLGGO) != 0) {
		cputs("DGENB: FAIL -- flag wait refused\r\n");
		return (1);
	}

	mkfcb(PEERF, &f);
	k = __bdos(BDOS_MAKE, (long) &f);
	lnadd("DGENB: made="); lndec((unsigned) k); lnout();
	__bdos(BDOS_CLOSE, (long) &f);

	__bdos(BDOS_FLAGSET, (long) FLGDONE);
	__bdos(BDOS_FLAGWAIT, (long) FLGEXIT);

	cputs("DGENB: B done\r\n");
	return (0);
}
