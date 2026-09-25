/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/* H19/Z19 console escape parser, with ESC '=' as ADM-3A addressing.
 * Serial output translates recognized sequences to ANSI; LR output writes
 * character cells and manages scrolling; HR does the same through the
 * graphics module's cell renderer. */
#include "romabi.h"

/*
 * `console serial' MUST REACH THE SERIAL LINE.
 *
 * The ROM's putchar/puts route by the ROM'S OWN flags (con_alt,
 * con_hires), not by anything CP/M decided.  On a machine whose ROM
 * believes it is a video console, every byte written through them lands
 * on the SCREEN however bi_console was set -- boot a `console serial'
 * medium and CP/M picks serial, the A> prompt works, and all of it comes
 * out on the LR display.
 *
 * So a serial console is written by us, straight to SCC channel B -- the
 * same line, and the same two registers, conpoll() in bios900.c already
 * READS for serial input.  Video consoles keep the ROM dispatcher, which
 * is correct for them.  Every byte crsr.c emits goes through crtty(), so
 * redefining putchar here covers the whole file at one point.
 *
 * CRSHOST is the host state-machine harness, which has no I/O ports and
 * supplies its own putchar; it keeps it.
 */
#ifndef CRSHOST
#undef	putchar
#define putchar(c)	crtty(c)

#define CRS_RR0		0x0101		/* SCC channel B: Rx/Tx status	*/
#define CRS_RR8		0x0111		/* SCC channel B: data		*/
#define CRS_TXEMPTY	0x04		/* RR0 D2: Tx buffer empty	*/
#define CRS_HRMODE	0x0404		/* HR mode: 2 mono, 1 colour	*/

extern int inb();
extern outb();
#endif

#define ESC	033
#define CAN	030

/* BvidCHR's 25 x 80; HR takes the renderer's */
static int nrow = 25, ncol = 80;
#define NROW	nrow
#define NCOL	ncol

/*
 * Screen access, as two macros so that the state machine below can be
 * compiled and driven on the host, which overrides both.
 *
 * A character cell is a 16-bit word at byte offset row*0xA0 + col*2 of
 * logical segment 0x3a; the ROM byte-writes the character at the cell's
 * EVEN address and the attribute byte survives (display_re.c:132-142).
 * Doing exactly what the ROM does is what keeps attributes intact.
 */
#ifndef VSET
#define VSET(off, ch)	vsetcell(off, ch)
#define VSETOWN
#endif
/*
 * A WORD store to the same cell, which is the only way to reach the
 * attribute: the card steers every byte write into the character lane, so
 * character and attribute can be set together or not at all.  The word is
 * attribute in the high byte, character in the low.
 */
#ifndef VSETW
#define VSETW(off, w)	vsetword(off, w)
#define VSETWOWN
#endif
#ifndef SCRST
#define SCRST		(*(long *)ROMV_SCRSTATE)
#endif
/*
 * Block move inside the character plane, for cellscroll().  The ROM's
 * own LDIRB thunk does 3,840 bytes in one instruction; its frame order
 * is (src, dst, len), which is what romabi.h's macro spells.  The host
 * harness overrides it with memmove.
 */
#ifndef VMOVE
#define VMOVE(doff, soff, n)	ldirb(0x3a000000L + (long)(soff), \
				      0x3a000000L + (long)(doff), (n))
#endif

#define CELLOFF(r, c)	((r) * 0xa0 + ((c) << 1))

/*
 * Cell attributes.  The low field is a video mode, one value at a time;
 * intensity and blink are flags on top of it.
 */
#define A_MODE		0x77
#define A_UNDERL	0x01
#define A_NORM		0x07
#define A_REVERSE	0x70
#define A_INTENSE	0x08
#define A_BLINK		0x80

/* console kind */
#define CK_SER	0
#define CK_LR	1
#define CK_HR	2

static int ckind;

/*
 * Tek 4010 graphics on the HR bitmap: GS or FS in ground state hands the
 * stream to the parser until it returns to alpha.  The parser runs in the
 * graphics module (crstek.c).
 */
extern tkinit(), tkput(), tkclear();

/* HR character cells, drawn by the graphics module */
extern hcinit(), hcram(), hcpark(), hcput(), hcblank(), hcscroll(), hcdelch();
extern gxcall();
extern int pgcur, concur;
extern int pconown();
static int hrow, hcol;			/* HR cursor			*/

#ifndef CRSHOST
/*
 * One character to the console CP/M chose.  Video keeps the ROM's
 * dispatcher; serial is driven here, with the '\n' -> CR+LF expansion ROM
 * putchar performed (romabi.h) and the bounded Tx wait conout() uses, so
 * a dead line costs a fixed number of INs and never wedges the boot.
 */
/*
 * IS THE ROM ITSELF ON THE SERIAL LINE?
 *
 * The ROM picks its console before we run and records it in two flags:
 * con_alt for the LR text console, con_hires for HR.  Both
 * clear means the ROM is talking to the SCC -- and therefore that the ROM
 * PROGRAMMED the SCC, baud and all.  Either set means the ROM is on a
 * video board and has no reason ever to have touched the serial channel.
 *
 * This is NOT the same question as `which console did CP/M choose'.  A
 * `console serial' medium makes CP/M choose serial (ckind == CK_SER) on a
 * machine whose ROM is still a video console, and then CP/M drives a
 * channel nobody initialised.  convid cannot
 * tell the two apart -- it is 0 for both -- so bios900.c asks here.
 */
crsromser()
{
	return (*(char *)ROMV_CONALT == 0 && *(char *)ROMV_CONHIRES == 0);
}

static crtty(c)
int c;
{
	register int i;

	if (ckind != CK_SER) {
		((int (*)())ROM_PUTCHAR)((int)(c));
		return;
	}
	if (c == '\n')
		crtty('\r');
	for (i = 0; i < 20000; i++)
		if (inb(CRS_RR0) & CRS_TXEMPTY)
			break;
	outb(CRS_RR8, c & 0xff);
}
#endif

/*
 * Parser state.  Four states, based on the donor handlers (mm.c's mmfunc
 * function-pointer machine, rec/mm.c:214-649), flattened to an int.
 */
#define ST_GND	0			/* ground				*/
#define ST_ESC	1			/* ESC seen				*/
#define ST_ROW	2			/* ESC Y / ESC = seen: want row		*/
#define ST_COL	3			/* row taken: want column		*/
#define ST_SKIP	4			/* eat one parameter byte		*/

static int state;
static int trow;			/* pending row (mm.c's trow)		*/
static int cattr;			/* attribute new cells are written with	*/
static int srow, scol;			/* ESC j / ESC k			*/

/*
 * LR only.  BvidCHR reads scr_state == 0 as "not initialised yet" and
 * clears the screen on the next character (display_re.c:110), so the
 * cursor may not simply be parked at row 0 column 0 -- the very next
 * character output would wipe the screen.  vpend records that the cursor
 * logically IS at (0,0) with scr_state left at the sentinel, and the next
 * character is then placed by hand.
 *
 * vheld is the deferred right margin.  A terminal that wraps as soon as
 * column 79 is written turns every exactly-80-column line into two, and
 * makes a program that legitimately paints the bottom-right cell scroll
 * the screen out from under itself.  So writing column 79 leaves the
 * cursor ON column 79 with vheld set, and the wrap happens only if
 * another printable character actually arrives.  Every cursor operation
 * goes through cellpark(), which clears it.
 */
static int vpend;
static int vheld;

extern sndbeep();

/* ---------------------------------------------------------------- serial */

static putdec(n)
int n;
{
	if (n >= 10)
		putchar('0' + n / 10);
	putchar('0' + n % 10);
}

static csi()
{
	putchar(ESC);
	putchar('[');
}

/* The whole attribute as one SGR, so the two paths cannot drift apart. */
static crsgr()
{
	csi();
	putchar('0');
	if ((cattr & A_MODE) == A_REVERSE) {
		putchar(';');
		putchar('7');
	}
	if ((cattr & A_MODE) == A_UNDERL) {
		putchar(';');
		putchar('4');
	}
	if (cattr & A_INTENSE) {
		putchar(';');
		putchar('1');
	}
	if (cattr & A_BLINK) {
		putchar(';');
		putchar('5');
	}
	putchar('m');
}

/* ------------------------------------------------------------------ video */

/*
 * Store one character cell.  The offset goes straight into the address
 * expression: the segment lives in the constant, the offset in the int,
 * and one ADDL puts them together.  Do NOT hoist the offset through a
 * long variable: that shape makes cc1 fold the constant away and store
 * into segment 0.
 */
#ifdef VSETOWN
static vsetcell(off, ch)
int off, ch;
{
	*(char *)(0x3a000000L + (long)off) = ch;
}
#endif

#ifdef VSETWOWN
static vsetword(off, w)
int off, w;
{
	*(unsigned *)(0x3a000000L + (long)off) = w;
}
#endif

/* BIOCOST-only direct store to cell (0,0), exposed through BIOS 101. */
vsettest(ch)
int ch;
{
	VSET(CELLOFF(0, 0), ch);
}

/*
 * Current cursor, from the ROM's own saved state.  Both halves are masked
 * rather than simply cast: int is 16 bits on the target and the cast alone
 * would do it, but the same source is compiled on the host, where int is
 * 32 bits and an unmasked cast would leave the row sitting in the column.
 */
static vrow()
{
	if (ckind == CK_HR)
		return (hrow);
	return (vpend ? 0 : (int)((SCRST >> 16) & 0xffffL));
}

static vcol()
{
	if (ckind == CK_HR)
		return (hcol);
	return (vpend ? 0 : (int)(SCRST & 0xffffL));
}

/* blank n cells starting at (r, c), wrapping by rows */
static vblank(r, c, n)
int r, c, n;
{
	if (ckind == CK_HR) {
		hcblank(r, c, n);
		return;
	}
	while (n-- > 0) {
		VSETW(CELLOFF(r, c), (A_NORM << 8) | ' ');
		if (++c >= NCOL) {
			c = 0;
			if (++r >= NROW)
				return;
		}
	}
}

/*
 * cellpark(r, c) -- park the LR cursor at (r, c), ONCE, after a run.
 * putchar(NUL) re-enters BvidCHR at its setcursor path
 * (display_re.c:130,170-183), which programs the 6845 cursor registers
 * from the state we just wrote -- so the hardware cursor follows without
 * this file touching the CRTC at all.
 */
static cellpark(r, c)
int r, c;
{
	vheld = 0;
	if (ckind == CK_HR) {
		hrow = r;
		hcol = c;
		hcpark(r, c);
		return;
	}
	if (r == 0 && c == 0) {
		SCRST = 0L;
		vpend = 1;
		return;
	}
	SCRST = ((long)r << 16) | (long)c;
	vpend = 0;
	putchar(0);
}

/*
 * cellput(r, c, s, n) -- store n character cells from s at (r, c).
 * One row only: the caller splits a run at the right margin, because it
 * is the caller that decides what happens there (wrap, scroll, clamp).
 * Each cell takes the current attribute with it.
 */
static cellput(r, c, s, n)
int r, c;
register char *s;
register int n;
{
	register int off, a;

	if (ckind == CK_HR) {
		hcput(r, c, s, n, cattr);
		return;
	}
	off = CELLOFF(r, c);
	a = (cattr & 0xff) << 8;
	while (n-- > 0) {
		VSETW(off, a | (*s++ & 0xff));
		off += 2;
	}
}

/*
 * cellins(r) -- open row r by pushing rows r..23 down one, and blank it.
 * Row by row from the bottom: the block move only copies upwards.
 */
static cellins(r)
int r;
{
	register int i;

	if (ckind == CK_HR) {
		hcscroll(r, NROW - 1, -1);
		return;
	}
	for (i = NROW - 1; i > r; i--)
		VMOVE(CELLOFF(i, 0), CELLOFF(i - 1, 0), 0xa0);
	vblank(r, 0, NCOL);
}

/*
 * cellscroll(top, bot, n) -- roll rows top..bot up by n, blanking the n
 * rows uncovered at the bottom.  The move is one LDIRB over whole cells,
 * so attributes travel with their characters.
 */
static cellscroll(top, bot, n)
int top, bot, n;
{
	register int rows;

	if (ckind == CK_HR) {
		hcscroll(top, bot, n);
		return;
	}
	rows = bot - top + 1 - n;
	if (rows > 0)
		VMOVE(CELLOFF(top, 0), CELLOFF(top + n, 0), rows * 0xa0);
	vblank(bot + 1 - n, 0, n * NCOL);
}

/* Write printable LR runs, splitting at row boundaries and scrolling as
 * needed. Update the hardware cursor once after the run. */
static lrput(s, n)
register char *s;
register int n;
{
	register int r, c, k;

	r = vrow();
	c = vcol();
	if (vheld)
		c = NCOL;		/* the margin was held: wrap now	*/
	while (n > 0) {
		if (c >= NCOL) {
			c = 0;
			if (++r >= NROW) {
				cellscroll(0, NROW - 1, 1);
				r = NROW - 1;
			}
		}
		k = NCOL - c;
		if (k > n)
			k = n;
		cellput(r, c, s, k);
		s += k;
		n -= k;
		c += k;
	}
	if (c >= NCOL) {
		cellpark(r, NCOL - 1);	/* clears vheld ... */
		vheld = 1;		/* ... so set it after */
		return;
	}
	cellpark(r, c);
}

/* CR: column 0 of the row we are on.  ROM putchar expands '\n' to CR+LF,
   so lrlf() does both halves and this is the bare CR. */
static lrcr()
{
	cellpark(vrow(), 0);
}

/* LF, with the scroll BvidCHR never had */
static lrlf()
{
	register int r;

	r = vrow() + 1;
	if (r >= NROW) {
		cellscroll(0, NROW - 1, 1);
		r = NROW - 1;
	}
	cellpark(r, 0);
}

/* BS: a held margin is spent by coming back onto column 79 */
static lrbs()
{
	register int c;

	c = vcol();
	if (!vheld && c > 0)
		--c;
	cellpark(vrow(), c);
}

/* -------------------------------------------------------------- operations */

/* absolute cursor address; out of range is ignored (mm.c:633-649) */
static crmove(r, c)
int r, c;
{
	if (r < 0 || r >= NROW || c < 0 || c >= NCOL)
		return;
	switch (ckind) {
	case CK_SER:
		csi();
		putdec(r + 1);
		putchar(';');
		putdec(c + 1);
		putchar('H');
		return;
	case CK_LR:
	case CK_HR:
		cellpark(r, c);
		return;
	}
}

/* one step in direction d ('A' up, 'B' down, 'C' right, 'D' left) */
static crstep(d)
int d;
{
	register int r, c;

	if (ckind == CK_SER) {
		csi();
		putchar(d);
		return;
	}
	r = vrow();
	c = vcol();
	switch (d) {
	case 'A':	if (r > 0) --r; break;
	case 'B':	if (r < NROW - 1) ++r; break;
	case 'C':	if (c < NCOL - 1) ++c; break;
	case 'D':	if (c > 0) --c; break;
	}
	cellpark(r, c);
}

/*
 * Clear the screen and home.  On LR the blanking is done here rather than
 * left to the ROM, because the ROM's own clear is driven by the same
 * scr_state == 0 sentinel vpend exists to keep out of the way of.
 */
static crclear()
{
	switch (ckind) {
	case CK_SER:
		csi();
		putchar('H');
		csi();
		putchar('2');
		putchar('J');
		return;
	default:
		vblank(0, 0, NROW * NCOL);
		cellpark(0, 0);
		return;
	}
}

/* home without clearing */
static crhome()
{
	switch (ckind) {
	case CK_SER:
		csi();
		putchar('H');
		return;
	default:
		cellpark(0, 0);
		return;
	}
}

/* erase, in the five spans the terminal defines (mm.c:410-424) */
#define ER_EOL	0			/* cursor to end of line	*/
#define ER_EOS	1			/* cursor to end of screen	*/
#define ER_BOS	2			/* start of screen to cursor	*/
#define ER_LINE	3			/* the whole line		*/
#define ER_BOL	4			/* start of line to cursor	*/

static crerase(span)
int span;
{
	register int r, c;

	if (ckind == CK_SER) {
		csi();
		switch (span) {
		case ER_EOS:	putchar('J'); return;
		case ER_BOS:	putchar('1'); putchar('J'); return;
		case ER_LINE:	putchar('2'); putchar('K'); return;
		case ER_BOL:	putchar('1'); putchar('K'); return;
		}
		putchar('K');
		return;
	}
	r = vrow();
	c = vcol();
	switch (span) {
	case ER_EOS:	vblank(r, c, (NROW - 1 - r) * NCOL + NCOL - c); return;
	case ER_BOS:	vblank(0, 0, r * NCOL + c + 1); return;
	case ER_LINE:	vblank(r, 0, NCOL); return;
	case ER_BOL:	vblank(r, 0, c + 1); return;
	}
	vblank(r, c, NCOL - c);
}

/* insert or delete one line at the cursor row; the cursor does not move */
static crline(ins)
int ins;
{
	register int r;

	if (ckind == CK_SER) {
		csi();
		putchar(ins ? 'L' : 'M');
		return;
	}
	r = vrow();
	if (ins)
		cellins(r);
	else
		cellscroll(r, NROW - 1, 1);
}

/* delete the character under the cursor, pulling the rest of the row left */
static crdelch()
{
	register int r, c;

	if (ckind == CK_SER) {
		csi();
		putchar('P');
		return;
	}
	r = vrow();
	c = vcol();
	if (ckind == CK_HR) {
		hcdelch(r, c);
		return;
	}
	if (c < NCOL - 1)
		VMOVE(CELLOFF(r, c), CELLOFF(r, c + 1), (NCOL - 1 - c) << 1);
	vblank(r, NCOL - 1, 1);
}

/* save and restore the cursor */
static crmark(rest)
int rest;
{
	if (ckind == CK_SER) {
		putchar(ESC);
		putchar(rest ? '8' : '7');
		return;
	}
	if (rest)
		cellpark(srow, scol);
	else {
		srow = vrow();
		scol = vcol();
	}
}

/* set the video mode field, or one of the flags on top of it */
static crmode(m)
int m;
{
	cattr = (cattr & ~A_MODE) | m;
	if (ckind == CK_SER)
		crsgr();
}

static crflag(on, bit)
int on, bit;
{
	if (on)
		cattr |= bit;
	else
		cattr &= ~bit;
	if (ckind == CK_SER)
		crsgr();
}

/* ------------------------------------------------------------ ground state */

/* BEL drives the speaker and reaches the terminal only on serial output.
 * Video handles CR/LF/BS/FF locally and writes other bytes as character
 * cells. */
static crput(c)
int c;
{
	char b;

	if (c == '\007') {
		sndbeep();
		if (ckind != CK_SER)
			return;
	}
	if (ckind != CK_SER) {
		switch (c) {
		case '\r':
			lrcr();
			return;
		case '\n':
			lrlf();
			return;
		case '\b':
			lrbs();
			return;
		case '\f':
			crclear();
			return;
		}
		b = c;
		lrput(&b, 1);
		return;
	}
	putchar(c);
}

/* BIOS 27 bulk output: batch printable LR characters in ground state.
 * Escapes, controls, and serial/HR output use the ordinary parser. */
crsrun(s, n)
register char *s;
register int n;
{
	register int k;

	while (n > 0) {
		if (ckind != CK_LR || state != ST_GND || (*s & 0xff) < ' ') {
			crsout(*s++);
			--n;
			continue;
		}
		for (k = 0; k < n && (s[k] & 0xff) >= ' '; k++)
			;
		lrput(s, k);
		s += k;
		n -= k;
	}
}

/*
 * ESC dispatch.  Three outcomes: performed, consumed, or -- for anything
 * this layer does not claim, ESC [ ... above all -- re-emitted literally so
 * a terminal's own sequences reach it intact (mm.c's mmescesc path).
 *
 * A sequence is consumed rather than re-emitted when the terminal we answer
 * as defines it and this console cannot do it.  Re-emitting spills its
 * parameter onto the screen as text, and on the far end of a serial line
 * several of them mean something else entirely.
 */
static crescape(c)
int c;
{
	switch (c) {
	case CAN:
		return;
	case 'A':
	case 'B':
	case 'C':
	case 'D':
		crstep(c);
		return;
	case 'E':
		crclear();
		return;
	case 'H':
		crhome();
		return;
	case 'J':
		crerase(ER_EOS);
		return;
	case 'K':
		crerase(ER_EOL);
		return;
	case 'b':
		crerase(ER_BOS);
		return;
	case 'l':
		crerase(ER_LINE);
		return;
	case 'o':
		crerase(ER_BOL);
		return;
	case 'L':
		crline(1);
		return;
	case 'M':
		crline(0);
		return;
	case 'N':
		crdelch();
		return;
	case 'j':
		crmark(0);
		return;
	case 'k':
		crmark(1);
		return;
	case 'p':
		crmode(A_REVERSE);
		return;
	case 'h':
		crmode(A_UNDERL);
		return;
	case 'q':			/* leave reverse video	*/
	case 'i':			/* leave underline	*/
		crmode(A_NORM);
		return;
	case 'c':
		crflag(1, A_BLINK);
		return;
	case 'd':
		crflag(0, A_BLINK);
		return;
	case 'e':
		crflag(1, A_INTENSE);
		return;
	case 'f':
		crflag(0, A_INTENSE);
		return;
	case '\f':			/* Tek erase		*/
		if (ckind != CK_HR)
			break;
		tkclear();
		crclear();
		return;
	case 'z':			/* power-up state	*/
		cattr = A_NORM;
		if (ckind == CK_SER)
			crsgr();
		crclear();
		return;
	case 'Y':			/* H19/VT52 direct addressing	*/
	case '=':			/* ADM-3A direct addressing	*/
		state = ST_ROW;
		return;
	case 'x':			/* set mode, one parameter	*/
	case 'y':			/* reset mode, one parameter	*/
		state = ST_SKIP;
		return;
	case '@':			/* insert-character mode	*/
	case 'O':
	case 'F':			/* graphics mode		*/
	case 'G':
	case '>':			/* keypad modes			*/
	case 't':
	case 'u':
	case 'v':			/* wrap mode			*/
	case 'w':
	case '1':			/* the 25th line		*/
	case '2':
	case '\\':			/* hold-screen mode		*/
	case 'n':			/* cursor and terminal reports	*/
	case 'Z':
	case '(':			/* half intensity, which on a	*/
	case ')':			/* serial terminal would swallow*/
	case '3':			/* the character after it	*/
	case '4':
		return;
	}
	crput(ESC);
	crput(c);
}

crsout(c)
int c;
{
	c &= 0xff;
	if (ckind == CK_HR && !tkput(c, state == ST_GND))
		return;
	switch (state) {
	case ST_ESC:
		state = ST_GND;
		crescape(c);
		return;
	case ST_ROW:
		state = ST_GND;
		if (c == CAN)
			return;
		trow = c - ' ';
		state = ST_COL;
		return;
	case ST_COL:
		state = ST_GND;
		if (c == CAN)
			return;
		crmove(trow, c - ' ');
		return;
	case ST_SKIP:
		state = ST_GND;
		return;
	}
	if (c == ESC) {
		state = ST_ESC;
		return;
	}
	crput(c);
}

/*
 * crsmode() is the seam the host-side state-machine test drives, which
 * is why the console decision is a separate function from the probe
 * below.
 */
crsmode(k)
int k;
{
	ckind = k;
	state = ST_GND;
	vpend = 0;
	vheld = 0;
	cattr = A_NORM;
	srow = 0;
	scol = 0;
	if (k == CK_HR)
		tkinit();
	return (k);
}

/*
 * crsreset() -- the warm boot's call: the escape parser back to ground, on
 * the console crsinit() chose.  The console is decided once, at cold boot.
 */
crsreset()
{
	if (ckind == CK_SER && cattr != A_NORM) {
		cattr = A_NORM;
		crsgr();		/* a program must not leave the	*/
	}				/* terminal in reverse video	*/
	return (crsmode(ckind));
}

#ifndef CRSHOST
#include "c900cfg.h"

/* The renderer's rows << 8 | columns, cursor home. */
static hrdim(n)
int n;
{
	nrow = n >> 8;
	ncol = n & 0xff;
	hrow = 0;
	hcol = 0;
}

static char hrbad[] = "HR mode 0? mono\r\n";

/* HR as the mode register has it: 2 mono, 1 colour, else mono and say so. */
hrinit()
{
	register int m;
	register char *p;

	m = inb(CRS_HRMODE) & 3;
	hrdim(hcinit(m));
	crsmode(CK_HR);
	if (m != 1 && m != 2) {
		hrbad[8] = '0' + m;
		for (p = hrbad; *p; )
			crsout(*p++);
	}
	return (CK_HR);
}

/* Run a NUL-terminated stream through the HR console on surface s. */
hrram(s, p)
char *s;
register char *p;
{
	register int k;

	k = ckind;
	hrdim(hcram(s));
	crsmode(CK_HR);
	while (*p)
		crsout(*p++);
	if (k == CK_HR)
		hrinit();
	else {
		nrow = 25;
		ncol = 80;
		crsmode(k);
	}
}

/*
 * One GSX call from the running process, on surface s or the HR screen.
 * The screen is console 0's, so only its owner, there, may open GSX.
 */
crsgsx(s, pb)
char *s, *pb;
{
	return (gxcall(s, pb, ckind == CK_HR, pgcur,
	    concur == 0 && pconown(0) == pgcur));
}
#endif
