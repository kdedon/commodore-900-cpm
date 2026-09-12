/*
 * conn.c - Exercise concurrent console ownership.
 */

#include "cpm.h"

#define	BDOS_SETCON	148		/* XDOS Set Console		*/
#define	BIOS_CONCNT	29		/* bios900.c case 29		*/
#define	NPROBE		8		/* 148 takes a low nibble; this is
					   past any table it will ever have */

static int	blk[5];			/* fn 50 {code,P1seg,P1off,P2seg,P2off} */
static char	line[24];

/*  BIOS function 29 through BDOS function 50, the way DATE.Z8K reaches
    the clock (src/cmd/date.c biostime).  bioscl() answers 0xffffffff for
    a refused code, so a byte of 0xff is "this system does not have the
    call" and not a console count.  */
static int biosncon()
{
	blk[0] = BIOS_CONCNT;
	blk[1] = 0;
	blk[2] = 0;
	blk[3] = 0;
	blk[4] = 0;
	return ((int)(__bdos(50, (long)blk) & 0xff));
}

static say(tag, n)
char *tag;
int n;
{
	register int	i;

	i = 0;
	while (*tag != 0)
		line[i++] = *tag++;
	if (n > 9) {
		line[i++] = (char)('0' + n / 10);
		n %= 10;
	}
	line[i++] = (char)('0' + n);
	line[i++] = '\r';
	line[i++] = '\n';
	line[i++] = '$';
	line[i] = 0;
	printstr(line);
}

int main(argc, argv)
int argc;
char *argv[];
{
	register int	i;
	int		n, m;

	n = biosncon();
	m = 0;
	for (i = 1; i < NPROBE; i++) {
		if ((__bdos(BDOS_SETCON, (long)i) & 0xff) != 0)
			continue;	/* refused: that console is not there */
		__bdos(BDOS_SETCON, 0L);	/* back before anything prints */
		m = i;
	}

	if (n == 0xff) {
		printstr("CONN: FAIL -- BIOS function 29 refused\r\n$");
		return (1);
	}
	say("CONN: ncon=", n);
	say("CONN: max148=", m);
	if (m + 1 != n) {
		printstr("CONN: FAIL -- the BDOS and the BIOS disagree\r\n$");
		return (1);
	}
	printstr("CONN: agreed\r\n$");
	return (0);
}
