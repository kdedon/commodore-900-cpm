
#include "i86.h"

int	i86intno;		/* vector left behind by X_INT		*/
i16	i86segbad;		/* paragraph that caused X_SEGESC	*/

/*
 * The assigned-paragraph set.  The shim hands out one 64 KB host
 * segment per guest group and records the pair here; every write to a
 * segment register is checked against it.  The check sits on the WRITE
 * and not on the instruction that computed the value, because WordStar
 * was observed capturing an absolute segment into a variable and
 * reloading ES from it later (CPM86-SHIM-FEASIBILITY.md §1.3) -- a
 * check on the arithmetic would have missed that and a check on the
 * write cannot.
 */
i16	i86spar[I86NSEG];	/* guest paragraph			*/
char	*i86sbase[I86NSEG];	/* the host segment we gave it		*/
int	i86nseg;
i32	i86nsegslow;		/* K3's counter: slow-path resolutions	*/
i32	i86nsegbad;		/* ... and the writes it could not cover	*/

/* E1s's one policy question, left to whoever owns the segments.  Null
 * by default: an unresolvable paragraph is then a refusal, which is what
 * the host tests want and what a target build without a spare segment
 * has to do anyway. */
char	*(*i86segnew)();

/* K2's instruments.  Two counters, no branches on the hot path, and
 * they are what turns §6's threshold from an argument into a
 * measurement.  On the target they cost two long increments; when that
 * matters they go behind a build flag, which is not yet. */
i32	i86ninsn;		/* instructions executed		*/
i32	i86nflag;		/* times a lazy record was materialised	*/

char *i86resolve(par)
i16 par;
{
	register int i;

	for (i = 0; i < i86nseg; i++)
		if (i86spar[i] == par)
			return (i86sbase[i]);
	return ((char *)0);
}

/* ------------------------------------------------------------------ */
/* registers							       */

/* Byte registers are AL CL DL BL AH CH DH BH for reg = 0..7: the low
 * halves first, then the high ones.  Done arithmetically rather than by
 * overlaying a char array on the word file, because the host and the
 * Z8000 disagree about which end of a word a byte lives at and this
 * file must not. */
static int getb(m, n)
struct i86 *m;
int n;
{
	register i16 v;

	v = m->r[n & 3];
	return ((n & 4) ? ((v >> 8) & 0xff) : (v & 0xff));
}

static setb(m, n, v)
struct i86 *m;
int n, v;
{
	register i16 w;

	w = m->r[n & 3];
	if (n & 4)
		w = (i16)((w & 0x00ff) | ((i16)(v & 0xff) << 8));
	else
		w = (i16)((w & 0xff00) | (v & 0xff));
	m->r[n & 3] = w;
	return (0);
}

/* ------------------------------------------------------------------ */
/* memory: bytewise, little-endian, offsets wrap inside the segment    */

static int mrb(m, s, off)
struct i86 *m;
int s;
i16 off;
{
	register i16 t;

	s &= 3;
	t = (i16)(m->so[s] + off);
	if (t < m->so[s]) {
		m->fault = 1;
		m->fseg = (i8)s;
		m->foff = off;
		return (0);
	}
	return (m->sb[s][t] & 0xff);
}

static mwb(m, s, off, v)
struct i86 *m;
int s;
i16 off;
int v;
{
	register i16 t;

	s &= 3;
	t = (i16)(m->so[s] + off);
	if (t < m->so[s]) {
		m->fault = 1;
		m->fseg = (i8)s;
		m->foff = off;
		return (0);
	}
	m->sb[s][t] = (char)v;
	return (0);
}

/*
 * i86addr -- the same arithmetic, for the seam rather than for an
 * instruction: the host address of `len' guest bytes, or 0 if they do
 * not all fit in the window.  The BDOS seam hands FCBs and DMA buffers
 * to the native BDOS BY ADDRESS and never copies them
 * (CPM86-SHIM-FEASIBILITY.md §7.2), so this is the only thing standing
 * between a guest DMA offset of 0xFFC0 and our BDOS writing 128 bytes
 * into whatever host segment follows.  len 0 asks only that the offset
 * itself is inside.
 */
char *i86addr(m, s, off, len)
struct i86 *m;
int s;
i16 off;
i32 len;
{
	register i16 t;
	i32 end;

	s &= 3;
	t = (i16)(m->so[s] + off);
	if (t < m->so[s])
		return ((char *)0);
	end = (i32)t + (len ? len - 1 : 0);
	if (end > 0xffffL)
		return ((char *)0);
	return (&m->sb[s][t]);
}

static i16 mrw(m, s, off)
struct i86 *m;
int s;
i16 off;
{
	return ((i16)(mrb(m, s, off) | (mrb(m, s, (i16)(off + 1)) << 8)));
}

static mww(m, s, off, v)
struct i86 *m;
int s;
i16 off, v;
{
	mwb(m, s, off, v & 0xff);
	mwb(m, s, (i16)(off + 1), (v >> 8) & 0xff);
	return (0);
}

/* ------------------------------------------------------------------ */
/* flags								*/

static int par8(v)
int v;
{
	register int n;

	v &= 0xff;
	n = 0;
	while (v) {
		n += v & 1;
		v >>= 1;
	}
	return ((n & 1) == 0);		/* PF is EVEN parity of the low byte */
}

/*
 * Materialise the pending lazy record into m->fl and return FLAGS.
 * Idempotent: after it runs, m->lz is LZ_NONE and m->fl is complete.
 *
 * The width mask and sign bit are the only things that differ between a
 * byte and a word operation, and every formula below is written in
 * terms of them.  Casts to i32 make the carry comparisons unsigned in
 * both worlds -- on the target an i16 promotes to unsigned int, on the
 * host to a 32-bit signed int, and the two disagree about `<' unless
 * the comparison is made explicitly wide.
 */
i16 i86flags(m)
struct i86 *m;
{
	register i16 msk, msb;
	i16 f;

	if (m->lz == LZ_NONE)
		return (m->fl);
	i86nflag++;
	msk = (i16)(m->lw ? 0xffff : 0x00ff);
	msb = (i16)(m->lw ? 0x8000 : 0x0080);
	f = 0;
	if ((i16)(m->lr & msk) == 0)
		f |= F_ZF;
	if (m->lr & msb)
		f |= F_SF;
	if (par8(m->lr))
		f |= F_PF;
	switch (m->lz) {
	case LZ_ADD:
		/* r = a + b + c.  The carry out of the top is visible as
		 * the sum having failed to grow; with a carry in, a sum
		 * merely equal to a has already wrapped. */
		if (m->lc ? ((i32)(m->lr & msk) <= (i32)(m->la & msk))
			  : ((i32)(m->lr & msk) <  (i32)(m->la & msk)))
			f |= F_CF;
		if ((m->la ^ m->lb ^ m->lr) & 0x10)
			f |= F_AF;
		if ((i16)((m->la ^ m->lr) & (m->lb ^ m->lr)) & msb)
			f |= F_OF;
		break;
	case LZ_SUB:
		if (m->lc ? ((i32)(m->la & msk) <= (i32)(m->lb & msk))
			  : ((i32)(m->la & msk) <  (i32)(m->lb & msk)))
			f |= F_CF;
		if ((m->la ^ m->lb ^ m->lr) & 0x10)
			f |= F_AF;
		if ((i16)((m->la ^ m->lb) & (m->la ^ m->lr)) & msb)
			f |= F_OF;
		break;
	case LZ_LOG:
		/* CF and OF are cleared; AF is architecturally undefined
		 * and we clear it, which is what the part does. */
		break;
	case LZ_INC:
		/* CF is PRESERVED by INC and DEC -- the one place the
		 * 8086's flag rules are not uniform, and the reason
		 * these are separate classes rather than ADD with b = 1. */
		f |= (i16)(m->fl & F_CF);
		if ((m->la ^ m->lb ^ m->lr) & 0x10)
			f |= F_AF;
		if ((i16)((m->la ^ m->lr) & (m->lb ^ m->lr)) & msb)
			f |= F_OF;
		break;
	case LZ_DEC:
		f |= (i16)(m->fl & F_CF);
		if ((m->la ^ m->lb ^ m->lr) & 0x10)
			f |= F_AF;
		if ((i16)((m->la ^ m->lb) & (m->la ^ m->lr)) & msb)
			f |= F_OF;
		break;
	}
	m->fl = (i16)((m->fl & ~F_LAZY) | f);
	m->lz = LZ_NONE;
	return (m->fl);
}

/* Record a lazy result.  `c' is the carry in for ADC/SBB and 0 else. */
static lazy(m, cls, w, a, b, r, c)
struct i86 *m;
int cls, w, c;
i16 a, b, r;
{
	if (cls == LZ_INC || cls == LZ_DEC)
		i86flags(m);		/* CF must survive: fold first	*/
	m->lz = (i8)cls;
	m->lw = (i8)w;
	m->la = a;
	m->lb = b;
	m->lr = r;
	m->lc = (i8)c;
	return (0);
}

/* ------------------------------------------------------------------ */
/* signed widths						       */

static long sx8(v)
int v;
{
	v &= 0xff;
	return ((long)(v & 0x80 ? v - 256 : v));
}

static long sx16(v)
i16 v;
{
	return ((long)((v & 0x8000) ? (long)(v & 0x7fff) - 32768L
				    : (long)v));
}

static long sx32(v)
i32 v;
{
	v &= 0xffffffffL;
	if (!(v & 0x80000000L))
		return ((long)v);
	return ((long)(v & 0x7fffffffL) - 2147483647L - 1L);
}

/* A signed long back into 32 unsigned bits, on either word size. */
static i32 lo32(v)
long v;
{
	return ((i32)v & 0xffffffffL);
}

/* ------------------------------------------------------------------ */
/* operands							       */

/* The effective address of a mod r/m memory operand.  Computed at
 * execute time because it depends on live registers; the decode that
 * produced `in' does not. */
static i16 ea(m, in)
struct i86 *m;
struct i86in *in;
{
	register i16 a;

	if (in->mod == 0 && in->rm == 6)
		return (in->disp);
	switch (in->rm) {
	case 0:  a = (i16)(m->r[R_BX] + m->r[R_SI]); break;
	case 1:  a = (i16)(m->r[R_BX] + m->r[R_DI]); break;
	case 2:  a = (i16)(m->r[R_BP] + m->r[R_SI]); break;
	case 3:  a = (i16)(m->r[R_BP] + m->r[R_DI]); break;
	case 4:  a = m->r[R_SI]; break;
	case 5:  a = m->r[R_DI]; break;
	case 6:  a = m->r[R_BP]; break;
	default: a = m->r[R_BX]; break;
	}
	return ((i16)(a + in->disp));
}

/* Read and write the mod r/m operand.  `e' is the effective address,
 * already computed once by the caller so that a read-modify-write does
 * not form it twice (and so that a side effect in the address, were
 * there ever one, could not happen twice). */
static i16 rmrd(m, in, e)
struct i86 *m;
struct i86in *in;
i16 e;
{
	if (in->mod == 3)
		return (in->w ? m->r[in->rm] : (i16)getb(m, in->rm));
	return (in->w ? mrw(m, in->seg, e) : (i16)mrb(m, in->seg, e));
}

static rmwr(m, in, e, v)
struct i86 *m;
struct i86in *in;
i16 e, v;
{
	if (in->mod == 3) {
		if (in->w)
			m->r[in->rm] = v;
		else
			setb(m, in->rm, v & 0xff);
	} else if (in->w)
		mww(m, in->seg, e, v);
	else
		mwb(m, in->seg, e, v & 0xff);
	return (0);
}

/* The reg-field operand, which is always a register. */
static i16 rgrd(m, in)
struct i86 *m;
struct i86in *in;
{
	return (in->w ? m->r[in->reg] : (i16)getb(m, in->reg));
}

static rgwr(m, in, v)
struct i86 *m;
struct i86in *in;
i16 v;
{
	if (in->w)
		m->r[in->reg] = v;
	else
		setb(m, in->reg, v & 0xff);
	return (0);
}

/* ------------------------------------------------------------------ */
/* the stack							       */

static push(m, v)
struct i86 *m;
i16 v;
{
	m->r[R_SP] = (i16)(m->r[R_SP] - 2);
	mww(m, S_SS, m->r[R_SP], v);
	return (0);
}

static i16 pop(m)
struct i86 *m;
{
	register i16 v;

	v = mrw(m, S_SS, m->r[R_SP]);
	m->r[R_SP] = (i16)(m->r[R_SP] + 2);
	return (v);
}

/* ------------------------------------------------------------------ */
/* condition codes						       */

/* The sixteen 8086 conditions in encoding order, so the decoder can
 * hand the low four opcode bits straight through. */
static int cond(m, cc)
struct i86 *m;
int cc;
{
	register i16 f;
	register int t;

	f = i86flags(m);
	switch (cc >> 1) {
	case 0: t = (f & F_OF) != 0; break;			/* O	*/
	case 1: t = (f & F_CF) != 0; break;			/* B	*/
	case 2: t = (f & F_ZF) != 0; break;			/* E	*/
	case 3: t = (f & (F_CF | F_ZF)) != 0; break;		/* BE	*/
	case 4: t = (f & F_SF) != 0; break;			/* S	*/
	case 5: t = (f & F_PF) != 0; break;			/* P	*/
	case 6: t = ((f & F_SF) != 0) != ((f & F_OF) != 0); break;   /* L */
	default: t = (((f & F_SF) != 0) != ((f & F_OF) != 0))
			|| (f & F_ZF) != 0; break;		/* LE	*/
	}
	return ((cc & 1) ? !t : t);
}

/* ------------------------------------------------------------------ */
/* the ALU eight						       */

/* Returns the result; records the lazy flags; the caller decides
 * whether to store it (CMP does not). */
static i16 alu(m, aop, w, a, b)
struct i86 *m;
int aop, w;
i16 a, b;
{
	register i16 r;
	register int c;

	switch (aop) {
	case 0:					/* ADD			*/
		r = (i16)(a + b);
		lazy(m, LZ_ADD, w, a, b, r, 0);
		break;
	case 1:					/* OR			*/
		r = (i16)(a | b);
		lazy(m, LZ_LOG, w, a, b, r, 0);
		break;
	case 2:					/* ADC			*/
		c = (i86flags(m) & F_CF) != 0;
		r = (i16)(a + b + c);
		lazy(m, LZ_ADD, w, a, b, r, c);
		break;
	case 3:					/* SBB			*/
		c = (i86flags(m) & F_CF) != 0;
		r = (i16)(a - b - c);
		lazy(m, LZ_SUB, w, a, b, r, c);
		break;
	case 4:					/* AND			*/
		r = (i16)(a & b);
		lazy(m, LZ_LOG, w, a, b, r, 0);
		break;
	case 6:					/* XOR			*/
		r = (i16)(a ^ b);
		lazy(m, LZ_LOG, w, a, b, r, 0);
		break;
	default:				/* SUB (5) and CMP (7)	*/
		r = (i16)(a - b);
		lazy(m, LZ_SUB, w, a, b, r, 0);
		break;
	}
	return (r);
}

/*
 * Shifts and rotates, one bit at a time.
 *
 * Iterative on purpose.  This is the slow C path, correctness is the
 * only thing it owes anyone, and the bit-at-a-time form is the one that
 * is obviously right about the carry -- which is the flag every closed
 * form gets wrong at count 0 and at count >= width.  Note also that the
 * 8086 does NOT mask the count to five bits: that arrived with the 186,
 * so a CL of 200 really is two hundred iterations here.
 *
 * A count of zero changes no flags at all, which is why the flag stores
 * are inside the guard.
 */
static i16 shift(m, sop, w, v, n)
struct i86 *m;
int sop, w, n;
i16 v;
{
	register i16 msb, msk;
	register int cf, i, b;
	i16 f, v0;

	msk = (i16)(w ? 0xffff : 0x00ff);
	msb = (i16)(w ? 0x8000 : 0x0080);
	if (n == 0)
		return (v);
	f = i86flags(m);
	cf = (f & F_CF) != 0;
	v0 = (i16)(v & msk);
	v = v0;
	for (i = 0; i < n; i++) {
		switch (sop) {
		case 0:				/* ROL			*/
			b = (v & msb) != 0;
			v = (i16)(((v << 1) | b) & msk);
			cf = b;
			break;
		case 1:				/* ROR			*/
			b = v & 1;
			v = (i16)(((v >> 1) | (b ? msb : 0)) & msk);
			cf = b;
			break;
		case 2:				/* RCL			*/
			b = (v & msb) != 0;
			v = (i16)(((v << 1) | cf) & msk);
			cf = b;
			break;
		case 3:				/* RCR			*/
			b = v & 1;
			v = (i16)(((v >> 1) | (cf ? msb : 0)) & msk);
			cf = b;
			break;
		case 4: case 6:			/* SHL, and SAL = SHL	*/
			cf = (v & msb) != 0;
			v = (i16)((v << 1) & msk);
			break;
		case 5:				/* SHR			*/
			cf = v & 1;
			v = (i16)((v >> 1) & (msk >> 1));
			break;
		default:			/* SAR			*/
			cf = v & 1;
			v = (i16)(((v >> 1) | (v & msb)) & msk);
			break;
		}
	}
	f = (i16)(f & ~F_LAZY);
	if (cf)
		f |= F_CF;
	/* OF is architecturally defined only for a count of one.  The
	 * same formula is applied for larger counts, which is what the
	 * part is observed to do and what any other choice would have to
	 * justify; nothing in the corpus reads OF after a multi-bit
	 * shift. */
	switch (sop) {
	case 0: case 2:				/* ROL, RCL		*/
		if (((v & msb) != 0) != cf)
			f |= F_OF;
		break;
	case 1: case 3:				/* ROR, RCR		*/
		if (((v & msb) != 0) != ((v & (msb >> 1)) != 0))
			f |= F_OF;
		break;
	case 5:					/* SHR: OF from the source */
		if (v0 & msb)
			f |= F_OF;
		break;
	case 7:					/* SAR: OF is always 0	*/
		break;
	default:				/* SHL/SAL		*/
		if (((v & msb) != 0) != cf)
			f |= F_OF;
		break;
	}
	if (sop >= 4) {			/* the shifts, not the rotates,	*/
		if ((i16)(v & msk) == 0)	/* set SF, ZF and PF	*/
			f |= F_ZF;
		if (v & msb)
			f |= F_SF;
		if (par8(v))
			f |= F_PF;
	} else
		f |= (i16)(m->fl & (F_SF | F_ZF | F_PF | F_AF));
	m->fl = f;
	m->lz = LZ_NONE;
	return (v);
}

/* ------------------------------------------------------------------ */
/* segment-register writes: K3's check				       */

static int setsr(m, s, par)
struct i86 *m;
int s;
i16 par;
{
	register char *b;
	register int i;
	i16 d;

	s &= 3;
	b = i86resolve(par);
	if (b != (char *)0) {			/* the fast path: a base	*/
		m->sr[s] = par;
		m->sb[s] = b;
		m->so[s] = 0;
		return (X_OK);
	}
	for (i = 0; i < i86nseg; i++) {
		if (par < i86spar[i])
			continue;
		d = (i16)(par - i86spar[i]);
		if (d >= CMD_MAXPAR)
			continue;
		i86nsegslow++;
		m->sr[s] = par;
		m->sb[s] = i86sbase[i];
		m->so[s] = (i16)(d * CMD_PARA);
		return (X_OK);
	}
	if (i86segnew) {
		b = (*i86segnew)(par);
		if (b != (char *)0) {
			i86nsegslow++;
			m->sr[s] = par;
			m->sb[s] = b;
			m->so[s] = 0;
			return (X_OK);
		}
	}
	i86nsegbad++;
	i86segbad = par;
	return (X_SEGESC);
}

static int wboot(m, seg, off)
struct i86 *m;
i16 seg, off;
{
	return (m->wset && off == 0 && seg == m->wseg);
}

/* ------------------------------------------------------------------ */
/* one instruction						       */

/*
 * i86step -- decode and execute the instruction at CS:IP.
 *
 * IP is advanced past the instruction BEFORE it executes, so that a
 * relative branch target (computed at decode time from the same base)
 * and a pushed return address agree with the hardware without either
 * having to know the instruction's length.  A refusal leaves IP where
 * it was, so the caller can report the address that could not run.
 */
int i86step(m, in)
struct i86 *m;
struct i86in *in;
{
	register i16 a, b, r;
	i16 e, ip0;
	i32 la, lb;
	long sa, sb;
	int rc, n;

	ip0 = m->ip;
	m->fault = 0;
	/* A code segment the slow path had to bias is refused before a
	 * byte of it is decoded, and this is not laziness.  i86dec()
	 * addresses its bytes as cs[(i16)(ip + k)] -- it wraps at the
	 * guest's 64 KB, which is right -- so handing it a base partway
	 * into a host segment lets a decode at the top of the guest's
	 * segment read past the end of the host's.  There is no correct
	 * answer available: the bytes the guest means are in another
	 * segment.  Stage one refuses; the alternative is reading memory
	 * that belongs to something else.  No DRI file reaches here. */
	if (m->so[S_CS]) {
		m->fault = 1;
		m->fseg = S_CS;
		m->foff = m->ip;
		return (X_WINDOW);
	}
	i86dec(m->sb[S_CS], m->ip, in);
	if (in->op == I_BAD)
		return (X_BAD);
	i86ninsn++;
	m->ip = (i16)(m->ip + in->len);
	e = (i16)((in->fl & IN_MEM) ? ea(m, in) : 0);

	switch (in->op) {

	case I_ALU:
		if (in->fl & IN_IMM) {
			a = rmrd(m, in, e);
			b = in->imm;
		} else if (in->fl & IN_DIR) {
			a = rgrd(m, in);
			b = rmrd(m, in, e);
		} else {
			a = rmrd(m, in, e);
			b = rgrd(m, in);
		}
		r = alu(m, in->x, in->w, a, b);
		if (in->x == 7)			/* CMP stores nothing	*/
			break;
		if (in->fl & IN_DIR)
			rgwr(m, in, r);
		else
			rmwr(m, in, e, r);
		break;

	case I_TEST:
		a = rmrd(m, in, e);
		b = (i16)((in->fl & IN_IMM) ? in->imm : rgrd(m, in));
		alu(m, 4, in->w, a, b);		/* AND, result discarded */
		break;

	case I_MOV:
		if (in->fl & IN_IMM)
			rmwr(m, in, e, in->imm);
		else if (in->fl & IN_DIR)
			rgwr(m, in, rmrd(m, in, e));
		else
			rmwr(m, in, e, rgrd(m, in));
		break;

	case I_MOVSR:
		if (in->fl & IN_DIR) {		/* 8E: Sreg <- r/m	*/
			if (in->x == S_CS) {
				m->ip = ip0;		/* MOV CS,x is not
							 * an instruction */
				return (X_UNIMP);
			}
			rc = setsr(m, in->x, rmrd(m, in, e));
			if (rc != X_OK) {
				m->ip = ip0;
				return (rc);
			}
		} else				/* 8C: r/m <- Sreg	*/
			rmwr(m, in, e, m->sr[in->x]);
		break;

	case I_LEA:
		if (!(in->fl & IN_MEM))
			return (X_BAD);		/* LEA of a register	*/
		m->r[in->reg] = e;
		break;

	case I_LXS:
		if (!(in->fl & IN_MEM))
			return (X_BAD);
		a = mrw(m, in->seg, e);
		b = mrw(m, in->seg, (i16)(e + 2));
		rc = setsr(m, in->x, b);
		if (rc != X_OK) {
			m->ip = ip0;
			return (rc);
		}
		m->r[in->reg] = a;
		break;

	case I_XCHG:
		a = rmrd(m, in, e);
		b = rgrd(m, in);
		rmwr(m, in, e, b);
		rgwr(m, in, a);
		break;

	case I_INC:
		a = rmrd(m, in, e);
		r = (i16)(a + 1);
		lazy(m, LZ_INC, in->w, a, 1, r, 0);
		rmwr(m, in, e, r);
		break;
	case I_DEC:
		a = rmrd(m, in, e);
		r = (i16)(a - 1);
		lazy(m, LZ_DEC, in->w, a, 1, r, 0);
		rmwr(m, in, e, r);
		break;
	case I_NOT:				/* NOT affects no flags	*/
		rmwr(m, in, e, (i16)~rmrd(m, in, e));
		break;
	case I_NEG:
		b = rmrd(m, in, e);
		r = (i16)(0 - b);
		lazy(m, LZ_SUB, in->w, 0, b, r, 0);
		rmwr(m, in, e, r);
		break;

	case I_SHIFT:
		n = (int)(in->imm2 ? (m->r[R_CX] & 0xff) : 1);
		rmwr(m, in, e, shift(m, in->x, in->w, rmrd(m, in, e), n));
		break;

	case I_PUSH:
		/* PUSH SP on an 8086 pushes the value AFTER the
		 * decrement -- the 286 changed that, and period code
		 * that cares is code that would notice. */
		if (in->mod == 3 && in->rm == R_SP && !(in->fl & IN_MEM)) {
			m->r[R_SP] = (i16)(m->r[R_SP] - 2);
			mww(m, S_SS, m->r[R_SP], m->r[R_SP]);
		} else
			push(m, rmrd(m, in, e));
		break;
	case I_POP:
		r = pop(m);
		rmwr(m, in, e, r);
		break;
	case I_PUSHSR:
		push(m, m->sr[in->x]);
		break;
	case I_POPSR:
		if (in->x == S_CS) {
			m->ip = ip0;
			return (X_UNIMP);
		}
		a = mrw(m, S_SS, m->r[R_SP]);
		rc = setsr(m, in->x, a);
		if (rc != X_OK) {
			m->ip = ip0;
			return (rc);
		}
		m->r[R_SP] = (i16)(m->r[R_SP] + 2);
		break;
	case I_PUSHF:
		push(m, (i16)(i86flags(m) | F_ONES));
		break;
	case I_POPF:
		m->fl = (i16)(pop(m) | F_ONES);
		m->lz = LZ_NONE;
		break;

	case I_JMP:
		m->ip = in->disp;
		break;
	case I_JCC:
		if (cond(m, in->x))
			m->ip = in->disp;
		break;
	case I_LOOP:
		if (in->x == 3) {		/* JCXZ, which does not	*/
			if (m->r[R_CX] == 0)	/* decrement CX		*/
				m->ip = in->disp;
			break;
		}
		m->r[R_CX] = (i16)(m->r[R_CX] - 1);
		if (m->r[R_CX] == 0)
			break;
		if (in->x == 2				/* LOOP		*/
		 || (in->x == 1 && (i86flags(m) & F_ZF))	/* LOOPE	*/
		 || (in->x == 0 && !(i86flags(m) & F_ZF)))	/* LOOPNE */
			m->ip = in->disp;
		break;
	case I_CALL:
		push(m, m->ip);
		m->ip = in->disp;
		break;
	case I_RET:
		m->ip = pop(m);
		if (in->fl & IN_IMM)
			m->r[R_SP] = (i16)(m->r[R_SP] + in->imm);
		break;
	case I_JMPI:
		if (in->x) {			/* far, through memory	*/
			if (!(in->fl & IN_MEM))
				return (X_BAD);	/* no far JMP of a reg	*/
			a = mrw(m, in->seg, e);			/* offset */
			b = mrw(m, in->seg, (i16)(e + 2));	/* segment */
			if (wboot(m, b, a)) {
				m->ip = ip0;
				return (X_WBOOT);
			}
			rc = setsr(m, S_CS, b);
			if (rc != X_OK) {
				m->ip = ip0;
				return (rc);
			}
			m->ip = a;
			break;
		}
		m->ip = rmrd(m, in, e);
		break;
	case I_CALLI:
		if (in->x) {			/* far, through memory	*/
			if (!(in->fl & IN_MEM))
				return (X_BAD);
			a = mrw(m, in->seg, e);
			b = mrw(m, in->seg, (i16)(e + 2));
			if (wboot(m, b, a)) {
				m->ip = ip0;
				return (X_WBOOT);
			}
			r = m->sr[S_CS];	/* the return segment	*/
			rc = setsr(m, S_CS, b);
			if (rc != X_OK) {
				m->ip = ip0;
				return (rc);
			}
			push(m, r);
			push(m, m->ip);
			m->ip = a;
			break;
		}
		a = rmrd(m, in, e);
		push(m, m->ip);
		m->ip = a;
		break;
	case I_JMPF:				/* EA: far direct	*/
		if (wboot(m, in->imm2, in->imm)) {
			m->ip = ip0;
			return (X_WBOOT);
		}
		rc = setsr(m, S_CS, in->imm2);
		if (rc != X_OK) {
			m->ip = ip0;
			return (rc);
		}
		m->ip = in->imm;
		break;
	case I_CALLF:				/* 9A: far direct	*/
		if (wboot(m, in->imm2, in->imm)) {
			m->ip = ip0;
			return (X_WBOOT);
		}
		r = m->sr[S_CS];
		rc = setsr(m, S_CS, in->imm2);
		if (rc != X_OK) {
			m->ip = ip0;
			return (rc);
		}
		push(m, r);
		push(m, m->ip);
		m->ip = in->imm;
		break;
	case I_RETF:
		/* The frame is READ before it is popped, so that a warm
		 * boot or an unresolvable segment leaves SP where the
		 * instruction found it. */
		a = mrw(m, S_SS, m->r[R_SP]);			/* offset */
		b = mrw(m, S_SS, (i16)(m->r[R_SP] + 2));	/* segment */
		if (wboot(m, b, a)) {
			m->ip = ip0;
			return (X_WBOOT);
		}
		rc = setsr(m, S_CS, b);
		if (rc != X_OK) {
			m->ip = ip0;
			return (rc);
		}
		m->r[R_SP] = (i16)(m->r[R_SP] + 4);
		if (in->fl & IN_IMM)
			m->r[R_SP] = (i16)(m->r[R_SP] + in->imm);
		m->ip = a;
		break;

	case I_INT:
		i86intno = (int)(in->imm & 0xff);
		return (X_INT);

	case I_CBW:
		m->r[R_AX] = (i16)((m->r[R_AX] & 0x80) ? (m->r[R_AX] | 0xff00)
						       : (m->r[R_AX] & 0x00ff));
		break;
	case I_CWD:
		m->r[R_DX] = (i16)((m->r[R_AX] & 0x8000) ? 0xffff : 0);
		break;

	case I_LAHF:
		setb(m, 4, i86flags(m) & 0xff);		/* AH		*/
		break;
	case I_SAHF:
		i86flags(m);
		m->fl = (i16)((m->fl & 0xff00) | (getb(m, 4) & 0x00d5) | 0x02);
		break;

	case I_FLAG:
		i86flags(m);
		if (in->x == 0)
			m->fl = (i16)(m->fl & ~in->imm);
		else if (in->x == 1)
			m->fl = (i16)(m->fl | in->imm);
		else
			m->fl = (i16)(m->fl ^ in->imm);
		break;

	case I_XLAT:
		setb(m, 0, mrb(m, in->seg,
			(i16)(m->r[R_BX] + (getb(m, 0) & 0xff))));
		break;

	case I_MULDIV:
		/*
		 * The four widening forms.  Every intermediate is held
		 * as a `long' with no more than 32 bits of meaning, and
		 * every store back into a guest register goes through
		 * lo32() and an explicit mask -- see the sx8/sx16/sx32
		 * comment above for why a cast would not survive the
		 * move between a 16-bit-int target and a 64-bit host.
		 *
		 * Division truncates toward zero on the 8086.  C89
		 * leaves the sign of a negative quotient to the
		 * implementation; every compiler this has to run under
		 * truncates toward zero, and tests/i86test.c asserts it
		 * with -100/7 rather than assuming it.
		 */
		a = rmrd(m, in, e);
		i86flags(m);
		switch (in->x) {
		case 4:				/* MUL			*/
			if (in->w) {
				la = (i32)m->r[R_AX] * (i32)a;
				m->r[R_AX] = (i16)(la & 0xffffL);
				m->r[R_DX] = (i16)((la >> 16) & 0xffffL);
				b = m->r[R_DX];
			} else {
				la = (i32)(getb(m, 0) & 0xff)
				   * (i32)(a & 0xff);
				m->r[R_AX] = (i16)(la & 0xffffL);
				b = (i16)((la >> 8) & 0xff);
			}
			m->fl &= ~(F_CF | F_OF);
			if (b)			/* the high half is not	*/
				m->fl |= (F_CF | F_OF);	/* all zero	*/
			break;
		case 5:				/* IMUL			*/
			if (in->w) {
				sa = sx16(m->r[R_AX]) * sx16(a);
				la = lo32(sa);
				m->r[R_AX] = (i16)(la & 0xffffL);
				m->r[R_DX] = (i16)((la >> 16) & 0xffffL);
				sb = sx16(m->r[R_AX]);
			} else {
				sa = sx8(getb(m, 0)) * sx8((int)a);
				la = lo32(sa);
				m->r[R_AX] = (i16)(la & 0xffffL);
				sb = sx8(getb(m, 0));
			}
			m->fl &= ~(F_CF | F_OF);
			if (sa != sb)		/* the high half is not	*/
				m->fl |= (F_CF | F_OF);	/* a sign extension */
			break;
		case 6:				/* DIV			*/
			if (in->w) {
				if (a == 0)
					goto divzero;
				la = ((((i32)m->r[R_DX]) << 16)
				    | (i32)m->r[R_AX]) & 0xffffffffL;
				lb = la / (i32)a;
				if (lb > 0xffffL)
					goto divzero;
				m->r[R_DX] = (i16)(la % (i32)a);
				m->r[R_AX] = (i16)lb;
			} else {
				if ((a & 0xff) == 0)
					goto divzero;
				la = (i32)m->r[R_AX] & 0xffffL;
				lb = la / (i32)(a & 0xff);
				if (lb > 0xffL)
					goto divzero;
				setb(m, 4, (int)(la % (i32)(a & 0xff)));
				setb(m, 0, (int)lb);
			}
			break;
		default:			/* IDIV			*/
			if (in->w) {
				sb = sx16(a);
				if (sb == 0)
					goto divzero;
				sa = sx32(((((i32)m->r[R_DX]) << 16)
					 | (i32)m->r[R_AX]) & 0xffffffffL);
				if (sa / sb > 32767L || sa / sb < -32768L)
					goto divzero;
				m->r[R_DX] = (i16)(lo32(sa % sb) & 0xffffL);
				m->r[R_AX] = (i16)(lo32(sa / sb) & 0xffffL);
			} else {
				sb = sx8((int)a);
				if (sb == 0)
					goto divzero;
				sa = sx16(m->r[R_AX]);
				if (sa / sb > 127L || sa / sb < -128L)
					goto divzero;
				setb(m, 4, (int)(lo32(sa % sb) & 0xffL));
				setb(m, 0, (int)(lo32(sa / sb) & 0xffL));
			}
			break;
		}
		m->lz = LZ_NONE;
		break;

	divzero:
		/* A divide error is INT 0, and on an 8086 the pushed
		 * address is the one AFTER the divide -- but stage one
		 * has no interrupt machinery, so the caller is handed
		 * the vector with IP back at the instruction, which is
		 * the address a refusal has to be able to name. */
		m->ip = ip0;
		m->lz = LZ_NONE;
		i86intno = 0;
		return (X_INT);

	case I_DAA:
	case I_DAS:
		/* The two decimal adjusts, written out rather than
		 * mapped onto the Z8000's DAB, because DAB reads the
		 * host's D and H flags and this path has neither -- it
		 * has an 8086 AF it has just materialised. */
		i86flags(m);
		a = (i16)(getb(m, 0) & 0xff);
		r = a;
		b = (i16)(m->fl & F_CF);
		if ((a & 0x0f) > 9 || (m->fl & F_AF)) {
			r = (i16)(in->op == I_DAA ? (r + 6) : (r - 6));
			m->fl |= F_AF;
		} else
			m->fl &= ~F_AF;
		if (a > 0x99 || b) {
			r = (i16)(in->op == I_DAA ? (r + 0x60) : (r - 0x60));
			m->fl |= F_CF;
		} else
			m->fl &= ~F_CF;
		setb(m, 0, r & 0xff);
		m->fl &= ~(F_ZF | F_SF | F_PF);
		if ((r & 0xff) == 0)
			m->fl |= F_ZF;
		if (r & 0x80)
			m->fl |= F_SF;
		if (par8(r))
			m->fl |= F_PF;
		m->lz = LZ_NONE;
		break;

	case I_AAA:
	case I_AAS:
		i86flags(m);
		if ((getb(m, 0) & 0x0f) > 9 || (m->fl & F_AF)) {
			setb(m, 0, in->op == I_AAA ? (getb(m, 0) + 6)
						   : (getb(m, 0) - 6));
			setb(m, 4, in->op == I_AAA ? (getb(m, 4) + 1)
						   : (getb(m, 4) - 1));
			m->fl |= (F_AF | F_CF);
		} else
			m->fl &= ~(F_AF | F_CF);
		setb(m, 0, getb(m, 0) & 0x0f);
		m->lz = LZ_NONE;
		break;

	case I_AAM:
		if ((in->imm & 0xff) == 0) {
			m->ip = ip0;
			i86intno = 0;
			return (X_INT);
		}
		a = (i16)(getb(m, 0) & 0xff);
		setb(m, 4, (int)(a / (in->imm & 0xff)));
		setb(m, 0, (int)(a % (in->imm & 0xff)));
		lazy(m, LZ_LOG, 0, 0, 0, (i16)(getb(m, 0) & 0xff), 0);
		break;
	case I_AAD:
		a = (i16)((getb(m, 4) & 0xff) * (in->imm & 0xff)
			+ (getb(m, 0) & 0xff));
		setb(m, 0, a & 0xff);
		setb(m, 4, 0);
		lazy(m, LZ_LOG, 0, 0, 0, (i16)(a & 0xff), 0);
		break;

	case I_NOP:
		break;
	case I_HLT:
		m->halt = 1;
		m->ip = ip0;
		return (X_HALT);

	/* ---- decoded, deliberately not executed in stage one.  Each
	 * of these is a line in CPM86-STAGE-ONE.md §2.3's "no" column;
	 * refusing loudly is the whole point of decoding them. */
	case I_STRING:				/* no string/REP ops	*/
	case I_IRET: case I_INTO:		/* no interrupt frames	*/
	case I_ESC:				/* no 8087		*/
	case I_IO:				/* no PC hardware	*/
	case I_WAIT:
	default:
		m->ip = ip0;
		return (X_UNIMP);
	}
	/* A window fault is detected at the reference and reported here,
	 * so the instruction that caused it has already had whatever
	 * effect it had before the escaping byte.  That is honest for a
	 * refusal -- the guest is not resumed -- and it is why the fault
	 * carries the slot and offset that escaped rather than only a
	 * status. */
	if (m->fault) {
		m->ip = ip0;
		return (X_WINDOW);
	}
	return (X_OK);
}
