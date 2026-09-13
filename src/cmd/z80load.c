
#include "z80.h"

/* ------------------------------------------------------------------ */
/* small helpers, on the guest's memory rather than on struct z80, so
 * that the furniture can be planted before any state exists	       */

static int pb(mem, a, v)
char *mem;
int a, v;
{
	mem[(z16)a] = (char)v;
	return (0);
}

/* Plant a three-byte 8080 JMP.  Written out rather than folded into the
 * callers because every one of page zero's fixed addresses is a JMP and
 * the byte order of the target is the one thing to get right. */
static int pjmp(mem, a, dst)
char *mem;
int a;
z16 dst;
{
	pb(mem, a, 0xc3);
	pb(mem, a + 1, dst & 0xff);
	pb(mem, a + 2, (dst >> 8) & 0xff);
	return (0);
}

/* Plant a hook stub: `ED FE nn' then a RET, so that a hook reached by a
 * CALL behaves as a subroutine and the guest resumes after it. */
static int phook(mem, a, no)
char *mem;
int a, no;
{
	pb(mem, a, 0xed);
	pb(mem, a + 1, 0xfe);
	pb(mem, a + 2, no);
	pb(mem, a + 3, 0xc9);
	return (0);
}

/* ------------------------------------------------------------------ */
/* the furniture						       */

int z80furn(m)
struct z80 *m;
{
	register char *mem;
	register int k;
	int stub;

	mem = m->m;

	/* Page zero.  The warm-boot vector points at BIOS table entry 1,
	 * which is where a real CP/M's does, and it is also where a
	 * program looks when it wants the BIOS base: it reads the WORD
	 * at 0x0001 and subtracts 3. */
	pjmp(mem, PZ_WBOOT, (z16)(FAKEBIOS + 3));
	pb(mem, PZ_IOBYTE, 0);
	pb(mem, PZ_CDISK, 0);
	pjmp(mem, PZ_BDOS, (z16)FAKEBDOS);

	/* The BDOS entry, six bytes above the top of the TPA so that
	 * `LHLD 6' answers with a TPA that really does end where we say
	 * it does.  See the FAKEBDOS comment in z80.h. */
	phook(mem, FAKEBDOS, HOOK_BDOS);

	/* The exit stub, and the word the guest's initial SP points at. */
	phook(mem, FAKEEXIT, HOOK_EXIT);

	/* The BIOS table, and its stubs immediately after it. */
	stub = FAKEBIOS + 3 * NBIOSV;
	for (k = 0; k < NBIOSV; k++) {
		pjmp(mem, FAKEBIOS + 3 * k, (z16)(stub + 4 * k));
		phook(mem, stub + 4 * k, HOOK_BIOS + k);
	}
	return (0);
}

/* ------------------------------------------------------------------ */
/* the 0xC9 prefix						       */

static z16 gw(img, a)
char *img;
long a;
{
	return ((z16)((img[a] & 0xff) | ((img[a + 1] & 0xff) << 8)));
}

/*
 * z80rsxhdr -- parse a GENCOM-bound .COM's 256-byte header record.
 *
 * Returns 0 if the image does not begin with one, 1 with *r filled in
 * if it does.  Every field is DRI's own, from cpm8000/ref/cpm3/
 * loader3.asm:96-100 and :234 -- see the struct comrsx comment in
 * z80.h, which also records that the layout was read back out of the
 * five files that carry one and agreed.
 */
int z80rsxhdr(img, n, r)
char *img;
long n;
struct comrsx *r;
{
	register int i, k;
	long d;

	r->comlen = 0;
	r->rsxonly = 0;
	r->n = 0;
	if (n < (long)RSX_HDRLEN || (img[0] & 0xff) != 0xc9)
		return (0);
	r->comlen = gw(img, 1L);
	/* `lda module / cpi ret' (loader3.asm:253): a 0xC9 at the image
	 * base -- file offset 0x100, guest 0x100 -- means there is no
	 * program here at all, only RSXes.  SAVE.COM is exactly that.
	 *
	 * The guard above admits n == RSX_HDRLEN, an image that is the
	 * header record and nothing else, and for that image byte
	 * RSX_HDRLEN is not part of the image: `n' is its length.  So
	 * the length is tested before the byte is read, because the
	 * caller's buffer may be exactly n bytes long and reading past
	 * it would answer this question out of somebody else's memory. */
	r->rsxonly = (n > (long)RSX_HDRLEN &&
		      (img[RSX_HDRLEN] & 0xff) == 0xc9);
	for (i = 0; i < RSX_NDESC; i++) {
		d = (long)RSX_DESC0 + (long)i * RSX_DSTRIDE;
		if (d + RSX_DSTRIDE > n)
			break;
		if (gw(img, d) == 0)		/* loader3.asm:244	*/
			break;
		r->off[i] = gw(img, d);
		r->len[i] = gw(img, d + 2);
		r->nbank[i] = (z8)(img[d + 4] & 0xff);
		for (k = 0; k < 8; k++)
			r->name[i][k] = img[d + 6 + k];
		r->name[i][8] = '\0';
		r->n++;
	}
	return (1);
}

/* ------------------------------------------------------------------ */
/* the command tail and the two default FCBs			       */

/*
 * Parse one blank-delimited token into a 16-byte FCB image: drive byte,
 * eight name characters, three type characters, all blank-padded and
 * upper-cased, with `*' expanded into `?' the way the CCP does.  The
 * remaining bytes are left alone, because FCB2 at 0x6C overlaps FCB1's
 * tail and the CCP writes them in this order for that reason.
 *
 * Returns the index of the first character it did not consume.
 */
static int fcb1(mem, at, s, i)
char *mem;
int at;
char *s;
int i;
{
	register int c, k;
	int drv, dot;

	for (k = 0; k < 11; k++)	/* eight name, three type	*/
		pb(mem, at + 1 + k, ' ');
	pb(mem, at, 0);
	while (s[i] == ' ')
		i++;
	if (s[i] == '\0')
		return (i);
	/* A drive letter is `X:' and nothing else: one letter, then a
	 * colon.  1 means A, which is the CP/M convention and the reason
	 * 0 can mean "the default drive". */
	drv = 0;
	c = s[i] & 0xff;
	if (c >= 'a' && c <= 'z')
		c -= 'a' - 'A';
	if (c >= 'A' && c <= 'Z' && s[i + 1] == ':') {
		drv = c - 'A' + 1;
		i += 2;
	}
	pb(mem, at, drv);
	k = 0;
	dot = 0;
	while (s[i] != '\0' && s[i] != ' ' && s[i] != '=' && s[i] != ',') {
		c = s[i] & 0xff;
		i++;
		if (c >= 'a' && c <= 'z')
			c -= 'a' - 'A';
		if (c == '.') {
			if (dot)
				break;
			dot = 1;
			k = 8;
			continue;
		}
		if (c == '*') {
			/* `*' is not stored: it fills the rest of its
			 * field with `?', which is what a directory match
			 * actually compares against. */
			while (k < (dot ? 11 : 8))
				pb(mem, at + 1 + k++, '?');
			continue;
		}
		if (k < (dot ? 11 : 8))
			pb(mem, at + 1 + k++, c);
	}
	return (i);
}

/*
 * z80tail -- build the command tail at 0x0080 and the two default FCBs.
 *
 * The tail is stored the way the CCP stores it: a length byte, then the
 * characters, then a NUL.  The CCP upper-cases the whole command line
 * before it gets here, and programs rely on that -- PIP parses its own
 * tail rather than using the FCBs, and it compares against upper case.
 *
 * Returns the tail length.
 */
int z80tail(m, tail)
struct z80 *m;
char *tail;
{
	register char *mem;
	register int i, c;
	int n;

	mem = m->m;
	n = 0;
	for (i = 0; tail[i] != '\0' && n < 127; i++) {
		c = tail[i] & 0xff;
		if (c >= 'a' && c <= 'z')
			c -= 'a' - 'A';
		pb(mem, PZ_DMA + 1 + n, c);
		n++;
	}
	pb(mem, PZ_DMA, n);
	pb(mem, PZ_DMA + 1 + n, 0);

	/* FCB1 first and in full: its 36 bytes run from 0x5C to 0x7F, so
	 * zeroing it clears FCB2's area as well.  Then FCB2's 16 bytes
	 * are written over the middle of it, which is exactly what the
	 * CCP does and why the second FCB has no extent or record
	 * fields of its own. */
	for (i = 0; i < 36; i++)
		pb(mem, PZ_FCB1 + i, 0);
	i = fcb1(mem, PZ_FCB1, (char *)mem + PZ_DMA + 1, 0);
	while (mem[PZ_DMA + 1 + i] == ' ' || mem[PZ_DMA + 1 + i] == '='
	    || mem[PZ_DMA + 1 + i] == ',')
		i++;
	fcb1(mem, PZ_FCB2, (char *)mem + PZ_DMA + 1, i);
	return (n);
}

/* ------------------------------------------------------------------ */
/* the load itself						       */

char *z80lerr(e)
int e;
{
	switch (e) {
	case CL_OK:	return ("ok");
	case CL_EMPTY:	return ("the file is empty");
	case CL_BIG:	return ("the image does not fit under the TPA ceiling");
	case CL_RSX:	return ("a GENCOM-bound .COM: it carries RSXes stage one does not load");
	case CL_NOTCOM:	return ("the first byte is 00 or FF: this file was never written");
	}
	return ("unknown");
}

/*
 * z80load -- bind 64 KB, plant the furniture, place the image, and set
 * the registers a .COM starts with.
 *
 * `mem' is 65,536 bytes the caller owns.  On the target it is a whole
 * host segment, and the guest's 16-bit address is its offset with no
 * arithmetic at all (Z80-SHIM-FEASIBILITY.md §1.1); on the host it is an
 * array, and the (z16) casts in z80exec.c give it the same wraparound.
 */
int z80load(m, mem, img, n)
struct z80 *m;
char *mem, *img;
long n;
{
	register long i;
	struct comrsx r;

	for (i = 0; i < 4; i++)
		m->rp[i] = 0;
	m->a = 0;
	m->f = F_ONE;			/* bit 1 reads back as one	*/
	m->pc = 0;
	m->ix = 0;
	m->iy = 0;
	for (i = 0; i < 3; i++)
		m->arp[i] = 0;
	m->aa = 0;
	m->af = F_ONE;
	m->iff = 0;
	m->lz = LZ_NONE;
	m->lc = 0;
	m->la = 0;
	m->lb = 0;
	m->lr = 0;
	m->halt = 0;
	m->m = mem;

	for (i = 0; i < 0x10000L; i++)
		mem[i] = 0;

	if (n <= 0)
		return (CL_EMPTY);
	if ((img[0] & 0xff) == 0xc9) {
		z80rsxhdr(img, n, &r);
		return (CL_RSX);
	}
	if ((img[0] & 0xff) == 0x00 || (img[0] & 0xff) == 0xff) {
		return (CL_NOTCOM);
	}
	if (n > (long)(GUESTTOP - COM_ORG))
		return (CL_BIG);

	for (i = 0; i < n; i++)
		mem[COM_ORG + i] = img[i];

	z80furn(m);

	/* The stack the CCP hands over: SP two below the exit stub, with
	 * the stub's address on top, so that a program terminating with
	 * a plain RET -- the documented CP/M-80 way -- lands on a hook
	 * and not in the middle of our furniture. */
	m->rp[P_SP] = (z16)(FAKEEXIT - 2);
	mem[(z16)(FAKEEXIT - 2)] = (char)(FAKEEXIT & 0xff);
	mem[(z16)(FAKEEXIT - 1)] = (char)((FAKEEXIT >> 8) & 0xff);
	m->pc = COM_ORG;
	return (CL_OK);
}
