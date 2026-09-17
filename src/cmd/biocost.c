/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/*
 * biocost.c - Measure per-character cost through BDOS (A), BIOS (S), ROM
 * (R), or video RAM (V). Compare equal-length zero/count runs to subtract
 * load and prompt overhead.
 */

#include "cpm.h"

/*
 * Exactly four digits, no sign, no spaces -- see concost.c's getcount(),
 * which this is a copy of: the caller is a measurement harness, and a
 * lenient parser here would let a mistyped count be measured instead of
 * reported.
 */
static getcount(s)
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

int main(argc, argv)
int argc;
char *argv[];
{
	register int	n, i;

	if (argc != 3 || (n = getcount(argv[2])) < 0) {
		printstr("usage: BIOCOST A|S|R|V nnnn$");
		return (1);
	}

	switch (argv[1][0]) {
	case 'A':
		for (i = 0; i < n; i++)
			__bdos(BDOS_CONOUT, (long) '*');
		return (0);

	case 'S':
		for (i = 0; i < n; i++)
			__bios(BIOS_CONOUT, (long) '*', 0L);
		return (0);

	case 'R':
		for (i = 0; i < n; i++)
			__bios(BIOS_ROMCHAR, (long) '*', 0L);
		return (0);

	case 'V':
		for (i = 0; i < n; i++)
			__bios(BIOS_VSETCHAR, (long) '*', 0L);
		return (0);
	}
	printstr("usage: BIOCOST A|S|R|V nnnn$");
	return (1);
}
