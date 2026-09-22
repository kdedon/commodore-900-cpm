/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/* Z80 target launcher. Usage: Z80 PROG.COM [tail...].
 * Allocate a 64 KB guest segment and a separate staging segment so the
 * loader can clear guest memory before copying the image into it. */

#include "cpm.h"
#include "z80.h"

static struct z80	G;
static struct fcb	f;

static int	gseg;			/* the guest's 64 KB		*/
static char	*gmem;			/* its base, as a far pointer	*/

/* ------------------------------------------------------------------ */
/* the BIOS function 25 calls, through BDOS function 50		       */

static struct biospb pb;

static long segcall(p1, p2)
long p1, p2;
{
	pb.code = BIOS_SEGMENT;
	pb.p1 = p1;
	pb.p2 = p2;
	return (__bdosl(BDOS_BIOSCALL, (long) &pb));
}

/* ------------------------------------------------------------------ */
/* the one function the seam does not contain			       */

/*
 * z80sys -- the native BDOS, reached the way every other transient
 * program reaches it.  `addr' non-null means the function's parameter is
 * that address; otherwise it is the 16-bit value.  The host tests
 * substitute a recorder or a stub CP/M for this.
 */
int z80sys(fn, val, addr)
int fn;
z16 val;
char *addr;
{
	return (__bdos(fn, addr ? (long) addr : (long) (val & 0xffff)));
}

/* ------------------------------------------------------------------ */
/* small output helpers -- libcpm.c has cputs/putdec and nothing hex   */

static char hexd[] = "0123456789ABCDEF";

static VOID phex2(v)
int v;
{
	conout(hexd[(v >> 4) & 15]);
	conout(hexd[v & 15]);
}

static VOID phex4(v)
int v;
{
	phex2((v >> 8) & 0xff);
	phex2(v & 0xff);
}

/*
 * putdec() takes an unsigned, and an instruction count does not fit in
 * one -- PIP's is 15,763 today but the limit below is twenty million.
 * The count is compared exactly against the host's, so `15k
 * instructions' would not tell a matching path from one that diverged.
 */
static VOID pdecl(v)
long v;
{
	char b[12];
	register int n;

	if (v == 0) {
		conout('0');
		return;
	}
	n = 0;
	while (v > 0 && n < 11) {
		b[n++] = (char) ('0' + (int) (v % 10L));
		v /= 10L;
	}
	while (n > 0)
		conout(b[--n]);
}

/* One line of the guest's own memory, read back through its segment. */
static VOID pdump(off, n)
int off, n;
{
	register int i;

	phex4(off);
	conout(':');
	for (i = 0; i < n; i++) {
		conout(' ');
		phex2(gmem[off + i] & 0xff);
	}
	cputs("\r\n");
}

/* ------------------------------------------------------------------ */

static char *xname(rc)
int rc;
{
	switch (rc) {
	case X_OK:	return ("running");
	case X_UNIMP:	return ("an instruction stage one does not implement");
	case X_BAD:	return ("not an instruction");
	case X_HOOK:	return ("an unserviced hook");
	case X_HALT:	return ("HLT");
	}
	return ("?");
}

/*
 * Read PROG.COM into `stage', which is a whole segment, and answer its
 * length -- or -1, having said why.  Sequential reads of 128-byte
 * records, which is what a .COM is: it has no header and no structure.
 */
static long comread(name, stage)
char *name, *stage;
{
	register long n;

	mkfcb(name, &f);
	if ((__bdos(BDOS_OPEN, (long) &f) & 0xff) == 0xff) {
		cputs("z80: cannot open ");
		cputs(name);
		cputs("\r\n");
		return (-1L);
	}
	/*
	 * Straight into the staging segment: the BDOS's DMA address is
	 * an XADDR and it deblocks by copy, so it will read a record
	 * into any segment that is mapped -- which this one now is.
	 * That is a second, quieter proof that the descriptor is real,
	 * and it is why there is no 128-byte bounce buffer here.
	 */
	n = 0;
	while (n + SECLEN <= 0x10000L) {
		setdma(stage + n);
		if (__bdos(BDOS_READSEQ, (long) &f) != 0)
			break;
		n += SECLEN;
	}
	setdma((char *) &_base->buff[0]);	/* back where the CCP had it */
	__bdos(BDOS_CLOSE, (long) &f);
	if (n == 0) {
		cputs("z80: ");
		cputs(name);
		cputs(" is empty\r\n");
		return (-1L);
	}
	return (n);
}

int main(argc, argv)
int argc;
char *argv[];
{
	struct z80in in;
	register int i;
	long n, k, limit, xa;
	int sseg, rc, brc;
	char *stage;
	static char tail[SECLEN];

	if (argc < 2) {
		cputs("usage: z80 prog.com [tail...]\r\n");
		return (1);
	}

	/*
	 * The gate's first line, and the one the whole task was about.
	 * A machine with no spare memory answers 0 here, which is the
	 * standard 512 KB machine and is not a failure of anything: the
	 * pages 0x0A-0x0F are spent on the TPA, the split-I/D banks, the
	 * disk buffers and the C stack, and there is no eleventh page.
	 */
	n = segcall(SEG_COUNT, 0L);
	cputs("z80: free segments: ");
	putdec((unsigned) n);
	cputs("\r\n");

	gseg = (int) segcall(SEG_GET, 0L);
	if (gseg == 0) {
		cputs("z80: no segment for the guest -- this machine has "
		      "no memory above the system's own\r\n");
		return (1);
	}
	sseg = (int) segcall(SEG_GET, 0L);
	if (sseg == 0) {
		cputs("z80: no second segment to stage the image in\r\n");
		segcall(SEG_PUT, (long) gseg);
		return (1);
	}
	/*
	 * A segment number becomes an address here and in one other
	 * place, and it goes through a LONG variable rather than a cast
	 * of the shift expression: cc1 takes an internal error on
	 * `(char *)((long)(s) << 24)' written inline.
	 */
	xa = SEGBASE(gseg);
	gmem = (char *) xa;
	xa = SEGBASE(sseg);
	stage = (char *) xa;
	cputs("z80: guest segment ");
	phex2(gseg);
	cputs(", staging segment ");
	phex2(sseg);
	cputs("\r\n");

	if ((n = comread(argv[1], stage)) < 0) {
		segcall(SEG_PUT, (long) sseg);
		segcall(SEG_PUT, (long) gseg);
		return (1);
	}

	rc = z80load(&G, gmem, stage, n);
	segcall(SEG_PUT, (long) sseg);		/* wanted for the read only */
	cputs("z80: load: ");
	cputs(z80lerr(rc));
	cputs("\r\n");
	if (rc != CL_OK) {
		segcall(SEG_PUT, (long) gseg);
		return (1);
	}

	/* The tail, rebuilt from what the CCP split for us. */
	tail[0] = '\0';
	k = 0;
	for (i = 2; i < argc; i++) {
		tail[k++] = ' ';
		for (n = 0; argv[i][n] && k < SECLEN - 1; n++)
			tail[k++] = argv[i][n];
	}
	tail[k] = '\0';
	z80tail(&G, tail);

	/*
	 * The evidence.  Every byte below was written by z80load() into
	 * the segment BIOS function 25 handed out, and is being read
	 * back out of it from Normal mode: 0x0000 is the warm-boot JMP,
	 * 0x0005 the BDOS entry JMP, 0x005C and 0x006C the two parsed
	 * FCBs, 0x0080 the tail, 0x0100 the first bytes of the program
	 * itself.  A segment that was not mapped, or was mapped System
	 * only, does not get this far -- it faults.
	 */
	cputs("z80: page zero and the furniture, out of the guest:\r\n");
	pdump(PZ_WBOOT, 8);
	pdump(PZ_FCB1, 16);
	pdump(PZ_FCB2, 16);
	pdump(PZ_DMA, 16);
	pdump(COM_ORG, 16);

	z80ninsn = z80nflag = 0;
	z80bdosinit(&G);

	limit = 20000000L;
	brc = B_RUN;
	rc = X_OK;
	for (k = 0; k < limit; k++) {
		rc = z80step(&G, &in);
		if (rc == X_OK)
			continue;
		if (rc != X_HOOK)
			break;
		brc = z80bdos(&G);
		if (brc == B_RUN)
			continue;
		break;
	}
	/* A loop that ended on a bad instruction, a HALT or the step
	 * limit never re-entered the seam, so the last partial line of
	 * the guest's output is still sitting in the batch. */
	z80oflush();
	z80bdosfini();

	cputs("\r\nz80: ");
	pdecl(k);
	cputs(" instructions, ");
	pdecl((long) z80nbdos);
	cputs(" BDOS calls, ");
	pdecl((long) z80nflag);
	cputs(" flag materialisations\r\n");
	if (brc == B_EXIT)
		cputs("z80: the guest terminated\r\n");
	else {
		cputs("z80: stopped: ");
		cputs(xname(rc));
		cputs(", seam says ");
		cputs(z80berr());
		cputs(", pc ");
		phex4((int) G.pc);
		cputs("\r\n");
	}
	segcall(SEG_PUT, (long) gseg);
	return (brc == B_EXIT ? 0 : 1);
}
