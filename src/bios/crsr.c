/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/* H19/Z19 console escape parser, with ESC '=' as ADM-3A addressing.
 * Serial output translates recognized sequences to ANSI; LR output writes
 * character cells and manages scrolling; HR uses the ROM bitmap driver
 * and supports only home/clear among the cursor operations. */
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
 * redefining the two macros here covers the whole file at one point.
 *
 * CRSHOST is the host state-machine harness, which has no I/O ports and
 * supplies its own putchar; it keeps both.
 */
#ifndef CRSHOST
#undef	putchar
#undef	puts
#define putchar(c)	crtty(c)
#define puts(s)		crputs(s)

#define CRS_RR0		0x0101		/* SCC channel B: Rx/Tx status	*/
#define CRS_RR8		0x0111		/* SCC channel B: data		*/
#define CRS_TXEMPTY	0x04		/* RR0 D2: Tx buffer empty	*/

extern int inb();
extern outb();
#endif

#define ESC	033
#define CAN	030

#define NROW	25			/* BvidCHR: 25 rows, 0..24	*/
#define NCOL	80			/* BvidCHR: 80 columns		*/

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

/* console kind */
#define CK_SER	0
#define CK_LR	1
#define CK_HR	2

static int ckind;

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

static crputs(s)
register char *s;
{
	if (ckind != CK_SER) {
		((int (*)())ROM_PUTS)((char *)(s));
		return;
	}
	while (*s != '\0')
		crtty(*s++);
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

static int state;
static int trow;			/* pending row (mm.c's trow)		*/

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
	return (vpend ? 0 : (int)((SCRST >> 16) & 0xffffL));
}

static vcol()
{
	return (vpend ? 0 : (int)(SCRST & 0xffffL));
}

/* blank n cells starting at (r, c), wrapping by rows */
static vblank(r, c, n)
int r, c, n;
{
	while (n-- > 0) {
		VSET(CELLOFF(r, c), ' ');
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
 * Only the even byte of each cell is written, so the attribute byte
 * survives -- the same property vsetcell() was written for.
 */
static cellput(r, c, s, n)
int r, c;
register char *s;
register int n;
{
	register int off;

	off = CELLOFF(r, c);
	while (n-- > 0) {
		VSET(off, *s++);
		off += 2;
	}
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
	if (ckind != CK_LR)
		return;
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
 * scr_state == 0 sentinel vpend exists to keep out of the way of.  On HR
 * it is the ROM's: any scr_state whose segment is not 0x3a makes AvidCHR
 * clear both bitmap planes on entry, and the FF that follows is handled
 * after the clear and homes the framebuffer pointer
 * (display_re.c:242-256,360-369).
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
	case CK_LR:
		vblank(0, 0, NROW * NCOL);
		cellpark(0, 0);
		return;
	case CK_HR:
		SCRST = 0x3b000000L;
		putchar('\f');
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
	case CK_LR:
		cellpark(0, 0);
		return;
	case CK_HR:
		putchar('\f');		/* AvidCHR FF homes, does not clear */
		return;
	}
}

/* erase: eos = 0 to end of line, 1 to end of screen (mm.c:410-424) */
static crerase(eos)
int eos;
{
	register int r, c;

	if (ckind == CK_SER) {
		csi();
		putchar(eos ? 'J' : 'K');
		return;
	}
	if (ckind != CK_LR)
		return;
	r = vrow();
	c = vcol();
	if (eos)
		vblank(r, c, (NROW - 1 - r) * NCOL + NCOL - c);
	else
		vblank(r, c, NCOL - c);
}

/* ------------------------------------------------------------ ground state */

/* BEL drives the speaker and reaches the terminal only on serial output.
 * LR handles CR/LF/BS/FF locally and writes other bytes as character cells. */
static crput(c)
int c;
{
	char b;

	if (c == '\007') {
		sndbeep();
		if (ckind != CK_SER)
			return;
	}
	if (ckind == CK_LR) {
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
 * ESC dispatch.  Anything not claimed here is re-emitted literally, which
 * is both the donor's behaviour (mm.c's mmescesc path) and what keeps a
 * serial terminal's own sequences -- ESC [ ... above all -- intact.
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
		crerase(1);
		return;
	case 'K':
		crerase(0);
		return;
	case 'Y':			/* H19/VT52 direct addressing	*/
	case '=':			/* ADM-3A direct addressing	*/
		state = ST_ROW;
		return;
	}
	crput(ESC);
	crput(c);
}

crsout(c)
int c;
{
	c &= 0xff;
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
	return (k);
}

/*
 * crsreset() -- the warm boot's call: the escape parser back to ground, on
 * the console crsinit() chose.  The console is decided once, at cold boot.
 */
crsreset()
{
	return (crsmode(ckind));
}

#ifndef CRSHOST
#include "c900cfg.h"
#include <bootinfo.h>

extern int mapseg();

/*
 * vprobe(base) -- is there framebuffer RAM at physical base<<8?
 *
 * COHERENT's vprobe: map a scratch segment onto the framebuffer's base,
 * save the word at offset 0, write and read back 0x55AA, then its
 * complement 0xAA55, and restore the word on either exit.  Both patterns
 * must survive.
 *
 * ONE ADDITION, for an undecoded bus.  COHERENT relies on undecoded reads
 * returning garbage.  If instead the bus
 * floated and held the last value driven onto it, a read straight after a
 * write would echo the pattern and pass.  So each write at offset 0 is
 * followed by a write of the OTHER pattern at offset 2 before offset 0 is
 * read back: an echo then returns the wrong pattern at both offsets and
 * fails.  Real framebuffer RAM has a word at offset 2 as well (a character
 * cell, or 16 bitmap pixels), and it is saved and restored the same way.
 */
static vprobe(base)
int base;
{
	register unsigned *p;
	unsigned s0, s2, r0, r2;
	int hit;

	mapseg(VPROBESEG, base, 0x02);
	p = (unsigned *)VPROBEADDR;
	s0 = p[0];
	s2 = p[1];
	p[0] = 0x55AA;
	p[1] = 0xAA55;
	r0 = p[0];
	r2 = p[1];
	hit = (r0 == 0x55AA && r2 == 0xAA55);
	if (hit) {
		p[0] = 0xAA55;
		p[1] = 0x55AA;
		r0 = p[0];
		r2 = p[1];
		hit = (r0 == 0xAA55 && r2 == 0x55AA);
	}
	p[0] = s0;
	p[1] = s2;
	mapseg(VPROBESEG, VPROBEHOME, 0x02);
	return (hit);
}

/*
 * crsinit(bicon) -- choose console 0, never from the ROM's
 * con_alt/con_hires flags.
 *
 *   BI_CON_SER			serial   } kboot DECIDED (its own probe, or
 *   BI_CON_LR			low-res  } the entry's `console serial'):
 *   BI_CON_HR			hi-res   } taken as sent, no probe here
 *
 *   BI_CON_ANY, BI_CON_VID,	FALLBACK: nobody decided, so probe HR,
 *   a value not known here,	then the text framebuffer, as COHERENT's
 *   no v3 handoff, or none	vidsel does; serial if
 *				neither answers.  This is how CP/M comes up
 *				from a loader that says nothing, or none.
 *
 * Returns the kind chosen: CK_SER is 0, so nonzero means video.
 */
crsinit(bicon)
int bicon;
{
	if (bicon == BI_CON_SER)
		return (crsmode(CK_SER));
	if (bicon == BI_CON_HR)
		return (crsmode(CK_HR));
	if (bicon == BI_CON_LR)
		return (crsmode(CK_LR));
	if (vprobe(VPHRBASE))
		return (crsmode(CK_HR));
	if (vprobe(VPLRBASE))
		return (crsmode(CK_LR));
	return (crsmode(CK_SER));
}
#endif
