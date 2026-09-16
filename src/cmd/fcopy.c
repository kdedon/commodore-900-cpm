/*
 * fcopy.c - copy a file record-by-record through the BDOS sequential
 * file calls: open/make/delete/read/write/close plus SETDMA, all via
 * the SC #2 shim.  Usage:  FCOPY SRC.TYP DST.TYP
 */

#include "cpm.h"

static struct fcb	src;
static struct fcb	dst;
static char		buf[SECLEN];

int main(argc, argv)
int argc;
char *argv[];
{
	register unsigned	n;

	if (argc != 3) {
		cputs("usage: fcopy src dst\r\n");
		return (1);
	}
	mkfcb(argv[1], &src);
	mkfcb(argv[2], &dst);

	if ((__bdos(BDOS_OPEN, (long) &src) & 0xff) == 0xff) {
		cputs("fcopy: cannot open ");
		cputs(argv[1]);
		cputs("\r\n");
		return (1);
	}
		return (1);
	}

	setdma(buf);
	n = 0;
	while (__bdos(BDOS_READSEQ, (long) &src) == 0) {
		n++;
	}
		return (1);
	}

	cputs("fcopy: copied ");
	putdec(n);
	cputs(" records\r\n");
	return (0);
}
