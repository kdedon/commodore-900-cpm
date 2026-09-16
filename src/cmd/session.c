/*
 * session.c - Console session workload for ownership tests.
 */

#include "cpm.h"

#define	BDOS_SESSION	142		/* start a console session	*/

static char *why[] = {
	"",
	"no CCP.Z8K to run",
	"not enough memory in the new page",
	"read error loading the CCP",
	"CCP load error",
	"no free process descriptor",
	"no free 64 KB page -- this needs a 1 MB machine",
	"a second split-I/D program would need a second data bank",
	"no such console"
};

static char upbuf[32];

int main(argc, argv)
int argc;
char *argv[];
{
	register int	n, k;

	n = 1;
	if (argc > 1)
		n = argv[1][0] - '0';

	k = __bdos(BDOS_SESSION, (long)n) & 0xff;
	if (k != 0) {
		cputs("SESSION: refused -- ");
		cputs(k > 0 && k < 9 ? why[k] : "no");
		cputs("\r\n");
		return (1);
	}

	upbuf[0] = 'S'; upbuf[1] = 'E'; upbuf[2] = 'S'; upbuf[3] = 'S';
	upbuf[4] = 'I'; upbuf[5] = 'O'; upbuf[6] = 'N'; upbuf[7] = ':';
	upbuf[8] = ' '; upbuf[9] = 'c'; upbuf[10] = 'o'; upbuf[11] = 'n';
	upbuf[12] = 's'; upbuf[13] = 'o'; upbuf[14] = 'l'; upbuf[15] = 'e';
	upbuf[16] = ' ';
	upbuf[17] = (char)('0' + (n & 15));
	upbuf[18] = ' '; upbuf[19] = 'u'; upbuf[20] = 'p';
	upbuf[21] = '\r'; upbuf[22] = '\n'; upbuf[23] = '$';
	upbuf[24] = 0;
	printstr(upbuf);
	return (0);
}
