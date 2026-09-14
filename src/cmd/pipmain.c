/*
 * pipmain.c - compatibility shims that let src/cmd/pip.c (DRI's PIP,
 * built from source per vendor/SOURCES) link against this port's own
 * runtime (cstart.c, bdossc.s, libcpm.c) instead of DRI's own devpack
 * runtime (vendor/z8001mb/.../libcpm.a, a different object format this
 * host toolchain does not read).
 *
 * Supplies exactly what pip.c calls that no other src/cmd program needed:
 *
 *   main()   our cstart.c calls "main"; DRI's own crt0/startup.s called
 *            "_main" directly.  pip.c defines _main(), so main() just
 *   __BDOS   DRI's bdos.h wants a long-returning BDOS gate; ours (__bdos,
 *            bdossc.s) returns int.  Every function pip.c calls through
 *            __BDOS fits in a word, so the extra width is never used.
 *   sbrk     a bump allocator over the basepage's free TPA span
 *            (_base->lbss + _base->bsslen .. + _base->freelen); pip.c
 *            uses it to grab its copy buffer.
 *   _exit    the same warm boot (BDOS fn 0) crt0.s issues when main()
 *            returns -- pip.c's error() path calls _exit(0) directly
 */

#include "cpm.h"

extern int _main();

int main()
{
}

long __BDOS(func, param)
int func;
long param;
{
	return ((long) __bdos(func, param));
}

VOID _exit(status)
int status;
{
	__bdos(0, 0L);
	for (;;)
		;
}

static char *brk;

char *sbrk(incr)
int incr;
{
	char *base, *lim, *old;

	base = (char *) (_base->lbss + _base->bsslen);
	lim  = base + _base->freelen;
	if (brk == 0)
		brk = base;
	if (incr == 0)
		return (brk);
	if (brk + incr > lim || brk + incr < base)
		return ((char *) -1);
	old = brk;
	brk += incr;
	return (old);
}
