/*
 */

#include "cpm.h"

#define	BDOS_SESSION	142		/* start a console session	*/
#define	BDOS_CREATEPROC	144		/* XDOS create process		*/
#define	BDOS_PROCCNT	145		/* how many processes are live	*/
#define	BDOS_DELAY	141		/* XDOS delay			*/
#define	BIOS_TICK	24		/* bios900.c case 24		*/

/*  The unit of computation: the same three loops concx.c and concy.c
    run, because the whole comparison rests on it being the same work.  */

#define	CONCZUNITS	1

/*  Ticks of quiet before t0, and the console the session goes on.  150
    is about a second and a half of emulated time against a session
    startup that is one program load; it is margin, not calibration, and
    the target's own numbers would show it up if it were short -- a
    session still loading during the timed loop would make the WITH run
    slower, which is the direction the target fails in.  */

#define	SETTLE		150
#define	SESSCON		1

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

/*  ONE cputs() PER LINE, for the reason src/cmd/xdosd.c's countline()
    gives at length: a process is switched away from at a BDOS gate, so
    the gap between two cputs() calls is a place another process's line
    can land in the middle of this one's.  A whole-line call has no such
    gap, because nothing switches a process that is inside the BDOS.  */

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


int main(argc, argv)
int argc;
char *argv[];
{
	register int	i, k;
	int		sess;
	long		t0, t1;

	sess = (argc > 1 && (argv[1][0] == 'S' || argv[1][0] == 's'));

	cputs(sess ? "CONCZ: start with an idle console\r\n"
		   : "CONCZ: start alone\r\n");

	if (sess) {
		k = __bdos(BDOS_SESSION, (long)SESSCON) & 0xff;
		if (k != 0) {
			cputs("CONCZ: no session: ");
			cputs(k > 0 && k < 9 ? why[k] : "refused");
			cputs("\r\n");
			return (1);
		}
		/*  Let it load its CCP, print its prompt and go to sleep
		    in conbdos.c getch().  This call blocks, so the
		    session has the machine to do it in.  */
		__bdos(BDOS_DELAY, (long)SETTLE);
	}

	mkfcb("CONCY.Z8K", &req.pq_fcb);
	req.pq_tlen = 0;
	for (i = 0; i < 128; i++)
		req.pq_tail[i] = 0;

	k = __bdos(BDOS_CREATEPROC, (long) &req);
	if (k != 0) {
		cputs("CONCZ: no second process: ");
		cputs(k > 0 && k < 9 ? why[k] : "refused");
		cputs("\r\n");
		return (1);
	}

	/*  HOW MANY PROCESSES THE TIMED LOOP IS SHARING THE MACHINE WITH,
	    which is the other half of the claim: 2 alone, 3 with the
	    session, and the target checks both.  A run that printed 2 in
	    the WITH case would be measuring nothing.  */
	numline("CONCZ: live=", (long)(__bdos(BDOS_PROCCNT, 0L) & 0xff));

	/*  From here to the print below there is no BDOS call at all.  */
	t0 = __bios(BIOS_TICK, 0L, 0L);
	work(CONCZUNITS);
	t1 = __bios(BIOS_TICK, 0L, 0L);

	numline("CONCZ: Z done, ticks=", t1 - t0);
	return (0);
}
