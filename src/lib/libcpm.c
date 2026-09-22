/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/*
 * libcpm.c - thin C wrappers over the SC #2 BDOS gate (__bdos, bdossc.s).
 * Pointers pass as longs: a far pointer's value IS the XADDR the BDOS
 * wants.
 */

#include "cpm.h"

int conout(c)
int c;
{
	return (__bdos(BDOS_CONOUT, (long) c));
}

int conin()
{
	return (__bdos(BDOS_CONIN, 0L));
}

/* print a NUL-terminated string via BDOS fn 111 (print block to console):
   one BDOS call for the whole string instead of one per character, with
   the same per-character cookdout() handling (tab expansion, ^S/^Q,
   column tracking) that fn 2 gave conout() -- prt_blk() (conbdos.c)
   calls cookdout() in its own byte loop, exactly as DRI's FUNC111 calls
   TABOUT in a loop (CONBDOS.ASM:856-869), so this buys fewer gate
   crossings and nothing else. */
VOID cputs(s)
register char *s;
{
	register char	*p;
	struct ccb	blk;

	for (p = s; *p != 0; p++)
		;
	blk.cbaddr = (long) s;
	blk.cblen = (unsigned) (p - s);
	__bdos(BDOS_PRTBLK, (long) &blk);
}

/* print a NUL-terminated string the OLD way, one character at a time
   through BDOS fn 2.  Identical output to cputs(), and slower by exactly
   the gate crossings cputs() was changed to save -- so it exists for the
   one reason that difference is observable: a Resident System Extension
   hooks the BDOS entry, and fn 111 is served by prt_blk() INSIDE the
   BDOS, below the chain.  A module that folds fn 2 sees conputs() and
   cannot see cputs(), which is also true of DRI's own CP/M 3.  The RSX
   test rig and MHELLO's argument echo therefore speak fn 2
   deliberately; everything else in this tree wants cputs(). */
VOID conputs(s)
register char *s;
{
	while (*s != 0)
		conout(*s++);
}

/* print a '$'-terminated string via BDOS fn 9 */
VOID printstr(s)
char *s;
{
	__bdos(BDOS_PRINTSTR, (long) s);
}

VOID putdec(n)
unsigned n;
{
	if (n >= 10)
		putdec(n / 10);
	conout('0' + n % 10);
}

VOID setdma(p)
char *p;
{
	__bdos(BDOS_SETDMA, (long) p);
}

/*
 * Parse "NAME", "NAME.EXT" or "D:NAME.EXT" (already uppercased by the
 * CCP) into a fresh FCB.
 */
VOID mkfcb(name, f)
register char *name;
struct fcb *f;
{
	register char	*p;
	register int	i;

	p = (char *) f;
	for (i = 0; i < sizeof (struct fcb); i++)
		*p++ = 0;
	if (name[0] != 0 && name[1] == ':') {
		f->drvcode = name[0] - 'A' + 1;
		name += 2;
	}
	for (i = 0; i < 8; i++)
		f->fname[i] = ' ';
	for (i = 0; i < 3; i++)
		f->ftype[i] = ' ';
	for (i = 0; i < 8 && *name != 0 && *name != '.'; i++)
		f->fname[i] = *name++;
	while (*name != 0 && *name != '.')
		name++;
	if (*name == '.')
		name++;
	for (i = 0; i < 3 && *name != 0; i++)
		f->ftype[i] = *name++;
}
