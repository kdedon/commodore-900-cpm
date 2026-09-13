/*
 */

#include "cpm.h"

#define	BDOS_SETUSER	32
#define	BDOS_CHAIN	47
#define	BDOS_SCB	49
#define	BDOS_WRANZF	40	/* write random with 0 fill		*/

#define	SCB_CCPFLGS	0x17

static struct fcb	f;
static char		buf[SECLEN];
static char		pb[4];
static int		checks;
static int		bad;

static VOID	check();
static VOID	report();
static int	scbget();

int main(argc, argv)
int argc;
char *argv[];
{
	register int	r;
	int		startuser;

	if (argc > 1 && argv[1][0] == 'P') {
		/* ---- phase 2: only reachable by fn 47's chain ---- */
		cputs("V3RET: phase 2 (chained by fn 47)\r\n");
		r = scbget(SCB_CCPFLGS) & 0xff;
		checks++;
		cputs("ccp$flgs (17) bit 40h -> ");
		putdec((unsigned) r);
		if (r & 0x40)
			cputs(" OK");
		else {
			cputs(" BAD -- fn 47 E=0FFh did not set it");
			bad++;
		}
		cputs("\r\n");

		/* fold phase 1's bad count (parked in fn 108) into ours;
		   phase 1 already printed its own tally, so only the
		   verdict needs to carry forward */
		bad += (int) (__bdos(BDOS_RETCODE, 0xffffL) & 0x7fff);
		report();
		return (bad != 0);
	}

	cputs("V3RET: BDOS function return-value shapes (V3)\r\n");

	/* ---- G7: fn 32, get/set user number ---- */
	startuser = __bdos(BDOS_SETUSER, 0xffL) & 0xff;
	__bdos(BDOS_SETUSER, 0L);
	check("fn 32 set 0, returns 0", __bdos(BDOS_SETUSER, 0L) & 0xffff, 0);
	check("fn 32 get after set 0", __bdos(BDOS_SETUSER, 0xffL) & 0xff, 0);
	check("fn 32 set 3, returns 0", __bdos(BDOS_SETUSER, 3L) & 0xffff, 0);
	check("fn 32 get after set 3", __bdos(BDOS_SETUSER, 0xffL) & 0xff, 3);
	/* out of range is masked to 4 bits (ani 0fh), not ignored */
	check("fn 32 set 0x14 (masked to 4), returns 0",
	      __bdos(BDOS_SETUSER, 0x14L) & 0xffff, 0);
	check("fn 32 get after set 0x14", __bdos(BDOS_SETUSER, 0xffL) & 0xff,
	      0x14 & 0x0f);
	__bdos(BDOS_SETUSER, 0L);	/* back to 0 for the rest of this run */

	/* ---- G10: fns 38/39, MP/M-only, func$ret -> 0 outside MP/M ---- */
	check("fn 38 (get/set process descr, MP/M-only)", __bdos(38, 0L), 0);
	check("fn 39 (get/set priority, MP/M-only)", __bdos(39, 0L), 0);

	/* ---- G9: fn 41, lret$eq$ff -> 00FFh ---- */
	check("fn 41 (outer environment, not a v3 call)", __bdos(41, 0L),
	      0x00ff);

	/* ---- G11: the default arm ----
	   51-97 and 113-127 -> 00FFh; 128 and up -> 0.  Pick numbers
	   this BDOS does not otherwise implement (55/90, 115/120,
	check("fn 55  (51-97 gap)", __bdos(55, 0L), 0x00ff);
	check("fn 90  (51-97 gap)", __bdos(90, 0L), 0x00ff);
	check("fn 115 (113-127 gap)", __bdos(115, 0L), 0x00ff);
	check("fn 120 (113-127 gap)", __bdos(120, 0L), 0x00ff);
	check("fn 200 (>=128, XDOS/MP/M)", __bdos(200, 0L), 0);

	/* ---- G5, unaffected: fn 27 stays the bad-function 0FFFFh ---- */
	check("fn 27 (won't-fix, structural) still 0FFFFh",
	      __bdos(27, 0L) & 0xffff, 0xffff);

	/* ---- G12: a file reached through the user-0 fallback is
	   read-only, 03FFh plus the console message, not a bare 3.
	   Built entirely on this run's own disk: create a SYS file in
	   user 0, switch to a nonzero user, and let the BDOS's own
	   search$user0 rule find it.				  */
	mkfcb("V3TMP.TXT", &f);
	setdma(buf);
	__bdos(BDOS_DELETE, (long) &f);	/* clean slate; ignore result */

	mkfcb("V3TMP.TXT", &f);
	r = __bdos(BDOS_MAKE, (long) &f) & 0xff;
	check("create V3TMP.TXT in user 0", (r == 0xff) ? 0xff : 0, 0);
	buf[0] = 'A';
	__bdos(BDOS_WRITESEQ, (long) &f);
	__bdos(BDOS_CLOSE, (long) &f);

	/* set$attr: SYS on, R/O left OFF, so the only reason the write
	   below fails is the user-0 fallback rule itself, not the
	   file's own read-only bit */
	mkfcb("V3TMP.TXT", &f);
	f.ftype[1] |= (char) 0x80;	/* OR the SYS bit onto the real
					   type character: match() masks
					   bit 7 but still compares the
					   other seven, so overwriting
					   the character loses the match */
	check("set SYS attribute on V3TMP.TXT",
	      __bdos(BDOS_SETATTR, (long) &f) & 0xff, 0);

	__bdos(BDOS_SETUSER, 5L);
	mkfcb("V3TMP.TXT", &f);
	r = __bdos(BDOS_OPEN, (long) &f) & 0xff;
	checks++;
	cputs("open V3TMP.TXT from user 5 -> ");
	putdec((unsigned) r);
	if (r == 0xff) {
		cputs(" BAD -- the user-0 fallback did not fire");
		bad++;
	} else
		cputs(" OK");
	cputs("\r\n");

	if (r != 0xff) {
		setdma(buf);
		f.cur_rec = 0;
		buf[0] = 'X';
		r = __bdos(BDOS_WRITESEQ, (long) &f) & 0xffff;
		checks++;
		cputs("write V3TMP.TXT via the fallback (fn 21) -> ");
		putdec((unsigned) r);
		if (r != 0x03ff) {
			cputs(" BAD -- want 03FF (error 3, set$aret)");
			bad++;
		} else
			cputs(" OK");
		cputs("\r\n");

		/* the fix is ONE code change shared by fns 21, 34 and 40
		   (bdosmain.c's hi_ext check is identical at all three
		   call sites), but until now only fn 21 was ever driven
		   through it -- fn 34 (random write) and fn 40 (random
		   write, 0 fill) share the open FCB and the same hi_ext
		   flag, set once by the OPEN above and never touched by
		   either random-record field, so no reselect is needed
		   between them */
		f.ran0 = 0;
		f.ran1 = 0;
		f.ran2 = 0;
		r = __bdos(BDOS_WRITERAN, (long) &f) & 0xffff;
		checks++;
		cputs("write V3TMP.TXT via the fallback (fn 34) -> ");
		putdec((unsigned) r);
		if (r != 0x03ff) {
			cputs(" BAD -- want 03FF (error 3, set$aret)");
			bad++;
		} else
			cputs(" OK");
		cputs("\r\n");

		r = __bdos(BDOS_WRANZF, (long) &f) & 0xffff;
		checks++;
		cputs("write V3TMP.TXT via the fallback (fn 40) -> ");
		putdec((unsigned) r);
		if (r != 0x03ff) {
			cputs(" BAD -- want 03FF (error 3, set$aret)");
			bad++;
		} else
			cputs(" OK");
		cputs("\r\n");

		__bdos(BDOS_CLOSE, (long) &f);
	}

	__bdos(BDOS_SETUSER, 0L);
	mkfcb("V3TMP.TXT", &f);
	__bdos(BDOS_DELETE, (long) &f);	/* clean up */
	__bdos(BDOS_SETUSER, (long) startuser);	/* leave the user number as
						   we found it		 */

	/* ---- G8: fn 47, E=0FFh sets bit 40h of ccp$flgs.  It always
	   ends the calling program (bdos30.asm:4665-4670,
	   bdosmain.c:47), so the only way to see the bit is from the
	   NEXT program -- chain to ourselves with "PHASE2" on the
	   command line.  Park this phase's bad count in fn 108 first,
	   since GBL.retcode -- unlike everything local to this
	   process -- survives the warm boot (bdosmisc.c:warmboot()
	   never touches it). */
	cputs("V3RET: phase 1 done, ");
	putdec((unsigned) checks);
	cputs(" checks, ");
	putdec((unsigned) bad);
	cputs(" bad; chaining to phase 2 via fn 47\r\n");
	__bdos(BDOS_RETCODE, (long) (bad & 0x7fff));

	/* the chain buffer replaces the WHOLE next console-read line
	   (conbdos.c readline()), so it has to be a complete command,
	   not just a tail: "V3RET PHASE2", 12 characters, no CR --
	   readline's own buffers never keep the terminating CR either */
	buf[0] = 12;			/* length byte		*/
	buf[1] = 'V';
	buf[2] = '3';
	buf[3] = 'R';
	buf[4] = 'E';
	buf[5] = 'T';
	buf[6] = ' ';
	buf[7] = 'P';
	buf[8] = 'H';
	buf[9] = 'A';
	buf[10] = 'S';
	buf[11] = 'E';
	buf[12] = '2';
	setdma(buf);
	__bdos(BDOS_CHAIN, 0xffL);		/* never returns */

	/* unreachable */
	report();
	return (bad != 0);
}

static VOID check(what, got, want)
char *what;
unsigned got;
unsigned want;
{
	checks++;
	cputs(what);
	cputs(" -> ");
	putdec(got);
	if (got == want)
		cputs(" OK");
	else {
		cputs(" BAD, wanted ");
		putdec(want);
		bad++;
	}
	cputs("\r\n");
}

static int scbget(off)
int off;
{
	pb[0] = off;
	pb[1] = 0;
	pb[2] = 0;
	pb[3] = 0;
	return (__bdos(BDOS_SCB, (long) pb));
}

static VOID report()
{
	cputs("V3RET: ");
	cputs(bad == 0 ? "PASS " : "FAIL ");
	putdec((unsigned) checks);
	cputs(" checks, ");
	putdec((unsigned) bad);
	cputs(" bad\r\n");
}
