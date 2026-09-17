/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
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
 *            calls it.  It does NOT return _main()'s value: pip.c's is
 *            declared VOID and stat.c's falls off its end, so there is no
 *            value in the return register to forward -- and since cstart.c
 *            now publishes what main() returns as the program return code
 *            (fn 108), forwarding the register would make both commands
 *            report a garbage status.  Their failures come out through
 *            _exit() below, which is the only exit either one has.
 *
 *   _setrc   cstart.c's one-liner that writes fn 108 (see its comment).
 *   __BDOS   DRI's bdos.h wants a long-returning BDOS gate; ours (__bdos,
 *            bdossc.s) returns int.  Every function pip.c calls through
 *            __BDOS fits in a word, so the extra width is never used.
 *   sbrk     a bump allocator over the basepage's free TPA span
 *            (_base->lbss + _base->bsslen .. + _base->freelen); pip.c
 *            uses it to grab its copy buffer.
 *   _exit    the same warm boot (BDOS fn 0) crt0.s issues when main()
 *            returns -- pip.c's error() path calls _exit(0) directly
 *            instead of returning -- WITH THE STATUS PUBLISHED FIRST.
 *            STAT and PIP both mean `_exit(1)' as "this command failed"
 *            (stat.c 407, 676, 706, 1573, 1653, 1892; pip.c:2061), and
 *            until _setrc() was called here that word went nowhere and
 *            CCP's `IF ERROR' tested whatever the previous command left.
 */

#include "cpm.h"

extern int _main();
extern VOID _setrc();

int main()
{
	_main();
	return (0);
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
	_setrc(status);
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
