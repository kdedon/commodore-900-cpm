/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/*
 * splitsc.c -- SC-trap emulator for the split-I/D loader shim.
 *
 * A 0xEE0B program runs natively in the code bank (the TPA segment) with
 * every data-space-referencing instruction's first word replaced by
 * SC #255 (word 0x7FFF) at load time (splitld.c).  On the trap, spemu()
 * looks the trapping PC up in the side table, re-decodes the original
 * instruction (zsplit.c) and performs its data access -- against the
 * data bank for static data and heap, against the code bank for the
 * stack region -- then resumes past the instruction.
 *
 * Bank routing.  In split I/D both the data segments and the stack live
 * in the 64K D address space; on this shim the stack must stay in the
 * CODE bank because CALL/RET/PUSH/POP reach it natively.  An effective
 * address is routed to the code bank when it is at or above the user
 * stack pointer at trap time (locals, caller frames, the base page and
 * command tail all sit at or above SP; statics and the brk heap sit
 * below it -- the DRI runtime's brk() keeps the heap strictly below the
 * stack).  A push-type write is routed by its post-decrement address
 * plus the operand width, i.e. by the pre-push SP.
 *
 * Emulated forms = every form the offline scan (host/splitchk.c) found
 * in the DRI dev-pack binaries, plus the cheap neighbors: LOAD/STORE/
 * STIMM/CLR, ALU read (ADD/SUB/OR/AND/XOR/CP), memory RMW (COM/NEG/
 * TSET/INC/DEC/SET/RES), flag reads (TEST/TESTL/BIT/CP-imm), EX, MULT/
 * MULTL/DIV/DIVL, LDM load/store, PUSH/PUSHL/POP/POPL/PUSH-immediate,
 * and the LDx/CPx block forms -- all of IR/DA/X/BA/BX, byte/word/long.
 * CPSx string compares and TRxB translates were not found in any binary
 * and return SPE_UNIMP (the gate panics loudly, never silently).
 *
 * Flag semantics mirror the Zilog data book (cross-checked against the
 * project emulator's ALU); host/splittest.c covers each form.
 *
 * Compiles for the target (MWC, int = 16) and the host (-DHOSTCC) with
 * identical behavior; memory is accessed bytewise big-endian.
 */

#include "zsplit.h"

/* FCW flag bits (low byte) */
#define FC	0x80
#define FZ	0x40
#define FS	0x20
#define FV	0x10	/* parity (byte logical) / overflow */
#define FD	0x08	/* decimal-adjust */
#define FH	0x04	/* half carry */

/* spemu() results */
#define SPE_OK		0	/* emulated; PC advanced		*/
#define SPE_NOTPAT	1	/* PC not in the side table (a genuine
				 * SC #255): caller takes the stock path */
#define SPE_UNIMP	2	/* form not emulated: caller panics	*/

/* The SC frame as scentry_ builds it (bdosglue.s): saved r0-r13, then
 * the hardware words.  The user's r14/r15 are banked in NSPSEG/NSPOFF;
 * the gate mirrors them through spufp/spusp around the call. */
struct spfr {
	zw	r[14];
	zw	id;
	zw	fcw;
	zw	pcseg;
	zw	pcoff;
};

/* Bank/table pointers, set by spscan() on the target (splitscan.c) and
 * by the test harness on the host.  Byte pointers; big-endian access
 * throughout.  These three are private to the module. */
char	*spcode;	/* code bank base (TPA text + stack + base page) */
char	*spdata;	/* data bank base				*/
zw	*sptab;		/* side table: original first word per text word */

/*
 * The next three are the module's shared state: sptop is read by the
 * resident assembly fast path on every trap and spusp/spufp are the
 * user r15/r14 mirrors the SC gate fills before entering here.  On the
 * target they live at fixed offsets in the module's interface header
 * (splitent.s) so that resident code can name them without being bound
 * to a linked module address; on the host there is no module, so this
 * file defines them itself.
 */
#ifdef HOSTCC
zw	sptop;		/* first text word offset NOT covered (bytes)	*/
zw	spusp;		/* user r15 (SP) at trap entry			*/
zw	spufp;		/* user r14					*/
#else
extern zw sptop;
extern zw spusp;
extern zw spufp;
#endif

static struct spfr *cf;	/* current frame				*/
static zw nfl, nfm;	/* flag accumulator: new flags, merge mask	*/

/* ------------------------------------------------------------------ */
/* registers							       */

static zw getw(n)
int n;
{
	n &= 15;
	if (n < 14)
		return (cf->r[n]);
	return (n == 14 ? spufp : spusp);
}

static setw(n, v)
int n;
zw v;
{
	n &= 15;
	if (n < 14)
		cf->r[n] = v;
	else if (n == 14)
		spufp = v;
	else
		spusp = v;
	return (0);
}

static int getb(n)
int n;
{
	register zw w;

	w = getw(n & 7);
	return ((n & 8) ? (w & 0xff) : ((w >> 8) & 0xff));
}

static setb(n, v)
int n, v;
{
	register zw w;

	w = getw(n & 7);
	if (n & 8)
		w = (w & 0xff00) | (v & 0xff);
	else
		w = (w & 0x00ff) | ((v & 0xff) << 8);
	setw(n & 7, w);
	return (0);
}

static long getl(n)
int n;
{
	n &= 14;
	return (((long)getw(n) << 16) | (long)getw(n + 1));
}

static setl(n, v)
int n;
long v;
{
	n &= 14;
	setw(n, (zw)((v >> 16) & 0xffff));
	setw(n + 1, (zw)(v & 0xffff));
	return (0);
}

/* ------------------------------------------------------------------ */
/* memory: bank routing + big-endian byte/word/long access	       */

/* Route an effective address: code bank iff it reaches the live stack
 * region.  `bias' is the operand width for push-type writes (the write
 * lands just below the pre-push SP) and 0 otherwise. */
static char *bnk(off, bias)
zw off;
int bias;
{
	if ((unsigned long)off + (unsigned long)bias >= (unsigned long)spusp)
		return (spcode);
	return (spdata);
}

#ifdef HOSTCC
/* the host is little-endian: compose big-endian bytes explicitly */
static int mrb(p, off)
char *p;
zw off;
{
	return (p[off] & 0xff);
}

static mwb(p, off, v)
char *p;
zw off;
int v;
{
	p[off] = (char)v;
	return (0);
}

static zw mrw(p, off)
char *p;
zw off;
{
	off &= ~1;
	return ((zw)(((zw)(p[off] & 0xff) << 8) | (zw)(p[off + 1] & 0xff)));
}

static mww(p, off, v)
char *p;
zw off, v;
{
	off &= ~1;
	p[off] = (char)(v >> 8);
	p[off + 1] = (char)v;
	return (0);
}
#else
/* the target is the Z8001 itself: native big-endian direct access */
#define mrb(p, off)	(*((p) + (zw)(off)) & 0xff)
#define mwb(p, off, v)	(*((p) + (zw)(off)) = (char)(v))
#define mrw(p, off)	(*(zw *)((p) + ((zw)(off) & ~1)))
#define mww(p, off, v)	(*(zw *)((p) + ((zw)(off) & ~1)) = (zw)(v))
#endif

/* read/write an operand of width wid at off, routed (bias for pushes) */
static long rmem(off, wid, bias)
zw off;
int wid, bias;
{
	register char *p;

	p = bnk(off, bias);
	if (wid == ZW_B)
		return ((long)mrb(p, off));
	if (wid == ZW_W)
		return ((long)mrw(p, off));
	return (((long)mrw(p, off) << 16) | (long)mrw(p, (zw)(off + 2)));
}

static wmem(off, wid, v, bias)
zw off;
int wid, bias;
long v;
{
	register char *p;

	p = bnk(off, bias);
	if (wid == ZW_B)
		mwb(p, off, (int)v);
	else if (wid == ZW_W)
		mww(p, off, (zw)v);
	else {
		mww(p, off, (zw)(v >> 16));
		mww(p, (zw)(off + 2), (zw)v);
	}
	return (0);
}

/* ------------------------------------------------------------------ */
/* flags							       */

static int par8(v)		/* 1 when parity of the byte is EVEN */
int v;
{
	register int n, i;

	n = 0;
	for (i = 0; i < 8; i++)
		if (v & (1 << i))
			n++;
	return ((n & 1) == 0);
}

/* Z/S(/P) for logical results; byte results include even parity in FV */
static lflags(v, wid)
long v;
int wid;
{
	nfl = 0;
	if (wid == ZW_B) {
		nfm = FZ | FS | FV;
		if ((v & 0xff) == 0) nfl |= FZ;
		if (v & 0x80) nfl |= FS;
		if (par8((int)(v & 0xff))) nfl |= FV;
	} else if (wid == ZW_W) {
		nfm = FZ | FS;
		if ((v & 0xffffL) == 0) nfl |= FZ;
		if (v & 0x8000L) nfl |= FS;
	} else {
		nfm = FZ | FS;
		if (v == 0) nfl |= FZ;
		if (v & 0x80000000L) nfl |= FS;
	}
	return (0);
}

/* a + b (issub = 0) or a - b (issub = 1); returns the result and loads
 * the C/Z/S/V (+ DA/H for byte) accumulator, exactly per the data book */
static long addsub(a, b, issub, wid)
long a, b;
int issub, wid;
{
	unsigned long ua, ub, ur;
	long r;

	nfl = 0;
	nfm = FC | FZ | FS | FV;
	if (wid == ZW_B) {
		nfm |= FD | FH;
		a &= 0xff; b &= 0xff;
		if (issub) {
			r = a - b;
			if (a < b) nfl |= FC;
			if ((a & 0x0f) < (b & 0x0f)) nfl |= FH;
			if (((a ^ b) & (a ^ r)) & 0x80) nfl |= FV;
			nfl |= FD;
		} else {
			r = a + b;
			if (r > 0xff) nfl |= FC;
			if ((a & 0x0f) + (b & 0x0f) > 0x0f) nfl |= FH;
			if ((~(a ^ b) & (a ^ r)) & 0x80) nfl |= FV;
		}
		r &= 0xff;
		if (r == 0) nfl |= FZ;
		if (r & 0x80) nfl |= FS;
		return (r);
	}
	if (wid == ZW_W) {
		a &= 0xffffL; b &= 0xffffL;
		if (issub) {
			r = a - b;
			if (a < b) nfl |= FC;
			if (((a ^ b) & (a ^ r)) & 0x8000L) nfl |= FV;
		} else {
			r = a + b;
			if (r > 0xffffL) nfl |= FC;
			if ((~(a ^ b) & (a ^ r)) & 0x8000L) nfl |= FV;
		}
		r &= 0xffffL;
		if (r == 0) nfl |= FZ;
		if (r & 0x8000L) nfl |= FS;
		return (r);
	}
	ua = (unsigned long)a; ub = (unsigned long)b;
	if (issub) {
		ur = (ua - ub) & 0xffffffffL;
		if (ua < ub) nfl |= FC;
		if (((ua ^ ub) & (ua ^ ur)) & 0x80000000L) nfl |= FV;
	} else {
		ur = (ua + ub) & 0xffffffffL;
		if (ur < ua) nfl |= FC;
		if ((~(ua ^ ub) & (ua ^ ur)) & 0x80000000L) nfl |= FV;
	}
	if (ur == 0) nfl |= FZ;
	if (ur & 0x80000000L) nfl |= FS;
	return ((long)ur);
}

/* condition-code evaluation over the accumulated CP flags in nfl */
static int cctrue(cc)
int cc;
{
	register int c, z, s, v;

	c = (nfl & FC) != 0; z = (nfl & FZ) != 0;
	s = (nfl & FS) != 0; v = (nfl & FV) != 0;
	switch (cc & 15) {
	case 0x0: return (0);
	case 0x1: return (s != v);		/* LT */
	case 0x2: return (s != v || z);		/* LE */
	case 0x3: return (c || z);		/* ULE */
	case 0x4: return (v);			/* OV/PE */
	case 0x5: return (s);			/* MI */
	case 0x6: return (z);			/* EQ */
	case 0x7: return (c);			/* ULT */
	case 0x8: return (1);			/* T */
	case 0x9: return (s == v);		/* GE */
	case 0xA: return (s == v && !z);	/* GT */
	case 0xB: return (!c && !z);		/* UGT */
	case 0xC: return (!v);			/* NOV/PO */
	case 0xD: return (!s);			/* PL */
	case 0xE: return (!z);			/* NE */
	default:  return (!c);			/* UGE */
	}
}

/* ------------------------------------------------------------------ */
/* 64-bit helpers for MULTL/DIVL (represented as hi/lo unsigned longs) */

static unsigned long m64h, m64l;	/* result of umul64 / udiv64 */
static unsigned long d64r;		/* remainder from udiv64 */

/* 32 x 32 -> 64 unsigned multiply */
static umul64(a, b)
unsigned long a, b;
{
	unsigned long ah, al, bh, bl, p0, p1, p2, p3, mid, lo;

	ah = (a >> 16) & 0xffffL; al = a & 0xffffL;
	bh = (b >> 16) & 0xffffL; bl = b & 0xffffL;
	p0 = al * bl;
	p1 = al * bh;
	p2 = ah * bl;
	p3 = ah * bh;
	mid = (p0 >> 16) + (p1 & 0xffffL) + (p2 & 0xffffL);
	lo = ((mid & 0xffffL) << 16) | (p0 & 0xffffL);
	m64h = p3 + (p1 >> 16) + (p2 >> 16) + (mid >> 16);
	m64l = lo & 0xffffffffL;
	m64h &= 0xffffffffL;
	return (0);
}

/* (hi:lo) / dv -> 64-bit quotient (m64h:m64l), remainder d64r.
 * dv must be nonzero. */
static udiv64(hi, lo, dv)
unsigned long hi, lo, dv;
{
	unsigned long qh, rem, ql;
	int i, sub, hb;

	qh = hi / dv;
	rem = hi % dv;
	ql = 0;
	for (i = 31; i >= 0; i--) {
		hb = (rem & 0x80000000L) != 0;
		rem = ((rem << 1) | ((lo >> i) & 1)) & 0xffffffffL;
		sub = hb || rem >= dv;
		if (sub)
			rem = (rem - dv) & 0xffffffffL;
		ql = ((ql << 1) | (sub ? 1 : 0)) & 0xffffffffL;
	}
	m64h = qh;
	m64l = ql;
	d64r = rem;
	return (0);
}

/* ------------------------------------------------------------------ */

/* generic register read/write of `wid' at nibble n */
static long getr(n, wid)
int n, wid;
{
	if (wid == ZW_B) return ((long)getb(n));
	if (wid == ZW_W) return ((long)getw(n));
	return (getl(n));
}

static setr(n, wid, v)
int n, wid;
long v;
{
	if (wid == ZW_B) setb(n, (int)v);
	else if (wid == ZW_W) setw(n, (zw)v);
	else setl(n, v);
	return (0);
}

/*
 * Emulate the patched instruction at the trap frame's PC.  Returns
 * SPE_OK, SPE_NOTPAT or SPE_UNIMP (see above).
 */
int spemu(fx)
long fx;
{
	struct zid in;
	register zw base, ea;
	zw w0, w1, w2, aw;
	long m, a, v;
	int wsz, i;

	cf = (struct spfr *)fx;
	base = cf->pcoff - 2;		/* the SC word that trapped */
	if (base >= sptop)
		return (SPE_NOTPAT);
	w0 = sptab[base >> 1];
	if (w0 == 0)
		return (SPE_NOTPAT);
	w1 = mrw(spcode, (zw)(base + 2));

	/* ---- fast path: the dominant no-flag forms (plain loads,
	 * stores, immediate stores, clears, push-immediate -- 75-80% of
	 * all patch sites).  Each case mirrors its zdecode+general-path
	 * twin exactly; the host test suite exercises these same forms
	 * through this path. ---- */
#define GW(n)	((n) < 14 ? cf->r[n] : ((n) == 14 ? spufp : spusp))
#define BNK(o)	((zw)(o) >= spusp ? spcode : spdata)
	{
		register short hi, n2, n3, len;
		register zw ea2;
		register char *p;

		hi = (w0 >> 8) & 0xff;
		n2 = (w0 >> 4) & 15;
		n3 = w0 & 15;
		len = 0;
		switch (hi) {
		case 0x20: case 0x21:		/* LDB/LD Rd,@Rs */
			if (n2 == 0) break;
			ea2 = GW(n2); p = BNK(ea2);
			if (hi & 1) setw(n3, mrw(p, ea2));
			else setb(n3, mrb(p, ea2));
			len = 1; break;
		case 0x14:			/* LDL RRd,@Rs */
			if (n2 == 0) break;
			ea2 = GW(n2); p = BNK(ea2);
			setw(n3 & 14, mrw(p, ea2));
			setw((n3 & 14) + 1, mrw(p, (zw)(ea2 + 2)));
			len = 1; break;
		case 0x2E: case 0x2F:		/* LDB/LD @Rd,Rs */
			ea2 = GW(n2); p = BNK(ea2);
			if (hi & 1) mww(p, ea2, getw(n3));
			else mwb(p, ea2, getb(n3));
			len = 1; break;
		case 0x1D:			/* LDL @Rd,RRs */
			ea2 = GW(n2); p = BNK(ea2);
			mww(p, ea2, getw(n3 & 14));
			mww(p, (zw)(ea2 + 2), getw((n3 & 14) + 1));
			len = 1; break;
		case 0x60: case 0x61:		/* LDB/LD Rd,DA|X */
			ea2 = w1; if (n2) ea2 += GW(n2);
			p = BNK(ea2);
			if (hi & 1) setw(n3, mrw(p, ea2));
			else setb(n3, mrb(p, ea2));
			len = 2; break;
		case 0x54:			/* LDL RRd,DA|X */
			ea2 = w1; if (n2) ea2 += GW(n2);
			p = BNK(ea2);
			setw(n3 & 14, mrw(p, ea2));
			setw((n3 & 14) + 1, mrw(p, (zw)(ea2 + 2)));
			len = 2; break;
		case 0x6E: case 0x6F:		/* LDB/LD DA|X,Rs */
			ea2 = w1; if (n2) ea2 += GW(n2);
			p = BNK(ea2);
			if (hi & 1) mww(p, ea2, getw(n3));
			else mwb(p, ea2, getb(n3));
			len = 2; break;
		case 0x5D:			/* LDL DA|X,RRs */
			ea2 = w1; if (n2) ea2 += GW(n2);
			p = BNK(ea2);
			mww(p, ea2, getw(n3 & 14));
			mww(p, (zw)(ea2 + 2), getw((n3 & 14) + 1));
			len = 2; break;
		case 0x30: case 0x31:		/* LDB/LD Rd,Rs(#d) */
			if (n2 == 0) break;	/* LDR: never patched */
			ea2 = GW(n2) + w1; p = BNK(ea2);
			if (hi & 1) setw(n3, mrw(p, ea2));
			else setb(n3, mrb(p, ea2));
			len = 2; break;
		case 0x35:			/* LDL RRd,Rs(#d) */
			if (n2 == 0) break;
			ea2 = GW(n2) + w1; p = BNK(ea2);
			setw(n3 & 14, mrw(p, ea2));
			setw((n3 & 14) + 1, mrw(p, (zw)(ea2 + 2)));
			len = 2; break;
		case 0x32: case 0x33:		/* LDB/LD Rd(#d),Rs */
			if (n2 == 0) break;
			ea2 = GW(n2) + w1; p = BNK(ea2);
			if (hi & 1) mww(p, ea2, getw(n3));
			else mwb(p, ea2, getb(n3));
			len = 2; break;
		case 0x37:			/* LDL Rd(#d),RRs */
			if (n2 == 0) break;
			ea2 = GW(n2) + w1; p = BNK(ea2);
			mww(p, ea2, getw(n3 & 14));
			mww(p, (zw)(ea2 + 2), getw((n3 & 14) + 1));
			len = 2; break;
		case 0x70: case 0x71:		/* LDB/LD Rd,Rs(Rx) */
			ea2 = GW(n2) + GW((w1 >> 8) & 15); p = BNK(ea2);
			if (hi & 1) setw(n3, mrw(p, ea2));
			else setb(n3, mrb(p, ea2));
			len = 2; break;
		case 0x75:			/* LDL RRd,Rs(Rx) */
			ea2 = GW(n2) + GW((w1 >> 8) & 15); p = BNK(ea2);
			setw(n3 & 14, mrw(p, ea2));
			setw((n3 & 14) + 1, mrw(p, (zw)(ea2 + 2)));
			len = 2; break;
		case 0x72: case 0x73:		/* LDB/LD Rd(Rx),Rs */
			ea2 = GW(n2) + GW((w1 >> 8) & 15); p = BNK(ea2);
			if (hi & 1) mww(p, ea2, getw(n3));
			else mwb(p, ea2, getb(n3));
			len = 2; break;
		case 0x77:			/* LDL Rd(Rx),RRs */
			ea2 = GW(n2) + GW((w1 >> 8) & 15); p = BNK(ea2);
			mww(p, ea2, getw(n3 & 14));
			mww(p, (zw)(ea2 + 2), getw((n3 & 14) + 1));
			len = 2; break;
		case 0x0C: case 0x0D:		/* @Rd: imm store / clear /
						 * push-immediate */
			ea2 = GW(n2); p = BNK(ea2);
			if (n3 == 5) {
				if (hi & 1) mww(p, ea2, w1);
				else mwb(p, ea2, (w1 >> 8) & 0xff);
				len = 2;
			} else if (n3 == 8) {
				if (hi & 1) mww(p, ea2, 0);
				else mwb(p, ea2, 0);
				len = 1;
			} else if (n3 == 9 && hi == 0x0D) {
				ea2 = GW(n2) - 2;
				setw(n2, ea2);
				p = ((zw)(ea2 + 2) >= spusp) ? spcode
							     : spdata;
				mww(p, ea2, w1);
				len = 2;
			}
			break;
		case 0x4C: case 0x4D:		/* DA|X: imm store / clear */
			if (n3 == 5) {
				w2 = mrw(spcode, (zw)(base + 4));
				ea2 = w1; if (n2) ea2 += GW(n2);
				p = BNK(ea2);
				if (hi & 1) mww(p, ea2, w2);
				else mwb(p, ea2, (w2 >> 8) & 0xff);
				len = 3;
			} else if (n3 == 8) {
				ea2 = w1; if (n2) ea2 += GW(n2);
				p = BNK(ea2);
				if (hi & 1) mww(p, ea2, 0);
				else mwb(p, ea2, 0);
				len = 2;
			}
			break;
		}
		if (len) {
			cf->pcoff = base + (len << 1);
			return (SPE_OK);
		}
	}

	w2 = mrw(spcode, (zw)(base + 4));
	zdecode(w0, w1, &in);
	if (in.klass != ZK_DATA)	/* side table corrupt: never valid */
		return (SPE_UNIMP);

	wsz = (in.width == ZW_B) ? 1 : (in.width == ZW_W) ? 2 : 4;
	nfl = 0; nfm = 0;

	/* effective address of the memory operand (LDM keeps its address
	 * in word 2; everything else in word 1) */
	aw = (in.op == ZOP_LDM || in.op == ZOP_STM) ? w2 : w1;
	switch (in.mode) {
	case ZM_IR:	ea = getw(in.rb); break;
	case ZM_DA:	ea = aw; break;
	case ZM_X:	ea = aw + getw(in.rb); break;
	case ZM_BA:	ea = getw(in.rb) + w1; break;
	case ZM_BX:	ea = getw(in.rb) + getw((w1 >> 8) & 15); break;
	default:	ea = 0; break;
	}

	switch (in.op) {

	case ZOP_LOAD:
		v = rmem(ea, in.width, 0);
		setr(in.ra, in.width, v);
		break;

	case ZOP_STORE:
		wmem(ea, in.width, getr(in.ra, in.width), 0);
		break;

	case ZOP_STIMM:
		/* immediate = last instruction word; byte imm in its
		 * high half */
		v = (in.len == 3) ? (long)w2 : (long)w1;
		if (in.width == ZW_B)
			v = (v >> 8) & 0xff;
		wmem(ea, in.width, v, 0);
		break;

	case ZOP_CLR:
		wmem(ea, in.width, 0L, 0);
		break;

	case ZOP_ALUR:
		m = rmem(ea, in.width, 0);
		a = getr(in.ra, in.width);
		switch (in.aop) {
		case A_ADD:
			v = addsub(a, m, 0, in.width);
			setr(in.ra, in.width, v);
			break;
		case A_SUB:
			v = addsub(a, m, 1, in.width);
			setr(in.ra, in.width, v);
			break;
		case A_CP:
			addsub(a, m, 1, in.width);
			break;
		case A_OR:
			v = a | m; lflags(v, in.width);
			setr(in.ra, in.width, v);
			break;
		case A_AND:
			v = a & m; lflags(v, in.width);
			setr(in.ra, in.width, v);
			break;
		case A_XOR:
			v = a ^ m; lflags(v, in.width);
			setr(in.ra, in.width, v);
			break;
		}
		break;

	case ZOP_ALUM:
		m = rmem(ea, in.width, 0);
		switch (in.aop) {
		case A_COM:
			v = ~m; lflags(v, in.width);
			wmem(ea, in.width, v, 0);
			break;
		case A_NEG:
			v = addsub(0L, m, 1, in.width);
			nfm = FC | FZ | FS | FV;	/* no DA/H */
			wmem(ea, in.width, v, 0);
			break;
		case A_TSET:
			nfl = 0; nfm = FS;
			if (m & ((in.width == ZW_B) ? 0x80L : 0x8000L))
				nfl |= FS;
			wmem(ea, in.width,
			     (in.width == ZW_B) ? 0xffL : 0xffffL, 0);
			break;
		case A_INC:
		case A_DEC:
			v = addsub(m, (long)(in.ra + 1),
				   in.aop == A_DEC, in.width);
			nfm = FZ | FS | FV;	/* INC/DEC: no carry */
			wmem(ea, in.width, v, 0);
			break;
		case A_SET:
			wmem(ea, in.width, m | (1L << in.ra), 0);
			break;
		case A_RES:
			wmem(ea, in.width, m & ~(1L << in.ra), 0);
			break;
		}
		break;

	case ZOP_TESTM:
		m = rmem(ea, in.width, 0);
		switch (in.aop) {
		case A_TEST:
			lflags(m, in.width);
			break;
		case A_BIT:
			nfl = 0; nfm = FZ;
			if ((m & (1L << in.ra)) == 0)
				nfl |= FZ;
			break;
		case A_CPIMM:
			v = (in.len == 3) ? (long)w2 : (long)w1;
			if (in.width == ZW_B)
				v = (v >> 8) & 0xff;
			addsub(m, v, 1, in.width);
			break;
		}
		break;

	case ZOP_EX:
		m = rmem(ea, in.width, 0);
		wmem(ea, in.width, getr(in.ra, in.width), 0);
		setr(in.ra, in.width, m);
		break;

	case ZOP_MUL:
		m = rmem(ea, in.width, 0);
		nfm = FC | FZ | FS | FV;
		nfl = 0;
		if (in.width == ZW_W) {
			a = (long)(short)getw(in.ra | 1);
			v = a * (long)(short)(zw)m;
			setl(in.ra & 14, v);
			if (v == 0) nfl |= FZ;
			if (v & 0x80000000L) nfl |= FS;
			if (v < -32768L || v > 32767L) nfl |= FC;
		} else {
			unsigned long ua, ub, rh, rl;
			int sgn;

			a = getl((in.ra & 12) + 2);
			sgn = 0;
			ua = (unsigned long)a;
			if (a < 0) { ua = (unsigned long)(-a); sgn ^= 1; }
			ub = (unsigned long)m;
			if (m < 0) { ub = (unsigned long)(-m); sgn ^= 1; }
			umul64(ua, ub);
			rh = m64h; rl = m64l;
			if (sgn) {		/* negate 64-bit */
				rl = (~rl + 1) & 0xffffffffL;
				rh = (~rh + (rl == 0 ? 1 : 0)) & 0xffffffffL;
			}
			setl(in.ra & 12, (long)rh);
			setl((in.ra & 12) + 2, (long)rl);
			if (rh == 0 && rl == 0) nfl |= FZ;
			if (rh & 0x80000000L) nfl |= FS;
			/* C: product does not fit a signed long */
			if (!(rh == 0 && (rl & 0x80000000L) == 0) &&
			    !(rh == 0xffffffffL && (rl & 0x80000000L)))
				nfl |= FC;
		}
		break;

	case ZOP_DIV:
		m = rmem(ea, in.width, 0);
		nfm = FC | FZ | FS | FV;
		nfl = 0;
		if (in.width == ZW_W) {
			long dd, q, r;
			short dv;

			dd = getl(in.ra & 14);
			dv = (short)(zw)m;
			if (dv == 0) {
				nfl = FZ | FV;
				break;
			}
			{
				unsigned long ud, uq, ur;
				unsigned long uv;
				int qn, rn;

				ud = (dd < 0) ? (unsigned long)(-dd)
					      : (unsigned long)dd;
				uv = (dv < 0) ? (unsigned long)(-(long)dv)
					      : (unsigned long)(long)dv;
				uq = ud / uv;
				ur = ud % uv;
				qn = (dd < 0) != (dv < 0);
				rn = (dd < 0);
				q = qn ? -(long)uq : (long)uq;
				r = rn ? -(long)ur : (long)ur;
				if (q >= -32768L && q <= 32767L) {
					setw((in.ra & 14) + 1, (zw)q);
					setw(in.ra & 14, (zw)r);
					if (q == 0) nfl |= FZ;
					if (q < 0) nfl |= FS;
				} else if (q >= -65536L && q <= 65535L) {
					setw((in.ra & 14) + 1, (zw)q);
					setw(in.ra & 14, (zw)r);
					nfl = FC | FV;
					if (q < 0) nfl |= FS;
					if ((zw)q == 0) nfl |= FZ;
				} else
					nfl = FV;
			}
		} else {
			unsigned long dh, dl, uv, uq, ur;
			long dv;
			int qn, rn, fit, fit2;

			dh = (unsigned long)getl(in.ra & 12);
			dl = (unsigned long)getl((in.ra & 12) + 2);
			dv = m;
			if (dv == 0) {
				nfl = FZ | FV;
				break;
			}
			qn = rn = 0;
			if (dh & 0x80000000L) {	/* negate 64-bit dividend */
				dl = (~dl + 1) & 0xffffffffL;
				dh = (~dh + (dl == 0 ? 1 : 0)) & 0xffffffffL;
				qn ^= 1; rn = 1;
			}
			uv = (unsigned long)dv;
			if (dv < 0) {
				uv = (unsigned long)(-dv);
				qn ^= 1;
			}
			udiv64(dh, dl, uv);
			uq = m64l; ur = d64r;
			/* fit = quotient within signed 32 bits;
			 * fit2 = within the "just out of range" band */
			fit = (m64h == 0 &&
			       ((uq & 0x80000000L) == 0 ||
				(qn && uq == 0x80000000L)));
			fit2 = (m64h == 0) || (qn && m64h == 1 && uq == 0);
			if (fit) {
				v = qn ? -(long)uq : (long)uq;
				setl((in.ra & 12) + 2, v);
				setl(in.ra & 12, rn ? -(long)ur : (long)ur);
				if (v == 0) nfl |= FZ;
				if (v < 0) nfl |= FS;
			} else if (fit2) {
				v = qn ? -(long)uq : (long)uq;
				setl((in.ra & 12) + 2, v);
				setl(in.ra & 12, rn ? -(long)ur : (long)ur);
				nfl = FC | FV;
				if (qn) nfl |= FS;
				if ((unsigned long)v == 0) nfl |= FZ;
			} else
				nfl = FV;
		}
		break;

	case ZOP_LDM:
	case ZOP_STM:
		{
			zw ew;
			int first, n;

			/* the register/count word is word 1 for both the
			 * IR and the DA/X forms (the address is word 2) */
			ew = w1;
			first = (ew >> 8) & 15;
			n = (ew & 15) + 1;
			for (i = 0; i < n; i++) {
				zw a2;

				a2 = ea + (i << 1);
				if (in.op == ZOP_LDM)
					setw(first + i,
					     (zw)rmem(a2, ZW_W, 0));
				else
					wmem(a2, ZW_W,
					     (long)getw(first + i), 0);
			}
		}
		break;

	case ZOP_PUSH:
		v = rmem(ea, in.width, 0);
		{
			zw sp;

			sp = getw(in.ra) - wsz;
			setw(in.ra, sp);
			wmem(sp, in.width, v, wsz);
		}
		break;

	case ZOP_PUSHI:
		{
			zw sp;

			sp = getw(in.ra) - 2;
			setw(in.ra, sp);
			wmem(sp, ZW_W, (long)w1, 2);
		}
		break;

	case ZOP_POP:
		{
			zw sp;

			sp = getw(in.ra);
			v = rmem(sp, in.width, 0);
			setw(in.ra, (zw)(sp + wsz));
			/* dst EA is computed after the SP update */
			switch (in.mode) {
			case ZM_IR: ea = getw(in.rb); break;
			case ZM_DA: ea = w1; break;
			case ZM_X:  ea = w1 + getw(in.rb); break;
			}
			wmem(ea, in.width, v, 0);
		}
		break;

	case ZOP_BLKT:		/* LDI/LDD(R): @dst <- @src */
		{
			int sr, dr, cr, dlt;
			zw cnt;

			sr = in.rb;
			dr = (w1 >> 4) & 15;
			cr = (w1 >> 8) & 15;
			dlt = (in.blk & ZB_DECR) ? -wsz : wsz;
			do {
				v = rmem(getw(sr), in.width, 0);
				wmem(getw(dr), in.width, v, 0);
				setw(sr, (zw)(getw(sr) + dlt));
				setw(dr, (zw)(getw(dr) + dlt));
				cnt = getw(cr) - 1;
				setw(cr, cnt);
			} while ((in.blk & ZB_REPT) && cnt != 0);
			nfm = FV;
			nfl = (cnt == 0) ? FV : 0;
		}
		break;

	case ZOP_BLKC:		/* CPI/CPD(R): reg vs @src, cc */
		{
			int sr, rr, cr, cc, dlt, match;
			zw cnt;

			sr = in.rb;
			rr = (w1 >> 4) & 15;
			cr = (w1 >> 8) & 15;
			cc = w1 & 15;
			dlt = (in.blk & ZB_DECR) ? -wsz : wsz;
			match = 0;
			do {
				m = rmem(getw(sr), in.width, 0);
				addsub(getr(rr, in.width), m, 1, in.width);
				match = cctrue(cc);
				setw(sr, (zw)(getw(sr) + dlt));
				cnt = getw(cr) - 1;
				setw(cr, cnt);
			} while ((in.blk & ZB_REPT) && cnt != 0 && !match);
			nfm = FC | FZ | FS | FV;
			nfl &= ~(FZ | FV);
			if (match) nfl |= FZ;
			if (cnt == 0) nfl |= FV;
		}
		break;

	default:		/* ZOP_BLKS / ZOP_TRANS: not present in
				 * any known 0xEE0B binary */
		return (SPE_UNIMP);
	}

	if (nfm)
		cf->fcw = (cf->fcw & ~nfm) | (nfl & nfm);
	cf->pcoff = base + (in.len << 1);
	return (SPE_OK);
}
