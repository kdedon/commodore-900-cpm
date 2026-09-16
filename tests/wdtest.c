/*
 * wdtest.c -- drive src/bios/wd900.c wdsec() on the host, with a
 * controller that never stops saying "busy, retry" (0x76).
 *
 * WHAT IS BEING TESTED.  wdsec() used to loop `for (;;)' on 0x76, and every
 * turn of that loop called wdgo900(), which starts a FRESH three-second
 * deadline.  A controller stuck in 0x76 therefore hung inside one BIOS read
 * for ever and no error ever reached the BDOS -- there was no status to
 * report because the function never returned.  The fix bounds the attempts
 * and returns the controller byte.
 *
 * The harness is the crsrtest.c pattern: compile the SAME source with its
 * hardware reached through overridable names.  A run that hangs is the old
 * behaviour, which is why verify-wdbusy runs this under `timeout'.
 *
 * Build: cc -std=gnu89 (wd900.c is K&R, like everything else in the port).
 */

#include <stdio.h>

#define ROMABI_H		/* romabi.h is a target header: keep it out */

/*  NOT `blk': wdsec()'s third parameter is named blk, and WDCB expands
    inside wdsec(), so a global of that name would be shadowed by a long.  */
static unsigned char wdblk[16];	/* the command block, on the host	*/
#define WDCB	((char *)wdblk)

static int goes;		/* how many times the controller was told
				   to go -- one per attempt		*/
static int answer = 0x76;	/* what it puts in the completion byte	*/

/* The controller.  Writing 1 to WDIO starts it; it answers at once, which
   is what makes the wedge this test is about a tight loop. */
static outw(port, v)
int port, v;
{
	if (v == 1) {
		goes++;
		wdblk[0x0c] = answer;
	}
	return (0);
}

static mapseg(seg, page, attr)
int seg, page, attr;
{
	return (0);
}

static long tickget()
{
	return (0L);
}

/* The deadline is irrelevant here: the controller answers immediately, so
   wdgo900()'s poll loop never reaches the clock.  Answer "expired" anyway,
   so that a future change that does consult it cannot spin. */
static int tickpast(dl)
long dl;
{
	return (1);
}

#include "../src/bios/wd900.c"

static int fails;

static check(cond, what)
int cond;
char *what;
{
	if (!cond) {
		printf("wdtest: FAIL -- %s\n", what);
		fails++;
	}
	return (cond);
}

int main()
{
	int st;

	wdinit900();

	goes = 0;
	st = wdsec(0x08, 100L, 0x00100000L);
	check(st == 0x76,
	      "a wedged controller must be reported, not retried for ever");
	check(goes > 1,
	      "0x76 must be retried at least once before it is believed");
	check(goes <= WDRETRY,
	      "the retries must be bounded by WDRETRY");

	/* A controller that answers properly must still be answered in one
	   attempt: the bound must not have turned success into a retry. */
	goes = 0;
	answer = 0x80;			/* "done" */
	st = wdsec(0x08, 100L, 0x00100000L);
	check(goes == 1, "a good transfer must take exactly one attempt");
	check(st == 0, "a completed transfer must report success");

	if (fails == 0)
		printf("wdtest: PASS -- %d attempts then status 0x76, and a good\n"
		       "        transfer still takes one\n", WDRETRY);
	return (fails != 0);
}
