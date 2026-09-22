/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/*
 * concost.c - Measure console output cost by subtracting equal-length
 * zero/count runs. Keep banners out of the measured output to avoid
 * scrolling differences.
 */

#include "cpm.h"

#define ESC	033

#define NROW	25
#define NCOL	80

/* the row width used by R and E: column 79 is left alone, because a
   character there would wrap and turn a repaint into a scroll */
#define RWIDTH	(NCOL - 1)

/* ESC Y row+' ' col+' ' -- H19/VT52 direct cursor addressing */
static at(r, c)
int r, c;
{
	conout(ESC);
	conout('Y');
	conout(r + ' ');
	conout(c + ' ');
}

/*
 * Exactly four digits, no sign, no spaces: the caller is a measurement
 * harness rather than a person, and a lenient parser here would let a
 * mistyped count be measured instead of reported.
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
	register int	n, i, col, row;

	if (argc != 3 || (n = getcount(argv[2])) < 0) {
		printstr("usage: CONCOST P|R|E|B|N nnnn$");
		return (1);
	}

	/*
	 * The glyph is fixed at '*' rather than cycled through the
	 * alphabet.  The ROM renderer's cost per character does not
	 * depend on which character it is, and a fixed one keeps the
	 * screen a measurement rather than a page of text.
	 */
	switch (argv[1][0]) {
	case 'P':
		for (i = 0; i < n; i++)
			conout('*');
		return (0);

	case 'R':
		row = 0;
		col = 0;
		for (i = 0; i < n; i++) {
			if (col == 0)
				at(row, 0);
			conout('*');
			if (++col >= RWIDTH) {
				col = 0;
				if (++row >= NROW)
					row = 0;
			}
		}
		return (0);

	case 'E':
		row = 0;
		for (i = 0; i < n; i++) {
			at(row, 0);
			if (++row >= NROW)
				row = 0;
		}
		return (0);

	case 'B':
		for (i = 0; i < n; i++)
			__bdos(12, 0L);
		return (0);

	case 'N':
		/* The counter is volatile-by-accident: `col' is a register
		   the compiler cannot discard, so the loop survives.  An
		   empty loop it optimised away would report the call as
		   costing more than it does. */
		col = 0;
		for (i = 0; i < n; i++)
			col += i;
		row = col;
		return (0);
	}
	printstr("usage: CONCOST P|R|E|B|N nnnn$");
	return (1);
}
