/*
 * v3free.c - Verify function 46 little-endian free-space output and
 * function 31 DPB return values.
 */

#include "cpm.h"

#define BDOS_FREESP	46
#define BDOS_GETDPB	31
#define DRIVE_B		1L

/* struct dpb (src/bdos/bdosdef.h:254) as it lands in our buffer.  The
   words are the BDOS's own, so they are big-endian here. */
#define DPB_BSH		2
#define DPB_BLM		3
#define DPB_DSM_HI	6
#define DPB_DSM_LO	7

/* Drive A is CPMA_BLOCKS = 20480 blocks of 512 = 10,485,760 bytes; at
   BLS 4096 that is 2560 allocation blocks, so DSM = 2559 = 0x09FF. */
#define WANT_A_DSM	2559

/* 0x00FF40 records free on B: see the derivation in the file header. */
#define WANT_B0		0x40
#define WANT_B1		0xff
#define WANT_B2		0x00
#define WANT_B3		0x00

/* The DMA buffer is a whole record so that nothing beyond the four
   bytes can be blamed on us, and it is filled with a sentinel first so
   that a byte function 46 fails to write shows up as 0xAA rather than
   as a zero we cannot tell from a written zero. */
#define SENTINEL	0xaa

static char	dma[SECLEN];
static char	buf[SECLEN];
static char	dpbbuf[SECLEN];
static char	hexdig[] = "0123456789ABCDEF";
static int	checks;
static int	fails;

static VOID	puthex2();
static VOID	puthex4();

static VOID ckbyte(what, got, want)
char *what;
unsigned got;
unsigned want;
{
	checks++;
	cputs("\r\n");
	cputs(what);
	cputs(" -> ");
	puthex2(got);
	if (got == want)
		cputs(" OK");
	else {
		cputs(" BAD, wanted ");
		puthex2(want);
		fails++;
	}
}

main()
{
	int		i;
	long		recs;
	unsigned	ret;
	unsigned	dsm;

	cputs("V3FREE: function 46 wire form on B, function 31 return");

	for (i = 0; i < SECLEN; i++)
		dma[i] = SENTINEL;

	setdma(dma);
	__bdos(BDOS_FREESP, DRIVE_B);
	setdma(buf);

	/* The four bytes, each asserted on its own.  This is the test. */
	ckbyte("dma[0] (recs bits  0.. 7)", (unsigned) (dma[0] & 0xff),
		(unsigned) WANT_B0);
	ckbyte("dma[1] (recs bits  8..15)", (unsigned) (dma[1] & 0xff),
		(unsigned) WANT_B1);
	ckbyte("dma[2] (recs bits 16..23)", (unsigned) (dma[2] & 0xff),
		(unsigned) WANT_B2);
	ckbyte("dma[3] (must be zero)   ", (unsigned) (dma[3] & 0xff),
		(unsigned) WANT_B3);

	/* Function 46 writes FOUR bytes and no more: byte 4 of the DMA
	   buffer must still hold the sentinel. */
	ckbyte("dma[4] (untouched)      ", (unsigned) (dma[4] & 0xff),
		(unsigned) SENTINEL);

	/* And the count those bytes spell, printed so the transcript
	   carries the number as well as the bytes. */
	recs =	  ((long) (dma[0] & 0xff))
		| ((long) (dma[1] & 0xff) <<  8)
		| ((long) (dma[2] & 0xff) << 16);
	cputs("\r\nfree records on B = ");
	putdec((unsigned) recs);
	cputs(" (want 65344)");

	/* ---- function 31, Get DPB Address: a TESTABLE return value ----
	   v3 returns the DPB's address in HL.  We cannot hand out the
	   BDOS's own copy (it lives in a SYS segment this program cannot
	   address -- the same wall that drops fn 27), so we deliver the
	   DPB's contents to the caller's buffer and return THAT address.
	   It used to return 0, so a caller testing the answer concluded
	   the call had failed while the DPB sat in its own buffer.  Here
	   the returned value is checked against the pointer passed in,
	   and the DPB contents are checked too, so a return value that
	   happens to be right cannot stand in for data that is not. */

	for (i = 0; i < SECLEN; i++)
		dpbbuf[i] = SENTINEL;
	ret = __bdos(BDOS_GETDPB, (long) dpbbuf);

	checks++;
	cputs("\r\nfn 31 return             -> ");
	puthex4(ret);
	if (ret == 0) {
		cputs(" BAD, zero (untestable)");
		fails++;
	} else if (ret != (unsigned) ((long) dpbbuf & 0xffffL)) {
		cputs(" BAD, not the buffer address");
		fails++;
	} else
		cputs(" OK (the buffer it filled)");

	ckbyte("fn 31 dpb bsh (want 05)  ",
		(unsigned) (dpbbuf[DPB_BSH] & 0xff), 5);
	ckbyte("fn 31 dpb blm (want 1f)  ",
		(unsigned) (dpbbuf[DPB_BLM] & 0xff), 31);

	checks++;
	dsm = (unsigned) (((dpbbuf[DPB_DSM_HI] & 0xff) << 8)
			 | (dpbbuf[DPB_DSM_LO] & 0xff));
	cputs("\r\nfn 31 dpb dsm on A       -> ");
	puthex4(dsm);
	if (dsm == WANT_A_DSM)
		cputs(" OK (2559)");
	else {
		cputs(" BAD, wanted 09FF");
		fails++;
	}

	cputs("\r\nV3FREE: ");
	cputs(fails == 0 ? "PASS " : "FAIL ");
	putdec((unsigned) checks);
	cputs(" checks, ");
	putdec((unsigned) fails);
	cputs(" bad\r\n");
	return (fails != 0);
}


static VOID puthex2(n)
unsigned n;
{
	conout(hexdig[(n >> 4) & 0xf]);
	conout(hexdig[n & 0xf]);
}


static VOID puthex4(n)
unsigned n;
{
	int	i;

	for (i = 12; i >= 0; i -= 4)
		conout(hexdig[(n >> i) & 0xf]);
}
