/*
 * cstart.c - C-level startup for MWC-built transient programs.
 * Called by crt0.s after the BSS clear with the far base-page pointer;
 * records _base, splits the command tail into argc/argv (the CCP has
 */

#include "cpm.h"

struct bpage	*_base;

extern int	main();

#define	NARGV	16

static char	tail[SECLEN + 1];
static char	*argv[NARGV + 1];

int _cstart(bp)
struct bpage *bp;
{
	register char	*p;
	register int	argc, n, i;

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
}
