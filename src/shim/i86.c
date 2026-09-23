/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/* CPM86 target launcher: allocate group and staging segments, load a .CMD,
 * build its base page, and execute it through the native BDOS bridge.
 * Usage: CPM86 PROG.CMD [tail...]. Staging is separate because destination
 * segments must be cleared before copying initialized group data. */

#include "cpm.h"
#include "i86.h"

static struct i86	G;
static struct i86cmd	C;
static struct fcb	f;

/* Each guest group owns one segment; one additional segment stages the
 * input file. gseg[i] matches i86spar[i]/i86sbase[i] and group sidx. */
#define I86MAXG	6			/* 7 pool - 1 staging		*/

/* The staging segment is not released after the load: it becomes the
 * guest's paragraph 0, the low 64 KB that holds the interrupt vector
 * table.  So it is held for the whole run and freed with the rest, and
 * a program that declares the six groups this machine can place still
 * gets them -- the seventh segment was always acquired, and now it is
 * kept rather than handed back. */
static int	gseg[I86MAXG + 1];	/* the segment numbers held	*/
static int	ngseg;			/* how many of them		*/
static int	cseg, dseg;		/* the guest's first two 64 KB	*/
static char	*cmem, *dmem;

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
 * i86sys -- the native BDOS, reached the way every other transient
 * program reaches it.  `addr' non-null means the function's parameter
 * is that address -- a far pointer into a guest segment, or to the
 * seam's own copy of an FCB.  Otherwise the parameter
 * is the byte or word value.
 */
int i86sys(fn, val, addr)
int fn;
i16 val;
char *addr;
{
	return (__bdos(fn, addr ? (long) addr : (long) (val & 0xffff)));
}

/*
 * The spare segments BDOS function 59 draws on when a guest loads a
 * program of its own.  They come from the same pool as the guest's, are
 * held apart from gseg[] because they come and go while the guest runs,
 * and putsegs() gives back whatever is still out at the end.
 *
 * Four, because the pool is seven: a run holds one per declared group,
 * of which there are at least two, plus the segment that became
 * paragraph 0.  Five spare at the very best, and a .CMD with more than
 * four groups is refused by i86place() before this is asked.
 */
#define I86XSEG	4

static int	xsegn[I86XSEG];
static char	*xsegb[I86XSEG];
static int	nxseg;

static char *xsegget()
{
	long xa;
	int s;

	if (nxseg >= I86XSEG)
		return ((char *) 0);
	s = (int) segcall(SEG_GET, 0L);
	if (s == 0)
		return ((char *) 0);
	xa = SEGBASE(s);
	xsegn[nxseg] = s;
	xsegb[nxseg] = (char *) xa;
	return (xsegb[nxseg++]);
}

static int xsegput(b)
char *b;
{
	register int i;

	for (i = 0; i < nxseg; i++)
		if (xsegb[i] == b) {
			segcall(SEG_PUT, (long) xsegn[i]);
			xsegn[i] = xsegn[nxseg - 1];
			xsegb[i] = xsegb[nxseg - 1];
			nxseg--;
			return (1);
		}
	return (0);
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

/* putdec() takes an unsigned; instruction counts do not fit in one. */
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
static VOID pdump(p, off, n)
char *p;
int off, n;
{
	register int i;

	phex4(off);
	conout(':');
	for (i = 0; i < n; i++) {
		conout(' ');
		phex2(p[off + i] & 0xff);
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
	case X_INT:	return ("an unserviced interrupt");
	case X_HALT:	return ("HLT");
	case X_SEGESC:	return ("a segment value we never handed out");
	case X_WINDOW:	return ("a reference past the end of a segment");
	case X_WBOOT:	return ("a warm boot");
	}
	return ("?");
}

static char *bname(rc)
int rc;
{
	switch (rc) {
	case B_RUN:	return ("running");
	case B_EXIT:	return ("terminated");
	case B_FN:	return ("an unmapped function");
	case B_ADDR:	return ("a parameter outside the guest segment");
	case B_SEG:	return ("a DMA base we never handed out");
	case B_VEC:	return ("an interrupt with no handler and no seam");
	case B_TRAP:	return ("a divide by zero");
	}
	return ("?");
}

/*
 * Read PROG.CMD into `stage', which is a whole segment, and answer its
 * length -- or -1, having said why.  Sequential reads of 128-byte
 * records: a .CMD is a record-granular file whose header says how much
 * of it means anything, and i86hdr() is given the length this returns.
 */
static long cmdread(name, stage)
char *name, *stage;
{
	register long n;

	mkfcb(name, &f);
	if ((__bdos(BDOS_OPEN, (long) &f) & 0xff) == 0xff) {
		cputs("i86: cannot open ");
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
		cputs("i86: ");
		cputs(name);
		cputs(" is empty\r\n");
		return (-1L);
	}
	return (n);
}

static VOID segcopy(dst, src, n)
char *dst, *src;
long n;
{
	register long i;

	for (i = 0; i < n; i++)
		dst[i] = src[i];
}

/* ------------------------------------------------------------------ */

/* Every segment the guest was given, back to the allocator, in reverse
 * order of acquisition.  ngseg is 2 until the header says otherwise. */
static VOID putsegs()
{
	register int i;

	while (nxseg > 0)
		xsegput(xsegb[nxseg - 1]);
	for (i = ngseg - 1; i >= 0; i--)
		if (gseg[i])
			segcall(SEG_PUT, (long) gseg[i]);
	ngseg = 0;
}

static char *mname(mo)
int mo;
{
	switch (mo) {
	case M_8080:	return ("8080");
	case M_SMALL:	return ("small");
	case M_COMPACT:	return ("compact");
	case M_LARGE:	return ("large");
	}
	return ("?");
}

int main(argc, argv)
int argc;
char *argv[];
{
	struct i86in in;
	struct i86grp *g;
	register int i;
	long n, k, limit, xa, np;
	int sseg, rc, brc, slot, need;
	unsigned nb;
	char *stage;
	static char hdr[CMD_HDR];
	static char tail[SECLEN];

	if (argc < 2) {
		cputs("usage: cpm86 prog.cmd [tail...]\r\n");
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
	cputs("i86: free segments: ");
	putdec((unsigned) n);
	cputs("\r\n");

	ngseg = 0;
	cseg = (int) segcall(SEG_GET, 0L);
	if (cseg == 0) {
		cputs("i86: no segment for the guest's code -- this machine "
		      "has no memory above the system's own\r\n");
		return (1);
	}
	gseg[0] = cseg;
	ngseg = 1;
	dseg = (int) segcall(SEG_GET, 0L);
	if (dseg == 0) {
		cputs("i86: no second segment for the guest's data\r\n");
		putsegs();
		return (1);
	}
	gseg[1] = dseg;
	ngseg = 2;
	sseg = (int) segcall(SEG_GET, 0L);
	if (sseg == 0) {
		cputs("i86: no third segment to stage the image in\r\n");
		putsegs();
		return (1);
	}
	/*
	 * A segment number becomes an address here and nowhere else, and
	 * it goes through a LONG variable rather than a cast of the
	 * shift expression: cc1 takes an internal error on
	 * `(char *)((long)(s) << 24)' written inline.
	 */
	xa = SEGBASE(cseg);
	cmem = (char *) xa;
	xa = SEGBASE(dseg);
	dmem = (char *) xa;
	xa = SEGBASE(sseg);
	stage = (char *) xa;

	if ((n = cmdread(argv[1], stage)) < 0) {
		segcall(SEG_PUT, (long) sseg);
		putsegs();
		return (1);
	}

	/*
	 * The header into a ZEROED buffer of its own, with the file's
	 * real length handed to i86hdr(): a file shorter than 128 bytes
	 * is not a header, and the bytes that are not there must not be
	 * whatever the segment held last.
	 */
	for (i = 0; i < CMD_HDR; i++)
		hdr[i] = 0;
	segcopy(hdr, stage, n < (long) CMD_HDR ? n : (long) CMD_HDR);
	rc = i86hdr(hdr, (i32) n, &C);
	cputs("i86: header: ");
	cputs(i86cerr(rc));
	cputs("\r\n");
	/* A file that filled the staging segment may go on past it. */
	if (rc == CE_OK && n >= 0x10000L && C.need > n) {
		cputs("i86: image larger than 64K\r\n");
		rc = CE_BIG;
	}
	if (rc != CE_OK) {
		segcall(SEG_PUT, (long) sseg);
		putsegs();
		return (1);
	}
	cputs("i86: model ");
	cputs(mname(C.model));
	cputs(", ");
	putdec((unsigned) C.ng);
	cputs(" groups, entry ");
	phex4((int) C.entry);
	cputs("\r\n");

	/*
	 * ONE SEGMENT PER GROUP, and the two already held are the first
	 * two of them.  A compact or large .CMD declares three to eight;
	 * the pool has seven and one of them is `stage', so six is the
	 * most this machine can place and the refusal below names the
	 * arithmetic rather than saying "no memory".
	 */
	need = C.ng;
	/* The 8080 model has no data group: its segment goes back, so the
	 * guest owns no paragraph 2000 and function 59 places above 1000,
	 * as on the host. */
	if (need == 1) {
		segcall(SEG_PUT, (long) dseg);
		ngseg = 1;
	}
	cputs("i86: code segment ");
	phex2(cseg);
	if (ngseg == 2) {
		cputs(", data segment ");
		phex2(dseg);
	}
	cputs(", staging segment ");
	phex2(sseg);
	cputs("\r\n");
	if (need > I86MAXG) {
		cputs("i86: this program declares ");
		putdec((unsigned) C.ng);
		cputs(" groups and each one needs a whole 64K segment.  "
		      "This machine has seven and one of them stages the "
		      "file, so six is the most that can be placed.\r\n");
		segcall(SEG_PUT, (long) sseg);
		putsegs();
		return (1);
	}
	if (need > 2) {
		cputs("i86: extra segments:");
		while (ngseg < need) {
			i = (int) segcall(SEG_GET, 0L);
			if (i == 0)
				break;
			gseg[ngseg++] = i;
			conout(' ');
			phex2(i);
		}
		cputs("\r\n");
		if (ngseg < need) {
			cputs("i86: only ");
			putdec((unsigned) ngseg);
			cputs(" of the ");
			putdec((unsigned) need);
			cputs(" segments this program's groups need\r\n");
			segcall(SEG_PUT, (long) sseg);
			putsegs();
			return (1);
		}
	}

	/*
	 * Zero every segment BEFORE placing: see the file header.  The
	 * guest's uninitialised data must read as zeroes and not as
	 * whatever the last program to hold this physical page left.
	 *
	 * The paragraphs are 0x1000 apart, one 64 KB apart from the next,
	 * so a guest that computes a segment value from another one --
	 * `add ax,0x1000' after LES, which is how a large-model program
	 * walks its own groups -- lands on a paragraph i86resolve() knows.
	 */
	for (i = 0; i < need; i++) {
		xa = SEGBASE(gseg[i]);
		i86spar[i] = (i16) (0x1000 * (i + 1));
		i86sbase[i] = (char *) xa;
		segclr(i86sbase[i]);	/* the BSS a .CMD has no bytes for */
	}
	i86nseg = need;
	cmem = i86sbase[0];
	dmem = i86sbase[1];
	G.fl = F_ONES;
	G.lz = LZ_NONE;
	rc = i86place(&C, &G, need);
	cputs("i86: place: ");
	cputs(i86cerr(rc));
	cputs("\r\n");
	if (rc != CE_OK) {
		segcall(SEG_PUT, (long) sseg);
		putsegs();
		return (1);
	}

	/*
	 * The images, out of the staging segment and into the segments
	 * i86place() gave the groups.  Through g->sidx and NOT g->seg:
	 * an auxiliary group of the large model owns a segment and no
	 * segment REGISTER, so G.sb[] cannot name it and i86sbase[] is
	 * the only handle there is.
	 */
	for (i = 0; i < CMD_NGRP; i++) {
		g = &C.g[i];
		if (g->form == G_NONE || g->form > G_AUX4)
			continue;
		segcopy(i86sbase[g->sidx], stage + g->foff,
			(long) i86have(g, (i32) n));
	}
	/*
	 * The staging segment becomes paragraph 0 now that the images
	 * are out of it: zeroed, registered after the groups so that a
	 * paragraph inside a group still resolves to that group, and
	 * held until the run ends.  Zeroed is what makes it a vector
	 * table with no handlers in it, which is what leaves INT 0E0h
	 * the seam's until a guest writes a vector of its own.
	 */
	segclr(stage);
	i86spar[need] = 0;
	i86sbase[need] = stage;
	i86nseg = need + 1;
	gseg[ngseg++] = sseg;

	/*
	 * One line per group: its form, the guest paragraph it was given
	 * and the physical segment behind that paragraph.  This is the
	 * evidence for the whole compact/large lane -- a group with a
	 * paragraph of its own is a group with a segment of its own -- and
	 * it costs nothing on a small-model file, where it prints the two
	 * lines PIP has always had implicitly.
	 */
	for (i = 0; i < CMD_NGRP; i++) {
		g = &C.g[i];
		if (g->form == G_NONE || g->form > G_AUX4)
			continue;
		cputs("i86: group ");
		putdec((unsigned) g->form);
		cputs(" at ");
		phex4((int) g->par);
		cputs(":0000, segment ");
		phex2(gseg[g->sidx]);
		cputs(", ");
		putdec((unsigned) g->npar);
		cputs(" paragraphs\r\n");
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

	/* The base page goes at the base of the group DS names -- the
	 * data group in the small model, the one group in the 8080
	 * model, where it lands on the 256 zero bytes the image
	 * supplies for it -- and the stack at the top of that group's
	 * allocation, which is where CP/M-86 leaves SP. */
	slot = C.model == M_8080 ? S_CS : S_DS;
	i86bpage(&C, &G, slot, tail);
	np = 0;
	for (i = 0; i < CMD_NGRP; i++)
		if (C.g[i].form && C.g[i].seg == slot)
			np = (long) C.g[i].npar;
	if (np == 0)
		np = (long) C.g[0].npar;
	G.r[R_SP] = (i16) (np >= (long) CMD_MAXPAR ? 0xfffe
					: np * (long) CMD_PARA);

	/*
	 * The evidence.  Every byte below was written into a segment
	 * BIOS function 25 handed out and is being read back out of it
	 * from Normal mode: 0x0000 is the base page's segment table
	 * (code base and length, then data), 0x0080 the command tail
	 * with its length byte, and the code segment's own first bytes
	 * at the entry point.  A segment that was not mapped, or was
	 * mapped System only, does not get this far -- it faults.
	 */
	cputs("i86: the base page, out of the guest's data segment:\r\n");
	pdump(G.sb[slot], 0x00, 16);
	pdump(G.sb[slot], 0x80, 16);
	cputs("i86: the code group at its entry point:\r\n");
	pdump(G.sb[S_CS], (int) C.entry, 16);

	i86ninsn = i86nflag = 0;
	i86nsegslow = i86nsegbad = 0;
	i86segget = xsegget;
	i86segput = xsegput;
	i86wait = i86sleep;
	i86bdosinit(&G);

	limit = 20000000L;
	brc = B_RUN;
	rc = X_OK;
	for (k = 0; k < limit; ) {
		nb = limit - k > 16384L ? 16384 : (unsigned) (limit - k);
		i86nrun = nb;
		rc = i86runa(&G, &in);
		k += (long) (nb - i86nrun);
		if (rc == X_OK)
			continue;
		if (rc != X_INT)
			break;
		brc = i86bdos(&G);
		if (brc != B_RUN)
			break;
		k++;
	}
	/* A loop that ended on a bad instruction, a HLT or the step
	 * limit never re-entered the seam, so the last partial line of
	 * the guest's output is still sitting in the batch. */
	i86oflush();
	i86bdosfini();
	k += i86nskip;

	cputs("\r\ni86: ");
	pdecl(k);
	cputs(" instructions, ");
	pdecl((long) i86nbdos);
	cputs(" BDOS calls, ");
	pdecl((long) i86nflag);
	cputs(" flag materialisations\r\n");
	cputs("i86: slow segment resolutions ");
	pdecl((long) i86nsegslow);
	cputs(", refused ");
	pdecl((long) i86nsegbad);
	cputs("\r\n");
	/* Two ways out, and both are the guest finishing: BDOS function 0,
	 * and the warm boot -- a far transfer to <entry SS>:0000, which is
	 * what DRI's PL/M-86 epilogue does and what src/cmd/i86exec.c
	 * wboot() recognises.  The warm boot says so on its own line, so
	 * the transcript records WHICH exit was taken and where. */
	if (rc == X_WBOOT) {
		cputs("i86: warm boot: a far transfer to the entry stack "
			"segment, cs:ip ");
		phex4((int) G.sr[S_CS]);
		conout(':');
		phex4((int) G.ip);
		cputs("\r\n");
	}
	if (brc == B_EXIT || rc == X_WBOOT)
		cputs("i86: the guest terminated\r\n");
	else {
		cputs("i86: stopped: ");
		cputs(xname(rc));
		cputs(", seam says ");
		cputs(i86berr());
		cputs(" (");
		cputs(bname(brc));
		cputs("), fn ");
		putdec((unsigned) i86bdosfn);
		cputs(", cs:ip ");
		phex4((int) G.sr[S_CS]);
		conout(':');
		phex4((int) G.ip);
		cputs("\r\n");
	}
	putsegs();
	return (brc == B_EXIT || rc == X_WBOOT ? 0 : 1);
}
