#include "romabi.h"

#define ESC	033
#define CAN	030

#define NROW	25			/* BvidCHR: 25 rows, 0..24	*/
#define NCOL	80			/* BvidCHR: 80 columns		*/

/*
 * Screen access, as two macros so that the state machine below can be
 * compiled and driven on the host (host/crsrtest.c overrides both).
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

#define CELLOFF(r, c)	((r) * 0xa0 + ((c) << 1))

/* console kind */
#define CK_SER	0
#define CK_LR	1
#define CK_HR	2

static int ckind;

/*
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
 */
static int vpend;

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

vsettest(ch)
int ch;
{
	VSET(CELLOFF(0, 0), ch);
}

/*
 * Current cursor, from the ROM's own saved state.  Both halves are masked
 * rather than simply cast: int is 16 bits on the target and the cast alone
 * would do it, but the same source is compiled on the host
 * (host/crsrtest.c) where int is 32 bits and an unmasked cast would leave
 * the row sitting in the column.
 */
static vrow()
{
	return (vpend ? 0 : (int)((SCRST >> 16) & 0xffffL));
}

static vcol()
{
	return (vpend ? 0 : (int)(SCRST & 0xffffL));
}

/*
 */
int r, c;
{
	if (r == 0 && c == 0) {
		SCRST = 0L;
		vpend = 1;
		return;
	}
	SCRST = ((long)r << 16) | (long)c;
	vpend = 0;
	putchar(0);
}

{
	while (n-- > 0) {
			c = 0;
		}
	}
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

static crput(c)
int c;
{
	if (c == '\007') {
		sndbeep();
		if (ckind != CK_SER)
			return;
	}
		switch (c) {
		case '\r':
		case '\b':
		case '\f':
			crclear();
			return;
		}
		return;
	}
	putchar(c);
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
 * crsmode() is the seam the host-side state-machine test drives
 * (host/crsrtest.c), which is why the console decision is a separate
 */
crsmode(k)
int k;
{
	ckind = k;
	state = ST_GND;
	vpend = 0;
	return (k);
}

#ifndef CRSHOST
{
		return (crsmode(CK_LR));
		return (crsmode(CK_HR));
	return (crsmode(CK_SER));
}
#endif
