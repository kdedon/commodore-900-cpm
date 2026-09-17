/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/*
 * conbrk.c - Exercise console break handling.
 */

#include "cpm.h"

#define CM_NOSTOP 0x0002	/* src/bdos/bdosdef.h; not exported via cpm.h */

static int getcount(s)
register char *s;
{
	register int	n, i;

	n = 0;
	for (i = 0; i < 4; i++) {
		if (s[i] < '0' || s[i] > '9')
			return (-1);
		n = n * 10 + (s[i] - '0');
	}
	return (s[4] == '\0' ? n : -1);
}

/* four hex digits -- function 108's return code is a UWORD, and
   RC_CTLC (0xfffe, src/bdos/bdosdef.h) needs all four to be unambiguous */
static VOID hex4(n)
unsigned n;
{
	static char digit[] = "0123456789ABCDEF";

	conout(digit[(n >> 12) & 0xf]);
	conout(digit[(n >> 8) & 0xf]);
	conout(digit[(n >> 4) & 0xf]);
	conout(digit[n & 0xf]);
}

/* one token: a 4-digit zero-padded counter and a trailing space.
   Strictly increasing and never repeats inside any run this program
   would plausibly be asked to do (10000 tokens), unlike a repeating
   digit or letter pattern -- a dropped or duplicated character shows
   up as a content mismatch instead of hiding behind periodicity. */
static VOID token(n)
unsigned n;
{
	conout('0' + (n / 1000) % 10);
	conout('0' + (n / 100) % 10);
	conout('0' + (n / 10) % 10);
	conout('0' + n % 10);
	conout(' ');
}

int main(argc, argv)
int argc;
char *argv[];
{
	register int	i, n;
	int		oldmode;

	if (argc < 2) {
		printstr("usage: CONBRK P nnnn | CONBRK R$");
		return (1);
	}

	switch (argv[1][0]) {
	case 'P':
		if (argc != 3 || (n = getcount(argv[2])) < 0) {
			printstr("usage: CONBRK P nnnn$");
			return (1);
		}
		oldmode = __bdos(BDOS_CONMODE, 0xffffL);
		__bdos(BDOS_CONMODE, (long) (oldmode | CM_NOSTOP));
		cputs("CONBRK-START\r\n");	/* conbrk()'s poll counter is
						   forced to zero here, not
						   counted -- CM_NOSTOP is
						   still set */
		__bdos(BDOS_CONMODE, (long) oldmode);
		for (i = 0; i < n; i++)
			token((unsigned) i);
		cputs("CONBRK-DONE\r\n");
		return (0);

	case 'R':
		cputs("CONBRK-RC=");
		hex4((unsigned) __bdos(BDOS_RETCODE, 0xffffL));
		cputs("\r\n");
		return (0);
	}
	printstr("usage: CONBRK P nnnn | CONBRK R$");
	return (1);
}
