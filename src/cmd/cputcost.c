/*
 * cputcost.c - Print COUNT asterisks in one cputs() call. Subtract equal-
 * length zero/count runs to isolate instruction cost; instruction counts
 * are not T-states.
 */

#include "cpm.h"

#define MAXCH	2000

static char buf[MAXCH + 1];

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

	if (argc != 2 || (n = getcount(argv[1])) < 0 || n > MAXCH) {
		printstr("usage: CPUTCOST nnnn (nnnn <= 2000)$");
		return (1);
	}
	for (i = 0; i < n; i++)
		buf[i] = '*';
	buf[n] = 0;
	cputs(buf);
	return (0);
}
