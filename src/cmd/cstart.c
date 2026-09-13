/*
 * cstart.c - C-level startup for MWC-built transient programs.
 * Called by crt0.s after the BSS clear with the far base-page pointer;
 * records _base, splits the command tail into argc/argv (the CCP has
 * already uppercased it), runs main, PUBLISHES ITS STATUS AS THE PROGRAM
 * RETURN CODE, and returns it (crt0 then warm-boots).
 *
 * THE RETURN CODE IS THE ABI, AND IT LIVES IN BDOS FUNCTION 108.  main's
 * value in r1 is not the program return code and never was: the CCP's
 * `IF ERROR' reads fn 108 (src/ccp/ccpext.c ccp_err()), the BDOS keeps it
 * in GBL.retcode (src/bdos/bdosdef.h), and warmboot() deliberately leaves
 * it alone so that it survives the transient's exit.  Until _setrc() below
 * existed nothing on the path from `return' to `sc 2' touched it, so every
 * C command warm-booted carrying whatever the PREVIOUS writer had left
 * there and `IF ERROR' tested a stale word.  One call here fixes it for
 * every C command in the release, which is why it is here and not in each
 * of them.
 */

#include "cpm.h"

struct bpage	*_base;

extern int	main();

#define	NARGV	16

static char	tail[SECLEN + 1];
static char	*argv[NARGV + 1];

/*  Set the program return code (fn 108) to a program's exit status.
 *
 *  0xFFFF is the parameter that READS the code back (BDOS_RETCODE with
 *  RC_GET, src/bdos/bdosmain.c:735), so it is the one value fn 108 cannot
 *  be asked to store.  A main() returning -1 means "failed", so report it
 *  as 1 rather than silently performing a read and leaving the old code in
 *  place -- the one case where this cannot pass the status through
 *  verbatim, and it still reports failure.
 *
 *  Shared with pipmain.c's _exit(), which is the other way out of a
 *  program built with this runtime (PIP and STAT).
 */

VOID _setrc(status)
int status;
{
	if (status == -1)
		status = 1;
	__bdos(BDOS_RETCODE, (long) (unsigned) status);
}

int _cstart(bp)
struct bpage *bp;
{
	register char	*p;
	register int	argc, n, i;
	int		rc;		/* main's status, published below	*/

	_base = bp;

	n = bp->buff[0] & 0x7f;		/* tail length byte, then text */
	for (i = 0; i < n; i++)
		tail[i] = bp->buff[i + 1];
	tail[n] = 0;

	argv[0] = "";
	argc = 1;
	p = tail;
	while (argc < NARGV) {
		while (*p == ' ' || *p == '\t')
			p++;
		if (*p == 0)
			break;
		argv[argc++] = p;
		while (*p != 0 && *p != ' ' && *p != '\t')
			p++;
		if (*p != 0)
			*p++ = 0;
	}
	argv[argc] = 0;
	rc = main(argc, argv);
	_setrc(rc);
	return (rc);
}
