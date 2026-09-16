#include "romabi.h"
#include "biosdef.h"
#include "c900cfg.h"

extern ccpentry();		/* glue.s: reset the stack, enter the CCP */
extern int mem_cpy(), mem_clr();

/*
 * splitimg.o, generated from build/split.mod: the linked split-I/D
 * module's loaded image (text + initialized data, contiguous), the
 * offset of its bss within the segment, and that bss's length.
 */
extern char	splitimg[];
extern short	spimglen, spbssoff, spbsslen;

static spminit()
{
	mem_cpy((long)splitimg, SPLITMBASE, (long)spimglen);
	mem_clr(SPLITMBASE + (long)spbssoff, (long)spbsslen);
	if (*(short *)SPLITMBASE != SPM_MAGIC)
		puts("split-I/D module image bad -- 0xEE0B tools disabled\n");
}

#ifndef M12_HARNESS

cmain()
{
	binit();
	spminit();
	puts("\nCP/M-8000(tm) for the Commodore 900\n");
	ccpentry();		/* no return */
}

#else	/* M12_HARNESS */

static char dmabuf[128];		/* DMA target for reads */
static char wbuf[128];			/* DMA source for writes */
static char expbuf[128];		/* expected record content */

static long rdrecs[7] = { 0L, 5L, 1022L, 4003L, 40001L, 81917L, 81919L };
static long wrrecs[2] = { 123L, 81918L };

static puthex(v, n)
long v;
int n;
{
	static char hd[] = "0123456789abcdef";

	while (--n >= 0)
		putchar(hd[(int)(v >> (n * 4)) & 15]);
}

static putlng(v)
long v;
{
	if (v >= 10)
		putlng(v / 10);
	putchar('0' + (int)(v % 10));
}

/*
 * Expected content of logical record `rec' per tools/mksig.py: each
 * 128-byte record starts "CPMA", 32-bit record number, 32-bit complement,
 * then zeros.
 */
static mkexp(rec, bp)
long rec;
char *bp;
{
	register int i;
	register long c;

	for (i = 0; i < 128; i++)
		bp[i] = 0;
	bp[0] = 'C'; bp[1] = 'P'; bp[2] = 'M'; bp[3] = 'A';
	bp[4] = rec >> 24; bp[5] = rec >> 16; bp[6] = rec >> 8; bp[7] = rec;
	c = ~rec;
	bp[8] = c >> 24; bp[9] = c >> 16; bp[10] = c >> 8; bp[11] = c;
}

/*
 * Write-test pattern for record `rec': bytes (lo(rec)+hi(rec)+i) & 0xff.
 * host-side verification recomputes the same bytes.
 */
static mkpat(rec, bp)
long rec;
char *bp;
{
	register int i, b;

	b = (int)(rec & 0xff) + (int)((rec >> 8) & 0xff);
	for (i = 0; i < 128; i++)
		bp[i] = b + i;
}

static rdrec(rec, bp)
long rec;
char *bp;
{
	bsettrk(rec >> 6);		/* 64 records per track */
	bsetsec(rec & 63L);
	bsetdma((long)bp);
	return (bread());
}

static cmprec(a, b)
char *a, *b;
{
	register int i;

	for (i = 0; i < 128; i++)
		if (a[i] != b[i])
			return (0);
	return (1);
}

static rdtest()
{
	register int i, ok, npass;

	npass = 0;
	for (i = 0; i < 7; i++) {
		puts("read rec ");
		putlng(rdrecs[i]);
		ok = (rdrec(rdrecs[i], dmabuf) == 0);
		if (ok) {
			mkexp(rdrecs[i], expbuf);
			ok = cmprec(dmabuf, expbuf);
		}
		puts(ok ? ": PASS\n" : ": FAIL\n");
		if (ok)
			npass++;
	}
	puts("read test: ");
	putlng((long)npass);
	puts("/7 passed\n");
}

static wrtest()
{
	register int i, ok, npass, st;

	for (i = 0; i < 2; i++) {
		mkpat(wrrecs[i], wbuf);
		bsettrk(wrrecs[i] >> 6);
		bsetsec(wrrecs[i] & 63L);
		bsetdma((long)wbuf);
		st = bwrite(0);
		puts("write rec ");
		putlng(wrrecs[i]);
		puts(st ? ": ERROR\n" : ": ok\n");
	}
	bflush();
	rdrec(40000L, dmabuf);		/* evict: fetch a far-away sector */
	npass = 0;
	for (i = 0; i < 2; i++) {
		ok = (rdrec(wrrecs[i], dmabuf) == 0);
		if (ok) {
			mkpat(wrrecs[i], expbuf);
			ok = cmprec(dmabuf, expbuf);
		}
		puts("verify rec ");
		putlng(wrrecs[i]);
		puts(ok ? ": PASS\n" : ": FAIL\n");
		if (ok)
			npass++;
	}
	puts("write test: ");
	putlng((long)npass);
	puts("/2 passed\n");
}

cmain()
{
	register int c, llen;
	char line[8];
	long dph;

	binit();
	puts("\nCP/M-8000(tm) for the Commodore 900 -- C900 BIOS M1/M2\n");
	dph = bseldsk(0, 0);
	puts("seldsk(0) dph=0x");
	puthex(dph, 8);
	putchar('\n');
	llen = 0;
	puts("cpm> ");
	for (;;) {
		while (bconstat() == 0)
			;
		c = bconin();
		if (c == '\r') {
			if (llen == 1 && line[0] == 'r') {
				putchar('\n');
				rdtest();
			} else if (llen == 1 && line[0] == 'w') {
				putchar('\n');
				wrtest();
			}
			llen = 0;
			puts("\ncpm> ");
		} else {
			bconout(c);
			if (llen < 7)
				line[llen++] = c;
		}
	}
}

#endif	/* M12_HARNESS */
