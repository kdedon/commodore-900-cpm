/*
 * splitb.c - Attempt a SECOND split-I/D program while one is running, and
 *	      leave the first one's WORK behind as the evidence.
 *
 * THE SPLIT-I/D BANKS ARE SHARED.  A 0xEE0B program's code runs in its
 * process's own 64 KB page, but its data address space is one fixed
 * physical bank (src/bios/c900cfg.h SPLITDSEG) and its instruction patch
 * table is one more (SPLITTSEG).  Neither moves with a page swap, which is
 * why at most one split program may be live and why function 144 answers
 * PC_SPLIT (7) to the second.
 *
 * THE REFUSAL USED TO COME TOO LATE.  src/bdos/proc.c pcrgen() asked
 * "is one already live?" only after ldimage() had returned -- by which
 * time the loader had put the second program's data image in the shared
 * bank and rebuilt the side table from its text.  The caller got a
 * correct-looking 7 and the program already running had had its data
 * replaced underneath it.
 *
 * SO THE RETURN CODE IS NOT THE OBJECT: it was 7 before the fix and it is
 * 7 after it.  What differs is whether the first split program's job comes
 * out right.  This program therefore:
 *
 *   1. creates ASZ8K.Z8K (0xEE0B, on the release disk) as a background
 *      process, assembling STARTUP.8KN -- seven kilobytes of source, which
 *      is a job long enough to be interrupted in the middle of;
 *   2. waits long enough for the assembler to be properly under way, with
 *      a symbol table and a source position in bank 0x35 -- two loads of
 *      the same file back to back would write the same initial bytes and
 *      show nothing;
 *   3. with argument 2, asks for a second split program.  With argument 1
 *      it does not, which is the control run: the same assembly, nobody
 *      interfering;
 *   4. either way, stays alive until the assembler is gone, by watching
 *      function 145's count.  That is not politeness: the run ends when
 *      the machine goes idle, and a CCP prompt with a background process
 *      working is idle enough to end it -- an assembly that was cut off
 *      halfway in BOTH runs would leave two identical truncated files and
 *      the comparison below would prove nothing.
 *
 * tests/verify.mk verify-split runs both and compares STARTUP.OBJ byte for
 * byte.  The assembler's output is the object; this program's transcript
 * only says that the attempt happened and what it was told.
 */

#include "cpm.h"

#define	BDOS_CREATEPROC	144		/* XDOS create process		*/
#define	BDOS_PROCCNT	145		/* how many are live		*/
#define	BDOS_DELAY	141		/* XDOS delay, in ticks		*/

/*  Ticks before the second attempt.  The assembler has to be past its own
    start-up and into the source file; 200 is about two seconds of emulated
    time against an assembly that runs for many, so it is margin rather
    than calibration.  If it were short the two runs would still have to
    match -- an interference that lands before the assembler has any state
    is an interference that proves nothing, and the target says so by
    checking that the assembly ran for longer than this.  */

#define	SETTLE		200

/*  Watching the assembler finish: a tick count per look and a bound on
    the number of looks, so a job that never ends fails this program
    rather than the emulator's cycle budget.  */

#define	WATCH		20
#define	WATCHMAX	20000

/*  The function 144 parameter block: src/bdos/proc.c struct pcreq.  */

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

/*  One line, one BDOS call: a process is switched away from at the gate,
    so two calls per line let another process's line land inside this one
    (src/cmd/concz.c says it at length).  */

static VOID numline(tag, n)
char	*tag;
int	n;
{
	register char	*p;
	register int	d, seen;

	for (p = lbuf; *tag != 0; )
		*p++ = *tag++;
	seen = 0;
	for (d = 10000; d > 0; d /= 10)
		if (n / d != 0 || seen || d == 1) {
			*p++ = '0' + (n / d) % 10;
			seen = 1;
		}
	*p++ = '\r';
	*p++ = '\n';
	*p = 0;
	cputs(lbuf);
}

/*  Fill req with a program name and a command tail.  The tail is what a
    child gets in its base page; the two parsed base-page FCBs are the
    loader's and pcrgen() zeroes them, so a child reads its arguments from
    the tail, which is where a C start-up looks for them anyway.  */

static VOID mkreq(name, tail)
char	*name, *tail;
{
	register int	i;
	register char	*p;

	mkfcb(name, &req.pq_fcb);
	for (i = 0; i < 128; i++)
		req.pq_tail[i] = 0;
	i = 0;
	for (p = tail; *p != 0; p++)
		req.pq_tail[i++] = *p;
	req.pq_tlen = (char)i;
}


int main(argc, argv)
int argc;
char *argv[];
{
	register int	k;
	int		second, base, n;

	second = (argc > 1 && argv[1][0] == '2');
	base = __bdos(BDOS_PROCCNT, 0L) & 0xff;

	cputs(second ? "SPLITB: interference run\r\n"
		     : "SPLITB: control run\r\n");

	/*  The split program whose work is the object.  */
	mkreq("ASZ8K.Z8K", " STARTUP.8KN");
	k = __bdos(BDOS_CREATEPROC, (long) &req) & 0xff;
	if (k != 0) {
		cputs("SPLITB: no assembler process: ");
		cputs(k > 0 && k < 9 ? why[k] : "refused");
		cputs("\r\n");
		return (1);
	}
	numline("SPLITB: assembling, live=",
		__bdos(BDOS_PROCCNT, 0L) & 0xff);

	/*  Let it get somewhere.  This call BLOCKS, so the assembler has
	    the machine for all of it.  */
	__bdos(BDOS_DELAY, (long)SETTLE);

	if (!second)
		cputs("SPLITB: control, no second request\r\n");
	else {
		/*  The second split program.  SIZEZ8K.Z8K is 0xEE0B too,
		    and it prints and exits, so if it ever IS created --
		    which is the defect, not the fix -- it does no further
		    damage of its own.  */
		mkreq("SIZEZ8K.Z8K", " MHELLO.Z8K");
		k = __bdos(BDOS_CREATEPROC, (long) &req) & 0xff;
		numline("SPLITB: second split request answered ", k);
		if (k == 7)
			cputs("SPLITB: refused 7, as it must be\r\n");
		else if (k == 0)
			cputs("SPLITB: CREATED -- a second split program is"
			      " live and the banks are shared\r\n");
		else {
			cputs("SPLITB: refused for another reason: ");
			cputs(k > 0 && k < 9 ? why[k] : "unknown");
			cputs("\r\n");
		}
	}

	/*  Wait the assembler out, so the run does not end at a prompt
	    while it is still writing its object file.  */
	for (n = 0; n < WATCHMAX; n++) {
		if ((__bdos(BDOS_PROCCNT, 0L) & 0xff) <= base)
			break;
		__bdos(BDOS_DELAY, (long)WATCH);
	}
	if (n >= WATCHMAX) {
		cputs("SPLITB: the assembler never finished\r\n");
		return (1);
	}
	numline("SPLITB: assembler gone, live=",
		__bdos(BDOS_PROCCNT, 0L) & 0xff);
	return (0);
}
