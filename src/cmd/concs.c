/*
 */

#include "cpm.h"

#define	BDOS_CREATEPROC	144		/* XDOS create process		*/
#define	BDOS_PROCCNT	145		/* live-process count (ours)	*/
#define	BIOS_TICK	24		/* bios900.c case 24		*/

/*  How long the System-mode loop is: SYSUNITS times 65536 register
    decrements.  Two constraints, and neither is a calibration:

      it must be long enough to hold several ticks, or a "was it
      preempted" question has no instants to be answered at;
      it must be SHORTER than the child's 16 units, or the tail of the
      measured run would be spent alone and the ratio would sag.

    24 measures at about 200 ticks against CONCY's 430 alone, so the
    child is still running when the loop ends with a wide margin either
    side.  */

#define	SYSUNITS	24

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
	"a second split-I/D program would need a second data bank",
	"no such console"
};

static char lbuf[80];

extern int	sysspin();		/* src/cmd/sysmode.s		*/

/*  ONE cputs() PER LINE, for src/cmd/xdosd.c countline()'s reason: a
    process is switched away from at a BDOS gate, so the gap between two
    cputs() calls is where another process's line lands in the middle of
    this one's.  Nothing switches a process that is inside the BDOS.  */

static VOID numline(tag, n)
char	*tag;
long	n;
{
	register char	*p;
	register long	d;
	register int	seen;

	for (p = lbuf; *tag != 0; )
		*p++ = *tag++;
	seen = 0;
	for (d = 100000L; d > 0L; d /= 10L)
		if (n / d != 0L || seen || d == 1L) {
			*p++ = '0' + (int)((n / d) % 10L);
			seen = 1;
		}
	*p++ = '\r';
	*p++ = '\n';
	*p = 0;
	cputs(lbuf);
}

static VOID hexline(tag, n)
char	*tag;
int	n;
{
	static char	digits[] = "0123456789ABCDEF";
	register char	*p;
	register int	i;

	for (p = lbuf; *tag != 0; )
		*p++ = *tag++;
	for (i = 12; i >= 0; i -= 4)
		*p++ = digits[(n >> i) & 0xf];
	*p++ = '\r';
	*p++ = '\n';
	*p = 0;
	cputs(lbuf);
}


int main(argc, argv)
int argc;
char *argv[];
{
	register int	i, k;
	int		both, fcw;
	long		t0, t1;

	both = (argc > 1 && (argv[1][0] == 'C' || argv[1][0] == 'c'));

	cputs(both ? "CONCS: start with a second job\r\n"
		   : "CONCS: start alone\r\n");

	if (both) {
		mkfcb("CONCY.Z8K", &req.pq_fcb);
		req.pq_tlen = 0;
		for (i = 0; i < 128; i++)
			req.pq_tail[i] = 0;

		k = __bdos(BDOS_CREATEPROC, (long) &req);
		if (k != 0) {
			cputs("CONCS: no second process: ");
			cputs(k > 0 && k < 9 ? why[k] : "refused");
			cputs("\r\n");
			return (1);
		}
	}

	/*  How many processes the timed loop is sharing the machine with:
	    1 alone, 2 with the child.  The target checks both before it
	    compares any ticks, because a WITH run that printed 1 would be
	    a comparison of nothing with nothing.  */
	numline("CONCS: live=", (long)(__bdos(BDOS_PROCCNT, 0L) & 0xff));

	/*  From here to the print below there is no BDOS call except the
	    function 62 inside sysspin() itself.  */
	t0 = __bios(BIOS_TICK, 0L, 0L);
	fcw = sysspin(SYSUNITS);
	t1 = __bios(BIOS_TICK, 0L, 0L);

	hexline("CONCS: sysfcw=", fcw);
	numline("CONCS: S done, ticks=", t1 - t0);
	return (0);
}
