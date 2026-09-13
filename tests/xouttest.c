/*
 * xouttest.c -- drive src/bdos/pgmld.c's pgmld() on the host, against
 * malformed x.out images, with the compiler's own bounds checking
 * (-fsanitize=address) watching the loader's segment arrays.
 *
 * WHAT IS BEING TESTED.  pgmld() used to take the image header's segment
 * count and loop on it with no bound against the sixteen-element x_sg,
 * seglim, segsiz and segloc arrays, so a seventeen-segment file wrote past
 * resident loader state; and it used to compare readxsg()'s end-of-file
 * answer against the wrong sentinel while loadseg() advanced past ignored
 * read errors, so a truncated program was loaded and run with stale bytes
 * standing in for its missing code.
 *
 * WHY ON THE HOST, when tests/verify.mk's verify-xout runs the same two
 * malformed images on the real machine: because the RETURN CODE IS NOT THE
 * OBJECT.  The seventeen-segment file is refused by the segment-number
 * check further down -- it was refused before this fix too -- and the
 * refusal says nothing about the memory the count loop had already
 * scribbled on by then.  Here the arrays are instrumented globals: a write
 * one element past them aborts the run with a global-buffer-overflow
 * report, which is the only way to assert "the state past the array is
 * intact" rather than "the caller got an error".  verify-xout asserts the
 * other half, which only the real machine can show: that nothing ran.
 *
 * The harness is the wdtest.c/crsrtest.c pattern -- compile the SAME
 * source with its surroundings reached through overridable names.  The
 * "machine" here is a 64 KB byte array standing in for the TPA segment
 * plus a byte string standing in for the open file; bdos() serves the two
 * calls pgmld makes (set DMA, read sequential) out of that string, one
 * 128-byte record at a time, and refuses the read when the records run
 * out, which is exactly how a truncated program presents itself.
 *
 * Build: cc -std=gnu89 -fsanitize=address (no -Isrc/bdos: pgmld.c's
 * "stdio.h" must resolve to the BDOS one next to it, not the host's).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- the simulated machine ------------------------------------------- */

#define GTPABASE 0x32000000L		/* c900cfg.h TPABASE		*/
#define GSEGSZ	 0x10000
#define GPOISON	 0xA5			/* what "the previous program" is */

static unsigned char gmem[GSEGSZ];	/* the TPA segment		*/
static unsigned char gdbank[GSEGSZ];	/* c900cfg.h SPLITDSEG 0x35 --
					   the ONE split-I/D data bank, and
					   the object of the split checks
					   at the end of main()		*/

/* A guest address in the TPA segment addresses gmem, one in the split data
   bank addresses gdbank; anything else is a host pointer the loader handed
   us back (its own local read buffer). */

static char *xlate(a)
long a;
{
	if ((a >> 24) == 0x32)
		return (char *) gmem + (a & 0xffffL);
	if ((a >> 24) == 0x35)
		return (char *) gdbank + (a & 0xffffL);
	return (char *) a;
}

/* ---- the file being loaded ------------------------------------------- */

#define FMAX 8192
static unsigned char fbuf[FMAX];	/* the image, record-padded	*/
static int fend;			/* its length in bytes		*/
static int fpos;			/* next record to hand out	*/
static long dma;			/* what bdos(26) was last told	*/

#define SECL 128

static int reads;			/* records actually delivered	*/
static int failedreads;			/* reads refused at end of file	*/

unsigned bdos(func, param)
int func;
long param;
{
	if (func == 26) {		/* Set DMA			*/
		dma = param;
		return (0);
	}
	if (func == 20) {		/* Read Sequential		*/
		if (fpos + SECL > fend) {
			failedreads++;
			return (1);	/* end of file, as the BDOS says */
		}
		memcpy(xlate(dma), fbuf + fpos, (size_t) SECL);
		fpos += SECL;
		reads++;
		return (0);
	}
	fprintf(stderr, "xouttest: unexpected BDOS call %d\n", func);
	exit(2);
	return (0);
}

/* ---- the rest of the system ----------------------------------------- */

long map_adr(a, space)
long a;
int space;
{
	if (a == 0L && (space == 4 || space == 5 || space == 0x105))
		return (GTPABASE);	/* TPAPROG/TPADATA/TRUE_TPAPROG	*/
	return (a);
}

/*  biosdef.h's cpy_in/cpy_out are macros over mem_cpy, so this one
    routine serves both directions -- whichever end is a guest address is
    translated by xlate().  */

mem_cpy(src, dst, len)
long src, dst;
long len;
{
	memcpy(xlate(dst), xlate(src), (size_t) len);
	return (0);
}

/* The memory region table.  One region: the TPA.  bgetseg() is bios6(18). */

static struct {
	int	count;
	struct { long tpalow, tpalen; } m_reg[2];
} mrt = { 1, { { GTPABASE, 0x10000L }, { 0L, 0L } } };

char *bios6(fn)
int fn;
{
	return ((char *) &mrt);
}

unsigned rsxres()
{
	return (0);			/* no resident extensions	*/
}

unsigned rsxchk(org, len)
unsigned org, len;
{
	return (0);
}

unsigned rsxlink(org, len)
unsigned org, len;
{
	return (0);
}

short spflag;

static int sploads;

int spload(n)
unsigned n;
{
	sploads++;
	return (0);
}

/*  src/bdos/proc.c -- "is a split-I/D program live in another process?".
    The loader asks it before it writes anything; here the answer is a
    variable the checks set.  */

static int gspother;

int pspother()
{
	return (gspother);
}

static long dmaset;			/* what pgmld recorded, leg (c)	*/
static int dmasets;

pdmaset(a)
long a;
{
	dmaset = a;
	dmasets++;
	return (0);
}

/*  pgmld.c calls these before it defines them as MLOCAL, which the 1982
    compiler accepted; declare them first so a host compiler does too.  */
static int ldrsx();
static int rsxfer();

#include "../src/bdos/pgmld.c"

/* ---- images ---------------------------------------------------------- */

/* Build an image with `n' segment headers (types and lengths from the
   arrays) and `datarecs' records of segment data, then pad to a record
   boundary.  `count' is what the HEADER says, which is the whole point:
   it need not be n.  */

/*  The segment number every x_sg entry gets.  Zero for the non-segmented
    images below, which never have it looked at; a SEGMENTED image is
    placed by raw CPU segment number, and pgmld refuses one whose entries
    do not name the TPA segment the MRT advertises, so the segmented
    checks in main() set this to 0x32 (c900cfg.h TPASEG) first.  */

static int gsgno = 0;

static int mkimage(magic, count, n, typ, len, bytes)
int magic, count, n;
int *typ;
unsigned *len;
int bytes;
{
	struct x_hdr	h;
	struct x_sg	s;
	int		i, p;

	memset(fbuf, 0, sizeof fbuf);
	h.x_magic = (short) magic;
	h.x_nseg = (short) count;
	h.x_init = 0L;
	h.x_reloc = 0L;
	h.x_symb = 0L;
	memcpy(fbuf, (char *) &h, sizeof h);
	p = sizeof h;
	for (i = 0; i < n; i++) {
		s.x_sg_no = (char) gsgno;
		s.x_sg_typ = (char) typ[i];
		s.x_sg_len = len[i];
		memcpy(fbuf + p, (char *) &s, sizeof s);
		p += sizeof s;
	}
	for (i = 0; i < bytes; i++)
		fbuf[p + i] = (unsigned char) (i & 0x7f);
	p += bytes;
	while (p % SECL)
		fbuf[p++] = 0x1a;
	fend = p;
	fpos = 0;
	reads = 0;
	failedreads = 0;
	return (p);
}

static struct lpb	lpb;

static int load()
{
	int k;

	memset(gmem, GPOISON, sizeof gmem);
	memset(gdbank, GPOISON, sizeof gdbank);	/* "another program's data
						   is in the bank"	*/
	lpb.fcbaddr = 0x1234L;
	lpb.pgldaddr = GTPABASE;
	lpb.pgtop = GTPABASE + 0x10000L;
	lpb.flags = 0;
	spflag = 0;
	sploads = 0;
	dmasets = 0;
	k = (int) pgmld((long) (char *) &lpb);
	if (getenv("XOUTDBG")) {	/* for working on this harness	*/
		fprintf(stderr,
			"[dbg] pgmld=%d records read=%d refused=%d file=%d"
			" text=%lx/%lx data=%lx\n",
			k, reads, failedreads, fend, (long) textloc,
			(long) textsiz, (long) datasiz);

		/*  The base page's geometry.  BPLEN is sizeof (struct
		    b_page) and the entry frame is sizeof (struct sstack) /
		    (struct ustack), all of which are HOST sizes here and
		    not the target's -- which is why every check in main()
		    is written against those names rather than against the
		    target's 0x100/0x200/8.  */

		if (k == 0) {
			struct b_page *d = (struct b_page *) xlate(lpb.bpaddr);

			fprintf(stderr,
				"[dbg] BPLEN=%ld DEFSTACK=%ld sstack=%ld"
				" ustack=%ld\n"
				"[dbg] bpaddr=%lx htpa=%lx lbss+bsslen=%lx"
				" freelen=%lx\n",
				(long) BPLEN, (long) DEFSTACK,
				(long) sizeof (struct sstack),
				(long) sizeof (struct ustack),
				(long) lpb.bpaddr, (long) d->htpa,
				(long) (d->lbss + d->bsslen),
				(long) d->freelen);
		}
	}
	return (k);
}

/* ---- the checks ------------------------------------------------------ */

static int fails;

static check(cond, what)
int cond;
char *what;
{
	if (!cond) {
		printf("xouttest: FAIL -- %s\n", what);
		fails++;
	}
	return (cond);
}

/* One code segment, one data segment, one bss segment: a normal program. */
static int gtyp[3] = { X_SG_COD, X_SG_DAT, X_SG_BSS };
static unsigned glen[3] = { 0x200, 0x80, 0x100 };

int main()
{
	int	k, i;
	int	btyp[40];
	unsigned blen[40];
	struct b_page *bpp;		/* the base page the loader wrote */

	/* ---- a GOOD image still loads.  Everything below is worthless if
	   the bound refuses normal programs too. */
	mkimage(X_NXN_MAGIC, 3, 3, gtyp, glen, 0x280);
	k = load();
	check(k == 0, "a well-formed three-segment image must still load");
	check(failedreads == 0, "a good load must not run off the end of the file");
	check(gmem[0] == 0 && gmem[1] == 1 && gmem[0x7f] == 0x7f,
	      "a good load must place the program's own bytes in the TPA");

	/*  FREELEN IS THE SPAN FROM THE END OF BSS TO THE STACK POINTER.
	    DRI defines the field as "Length of free memory after bss"
	    (pg/pgmac.tex:60), with the user stack at the highest address of
	    the TPA and the stack's maximum size equal to "the address of
	    the stack pointer minus the last address of the program"
	    (pg/pgm4f.tex:516-518).  So the end of the free span IS htpa,
	    the program's own initial SP.  Written as an equation rather
	    than a constant, because it has to hold whatever the image's
	    sizes are and whatever is resident above it.  */

	bpp = (struct b_page *) xlate(lpb.bpaddr);
	check(bpp->lbss + bpp->bsslen + bpp->freelen == bpp->htpa,
	      "freelen must run from the end of bss to the initial stack"
	      " pointer: the program owns everything between, and nothing above");

	/* ---- 17 segments: one past the sixteen-element arrays.  The
	   header count is 17 and seventeen real segment headers follow, so
	   the loop has something to read for every one of them.  Under
	   -fsanitize=address the seventeenth write to x_sg/seglim/segsiz
	   ABORTS this program: that abort is the assertion. */
	for (i = 0; i < 17; i++) {
		btyp[i] = (i == 0) ? X_SG_COD : X_SG_BSS;
		blen[i] = (i == 0) ? 0x200 : 0x10;
	}
	mkimage(X_NXN_MAGIC, 17, 17, btyp, blen, 0x200);
	k = load();
	check(k != 0, "a 17-segment image must be refused");
	check(k == 1, "a 17-segment image must be refused as a bad header");

	/* ---- 1000 segments: the same defect, far enough past the arrays
	   to reach the whole resident data area rather than one element. */
	for (i = 0; i < 16; i++) {
		btyp[i] = (i == 0) ? X_SG_COD : X_SG_BSS;
		blen[i] = (i == 0) ? 0x200 : 0x10;
	}
	mkimage(X_NXN_MAGIC, 1000, 16, btyp, blen, 0x200);
	k = load();
	check(k == 1, "a 1000-segment image must be refused as a bad header");

	/* ---- a negative count.  The old loop was empty for it, which is
	   harmless by accident; say so on purpose. */
	mkimage(X_NXN_MAGIC, -1, 3, gtyp, glen, 0x280);
	k = load();
	check(k == 1, "a negative segment count must be refused as a bad header");

	/* ---- zero segments: nothing to load, and nothing to run. */
	mkimage(X_NXN_MAGIC, 0, 0, gtyp, glen, 0);
	k = load();
	check(k == 1, "a zero-segment image must be refused as a bad header");

	/* ---- truncated: the header declares 0x200 bytes of code and the
	   file holds one 128-byte record of it.  THE OBJECT: the TPA must
	   not come back holding poison (the previous program) where the
	   missing code should be, and the caller must get READERR. */
	mkimage(X_NXN_MAGIC, 3, 3, gtyp, glen, 0x80);
	k = load();
	check(k == 3, "a truncated image must be refused with a read error");
	check(failedreads > 0, "the truncated load must have hit a failed read");

	/* ---- truncated in its very first record: the segment headers are
	   there, the data is not at all. */
	mkimage(X_NXN_MAGIC, 3, 3, gtyp, glen, 0);
	k = load();
	check(k == 3, "an image with no data records must be refused with a read error");

	/* ---- and a good image loads after every refusal, which is what
	   says the refusals left no wreckage in the loader's own state. */
	mkimage(X_NXN_MAGIC, 3, 3, gtyp, glen, 0x280);
	k = load();
	check(k == 0, "a good image must still load after the malformed ones");
	check(dmasets == 1, "a successful load must record one default DMA");
	check(dmaset == lpb.bpaddr + (long) (sizeof (struct b_page) - SECLEN),
	      "the recorded default DMA must be the base page's buffer");

	/* ---- THE SPLIT-I/D BANK IS SHARED, AND THE REFUSAL MUST COME
	   BEFORE THE LOAD.  The data bank (c900cfg.h SPLITDSEG, one fixed
	   physical page that no page swap moves) is gdbank here, poisoned
	   before every load to stand for a live split program's data.
	   fn 144 used to ask "is one already live?" AFTER ldimage() had
	   returned, so the answer was correct and the bank was already
	   gone; the object of these two checks is the BANK, not the code.
	   src/bdos/proc.c pcrgen() and the review's P1 #6. */

	gspother = 0;			/* nobody else is split: it loads */
	mkimage(X_NXI_MAGIC, 3, 3, gtyp, glen, 0x280);
	k = load();
	check(k == 0, "a split-I/D image must load when no other process is split");
	check(sploads == 1, "a split-I/D load must patch its text once");
	check(gdbank[0] != GPOISON,
	      "a split-I/D load must put its data in the split data bank -- if it"
	      " does not, the refusal check below proves nothing");

	/*  A SPLIT-I/D PROGRAM IS THE ONE FORMAT THAT KEEPS THE OLD
	    EXPRESSION, and it must: its bss is in the D bank (0x35) and its
	    stack is in the code bank (0x32), so htpa - (lbss + bsslen)
	    would subtract offsets in two DIFFERENT segments and produce
	    nonsense.  Its free span is the room left in the D bank, which
	    is what seglim - segsiz measures there.  With nothing resident
	    the D segment's limit is 0x10000 - BPLEN - DEFSTACK = 0xFE00,
	    and this image puts 0x180 of data and bss in it.  */

	bpp = (struct b_page *) xlate(lpb.bpaddr);
	check(bpp->freelen == (long) GSEGSZ - BPLEN - DEFSTACK - 0x180L,
	      "a split-I/D program's freelen must measure the room left in its"
	      " DATA BANK, not a distance to a stack pointer in another segment");

	gspother = 1;			/* one is live in another process */
	mkimage(X_NXI_MAGIC, 3, 3, gtyp, glen, 0x280);
	k = load();
	check(k == NOSPLIT, "a second split-I/D image must be refused (NOSPLIT)");
	check(sploads == 0, "a refused split-I/D load must not patch any text");
	for (i = 0, k = 0; i < GSEGSZ; i++)
		if (gdbank[i] != GPOISON)
			k++;
	check(k == 0,
	      "A REFUSED SPLIT-I/D LOAD MUST NOT TOUCH THE SHARED DATA BANK:"
	      " the live split program's data is in it");
	check(reads == 1,
	      "a refused split-I/D load must read the header and nothing more:"
	      " one record says the magic, and the magic is the whole question");

	/* ---- and a non-split image still loads with a split program live,
	   because it wants neither bank. */
	gspother = 1;
	mkimage(X_NXN_MAGIC, 3, 3, gtyp, glen, 0x280);
	k = load();
	check(k == 0,
	      "an ordinary image must still load while a split program is live");
	gspother = 0;

	/* ---- A SEGMENTED (0xEE01) IMAGE: THE CASE THE FREELEN FIX IS FOR.
	   Every segment's limit starts at SEGLEN - rsvd, and on this path
	   nothing used to reduce it for the base page and the stack: the
	   only reduction written is loadseg's X_SG_STK case, and it never
	   runs, because lout2cpm emits COD, DAT and BSS and no stack
	   segment.  So freelen reached 0x200 bytes past the top of what the
	   program may touch -- over its own base page and stack -- and
	   PIP's and STAT's sbrk ceiling (src/cmd/pipmain.c) went with it.
	   The anchors are exact: with nothing resident the stack goes at
	   SEGLEN - BPLEN - DEFSTACK and the segmented entry frame is 8
	   bytes, so the SP is 0xFDF8 -- the value src/cmd/crt0.s has always
	   documented as the entry rr14. */

	gsgno = 0x32;			/* c900cfg.h TPASEG: pgmld refuses a
					   segmented image whose segments
					   name any other		*/
	mkimage(X_SX_MAGIC, 3, 3, gtyp, glen, 0x280);
	k = load();
	gsgno = 0;
	check(k == 0, "a well-formed segmented image must load");
	bpp = (struct b_page *) xlate(lpb.bpaddr);
	check(bpp->htpa == GTPABASE + (long) GSEGSZ - BPLEN - DEFSTACK
			   - (long) sizeof (struct sstack),
	      "a segmented program's initial SP must sit below the base page"
	      " and the default stack the loader reserved for it");
	check(bpp->lbss + bpp->bsslen + bpp->freelen == bpp->htpa,
	      "A SEGMENTED PROGRAM'S FREELEN MUST STOP AT ITS OWN STACK"
	      " POINTER: counting to the segment limit handed it its own base"
	      " page and stack as free memory");
	check(bpp->freelen == bpp->htpa - (GTPABASE + 0x380L),
	      "a segmented program's freelen must be exactly the span from the"
	      " end of bss to the stack pointer");

	/*  And the reservation that makes the equation above satisfiable: an
	    image whose text+data+bss would collide with the base page and
	    the stack is now REFUSED, where before it was loaded on top of
	    them and freelen went negative.  0xFF00 of segment leaves no
	    room for the 0x200 bytes the loader puts at the top.  The
	    refusal comes before any data is read, so the image needs no
	    real content.  */

	gsgno = 0x32;
	btyp[0] = X_SG_COD;
	blen[0] = 0xFF00;
	mkimage(X_SX_MAGIC, 1, 1, btyp, blen, 0x200);
	k = load();
	gsgno = 0;
	check(k == NOMEM,
	      "a segmented image that would overwrite the base page and stack"
	      " the loader is about to write must be refused, not loaded");

	if (fails == 0)
		printf("xouttest: PASS -- 17, 1000, -1 and 0 segment counts and two\n"
		       "          truncated images all refused, no write past the %d-element\n"
		       "          segment arrays, good images load before and after, a\n"
		       "          second split-I/D image is refused with the shared data bank\n"
		       "          untouched, and freelen ends at the stack pointer for\n"
		       "          segmented and non-segmented images alike\n",
		       NSEG);
	return (fails != 0);
}
