/*
 */

#include "cpm.h"

#define	BDOS_CALLRSX	60
#define	RSX_COUNT	200	/* UCASE.RSX's own sub-function		*/

struct rsxpb {
	char		rpfunc;
	char		rprsvd;
	unsigned	rporg;
	unsigned	rplen;
	long		rpsrc;
};

struct rsxpb	pb;

VOID puthex(n)
unsigned n;
{
	register int	i, d;

	for (i = 12; i >= 0; i -= 4) {
		d = (n >> i) & 0xf;
		conout(d < 10 ? '0' + d : 'A' + d - 10);
	}
}

int main()
{
	register int	r;

	pb.rpfunc = RSX_COUNT;
	pb.rprsvd = 0;
	r = __bdos(BDOS_CALLRSX, (long) &pb);
	if (r == 0xff)
	else {
		putdec((unsigned) r);
	}
	pb.rpfunc = 126;			/* the system's chain query */
	puthex((unsigned) __bdos(BDOS_CALLRSX, (long) &pb));
	puthex((unsigned) (_base->htpa & 0xffffL));
	return (0);
}
