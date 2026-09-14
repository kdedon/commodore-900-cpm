/*
 */

#include "cpm.h"

#define	BDOS_SCB	49

#define	SCB_CONPAGE	0x1c
#define	SCB_CONLINE	0x1d
#define	SCB_PAGEMODE	0x2c
#define	SCB_PMDEFAULT	0x2d

#define	PM_ON		0x00
#define	PM_OFF		0xff

#define	PAGE		5	/* lines per page for this run		*/
#define	NLINES		12	/* lines printed: two full pages and a bit */

static char	pb[4];

static int scbget(off)
int off;
{
	pb[0] = off;
	pb[1] = 0;
	pb[2] = 0;
	pb[3] = 0;
	return (__bdos(BDOS_SCB, (long) pb));
}

static int scbsetb(off, val)
int off;
int val;
{
	pb[0] = off;
	pb[1] = 0xff;
	pb[2] = val;
	pb[3] = 0;
	return (__bdos(BDOS_SCB, (long) pb));
}

static VOID put2(n)
int n;
{
	conout('0' + (n / 10) % 10);
	conout('0' + n % 10);
}

static char	hexdig[] = "0123456789ABCDEF";

static VOID puthex2(n)		/* the SCB bytes read back, as bytes	*/
int n;
{
	conout(hexdig[(n >> 4) & 0xf]);
	conout(hexdig[n & 0xf]);
}

main()
{
	register char	*tail;
	register int	i;
	int		on;

	tail = &_base->buff[1];
	on = (_base->buff[0] != 0 && tail[0] == 'O' && tail[1] == 'N');

	cputs("PAGET: page=");
	put2(PAGE);
	cputs(" lines=");
	put2(NLINES);
	cputs(on ? " mode=ON\r\n" : " mode=OFF\r\n");

	scbsetb(SCB_CONPAGE, PAGE);
	scbsetb(SCB_PAGEMODE, on ? PM_ON : PM_OFF);
	scbsetb(SCB_CONLINE, 0);

	for (i = 1; i <= NLINES; i++) {
		cputs("PAGET LINE ");
		put2(i);
		cputs("\r\n");
	}

	/*  Paging off before anything else is printed, so that the
	    verdict below cannot itself pause and the count of prompts in
	    the transcript is exactly the count this loop caused.  */
	scbsetb(SCB_PAGEMODE, PM_OFF);

	cputs("PAGET: readback page=");
	puthex2(scbget(SCB_CONPAGE) & 0xff);
	cputs(" mode=");
	puthex2(scbget(SCB_PAGEMODE) & 0xff);
	cputs(" default=");
	puthex2(scbget(SCB_PMDEFAULT) & 0xff);
	cputs("\r\nPAGET: DONE\r\n");
	return (0);
}
