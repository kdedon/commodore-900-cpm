/*
 * concw.c -- CONCW.Z8K, the foreground half of the warm-boot ownership
 * test (first-release review P1 #10, src/bios/pgalloc.c pgrelall).
 *
 * THE CLAIM UNDER TEST.  pgrelall() runs on every warm boot and used to
 * release every allocated, unheld slot in the pool, because ownership was
 * a single Boolean.  A warm boot is the end of ONE program, though, and
 * the segments a BACKGROUND 8086 or Z80 interpreter is running out of are
 * unheld -- unheld means "not a parked 64 KB process image", which is true
 * of a guest's data group while the guest is still using it.  So a
 * foreground ^C handed a live interpreter's memory back to the pool, and
 * the pool gave it to the next program that asked.
 *
 * The three programs make that observable without any timing:
 *
 *   CONCW   (this one, the foreground)  allocates one segment, starts
 *           CONCWB, waits on flag 1 until CONCWB has allocated its own,
 *           and then EXITS -- which is the warm boot.
 *   CONCWB  (background)                allocates a segment, signs it,
 *           sets flag 1, waits on flag 2, then reads its signature back.
 *   CONCWC  (run by the CCP after the warm boot) allocates every segment
 *           the pool will give it and prints the list, then sets flag 2.
 *
 * CONCWC's list is the verdict.  This program's segment MUST be in it --
 * a warm boot still reclaims the warm-booting program's own scratch, and
 * a fix that leaked instead would be no better.  CONCWB's must NOT be.
 */

#include "cpm.h"

#define	BDOS_CREATEPROC	144
#define	BDOS_FLAGWAIT	132
#define	BDOS_FLAGSET	133

#define	FLAG_BREADY	1		/* CONCWB has its segment	*/

struct pcreq {
	struct fcb	pq_fcb;
	char		pq_tlen;
	char		pq_tail[128];
};

static struct pcreq	req;
static struct biospb	pb;

static long segcall(p1, p2)
long p1, p2;
{
	pb.code = BIOS_SEGMENT;
	pb.p1 = p1;
	pb.p2 = p2;
	return (__bdosl(BDOS_BIOSCALL, (long) &pb));
}

static char hexd[] = "0123456789ABCDEF";

static VOID phex2(v)
int v;
{
	conout(hexd[(v >> 4) & 0xf]);
	conout(hexd[v & 0xf]);
}

int main(argc, argv)
int argc;
char *argv[];
{
	register int	i, seg;

	seg = (int) segcall(SEG_GET, 0L);
	if (seg == 0) {
		cputs("CONCW: no segment -- this test needs more than 512 KB\r\n");
		return (1);
	}
	cputs("CONCW: A seg ");
	phex2(seg);
	cputs("\r\n");

	mkfcb("CONCWB.Z8K", &req.pq_fcb);
	req.pq_tlen = 0;
	for (i = 0; i < 128; i++)
		req.pq_tail[i] = 0;
	if (__bdos(BDOS_CREATEPROC, (long) &req) != 0) {
		cputs("CONCW: no second process\r\n");
		return (1);
	}

	/*  Not a delay: CONCWB sets this the instant it owns its segment,
	    so the warm boot below cannot happen first.  */
	if (__bdos(BDOS_FLAGWAIT, (long) FLAG_BREADY) != 0) {
		cputs("CONCW: flag wait refused\r\n");
		return (1);
	}

	cputs("CONCW: A exits, warm boot follows\r\n");
	return (0);		/* the warm boot, and pgrelall() with it */
}
