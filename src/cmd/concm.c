 * process to be the lock holder.  This program is the holder AND a
 * creator, which is as close as four descriptors got.
 *
 * THAT TEST NOW EXISTS ELSEWHERE.  PNPROC became 6 (F14), and
 * src/cmd/concl.c plus src/cmd/concr.c build the discriminating version --
 * a holder that creates nothing, two creators, ballast enough that exactly
 * one descriptor is free, and the release printed by a fourth process so
 * that neither racer's own output ends the window.  verify-concr2 FAILS
 * with the reservation removed; this target does not, and it stays exactly
 * what it honestly is: the regression that a creator parked inside
 * pcrgen() resumes with its own program and nothing deadlocks.
#define	BDOS_PROCCNT	145		/* how many processes are live	*/
	register int	i, k, nball;
	nball = 0;
	if (argc > 1)
		for (i = 0; argv[1][i] >= '0' && argv[1][i] <= '9'; i++)
			nball = nball * 10 + (argv[1][i] - '0');

	/*  BALLAST, so that exactly ONE descriptor is free when CONCO
	    parks with one picked -- which is what makes this program's own
	    request below have to be refused.  The count is the command
	    tail's, because this program cannot see PNPROC and the target
	    that runs it can: at PNPROC 4 it was zero and the arrangement
	    was implicit in the constant, which is exactly why raising
	    PNPROC to 6 broke this target (F14).  CONCR W is filler that
	    never prints anything until it is over, so it cannot disturb
	    the --input-mark this target releases its prompt on.  */
	for (i = 0; i < nball; i++) {
		mkreq("CONCR.Z8K", " W");
		k = __bdos(BDOS_CREATEPROC, (long) &req) & 0xff;
		if (k != 0) {
			cputs("CONCM: no ballast: ");
			cputs(k > 0 && k < 9 ? why[k] : "refused");
			cputs("\r\n");
			return (1);
		}
	}
	numline("CONCM: live before the race ", __bdos(BDOS_PROCCNT, 0L) & 0xff);

