/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/*
 * crsrtest.c -- drive src/crsr.c on the host and check what it did.
 *
 * The emulator has no video card (its own README says so), so the LR
 * video backend cannot be exercised on the target at all: on the emulator
 * the ROM always selects the serial console.  This harness compiles the
 * SAME source with the two screen accessors redirected at arrays, so the
 * cell writes and the ROM cursor-state writes the LR backend performs are
 * observable and can be asserted by row and column.  It is a test of the
 * state machine and the video backend; `verify-crsr' also runs the serial
 * backend end to end on the emulator, which this cannot do.
 *
 * Build: cc -std=gnu89 (crsr.c is K&R, like everything else in the port).
 */

#include <stdio.h>
#include <string.h>

#define ROMABI_H		/* romabi.h is a target header: keep it out */
#define CRSHOST			/* ... so crsinit()'s ROM flag read goes too */

#define NR 25
#define NC 80

static char out[16384];		/* everything crsr.c handed to putchar     */
static int outn;
static char vram[NR * 0xa0];	/* the LR character SRAM, cell = row*0xA0 + col*2 */
static long scr;		/* the ROM's saved cursor (01:0614)        */
static int beeps;

static int hostput(c)
int c;
{
	if (outn < (int)sizeof(out))
		out[outn++] = c;
	return (c);
}

/* Cells are big-endian words: attribute at the even byte, character at the
   odd one.  A byte write reaches only the character. */
static hostsetw(off, w)
int off, w;
{
	vram[off] = w >> 8;
	vram[off + 1] = w;
}

#define putchar(c)	hostput(c)
#define VSET(off, ch)	(vram[(off) + 1] = (ch))
#define VSETW(off, w)	hostsetw(off, w)
#define VMOVE(d, s, n)	memmove(&vram[d], &vram[s], (size_t)(n))
#define SCRST		scr

#include "../src/bios/crsr.c"

sndbeep()
{
	beeps++;
}

/* ------------------------------------------------------------------ checks */

static int fails;

static fail(what)
char *what;
{
	printf("crsrtest: FAIL -- %s\n", what);
	fails++;
}

static ok(what)
char *what;
{
	printf("crsrtest: ok   -- %s\n", what);
}

static feed(s)
register char *s;
{
	while (*s != '\0')
		crsout(*s++);
}

static reset(kind)
int kind;
{
	memset(vram, 0, sizeof(vram));
	memset(out, 0, sizeof(out));
	outn = 0;
	scr = 0x00050005L;		/* somewhere that is not the sentinel */
	beeps = 0;
	crsmode(kind);
}

static fillscreen(ch)
int ch;
{
	int r, c;

	for (r = 0; r < NR; r++)
		for (c = 0; c < NC; c++)
			vram[r * 0xa0 + c * 2 + 1] = ch;
}

static fillrows()	/* row r gets the character 'A' + r */
{
	int r, c;

	for (r = 0; r < NR; r++)
		for (c = 0; c < NC; c++)
			vram[r * 0xa0 + c * 2 + 1] = 'A' + r;
}

static int cell(r, c)
int r, c;
{
	return (vram[r * 0xa0 + c * 2 + 1] & 0xff);
}

static int attr(r, c)
int r, c;
{
	return (vram[r * 0xa0 + c * 2] & 0xff);
}

static eqcell(r, c, want, what)
int r, c, want;
char *what;
{
	if (cell(r, c) != want) {
		printf("crsrtest: cell(%d,%d) = 0x%02x, want 0x%02x\n",
		       r, c, cell(r, c), want);
		fail(what);
	} else
		ok(what);
}

static eqattr(r, c, want, what)
int r, c, want;
char *what;
{
	if (attr(r, c) != want) {
		printf("crsrtest: attr(%d,%d) = 0x%02x, want 0x%02x\n",
		       r, c, attr(r, c), want);
		fail(what);
	} else
		ok(what);
}

static eqscr(want, what)
long want;
char *what;
{
	if (scr != want) {
		printf("crsrtest: scr = 0x%08lx, want 0x%08lx\n", scr, want);
		fail(what);
	} else
		ok(what);
}

static eqoutn(want, n, what)
char *want;
int n;
char *what;
{
	if (outn != n || memcmp(out, want, n) != 0) {
		printf("crsrtest: out = \"");
		{
			int i;
			for (i = 0; i < outn; i++)
				if (out[i] >= ' ' && out[i] < 0x7f)
					putc(out[i], stdout);
				else
					printf("\\%03o", out[i] & 0xff);
		}
		printf("\", want \"");
		{
			int j;
			for (j = 0; j < n; j++)
				if (want[j] >= ' ' && want[j] < 0x7f)
					putc(want[j], stdout);
				else
					printf("\\%03o", want[j] & 0xff);
		}
		printf("\"\n");
		fail(what);
	} else
		ok(what);
}

static eqout(want, what)
char *want, *what;
{
	eqoutn(want, (int)strlen(want), what);
}

static blankrun(r, c, n, what)
int r, c, n;
char *what;
{
	int i, rr = r, cc = c;

	for (i = 0; i < n; i++) {
		if (cell(rr, cc) != ' ') {
			printf("crsrtest: cell(%d,%d) = 0x%02x, want blank\n",
			       rr, cc, cell(rr, cc));
			fail(what);
			return;
		}
		if (++cc >= NC) {
			cc = 0;
			rr++;
		}
	}
	ok(what);
}

/* ------------------------------------------------------------------- video */

static lrtests()
{
	/* ESC Y: direct addressing writes the ROM's cursor cell and syncs
	   the 6845 with a NUL */
	reset(CK_LR);
	feed("\033Y%*");		/* row 0x25-0x20 = 5, col 0x2a-0x20 = 10 */
	eqscr(0x0005000aL, "LR: ESC Y 5 10 sets the ROM cursor state");
	eqoutn("\000", 1, "LR: ESC Y syncs the hardware cursor with NUL");

	/* ADM-3A spelling of the same thing */
	reset(CK_LR);
	feed("\033=')");		/* row 7, col 9 */
	eqscr(0x00070009L, "LR: ESC = 7 9 (ADM-3A) addresses the same way");

	/* out of range is ignored, as in the donor */
	reset(CK_LR);
	feed("\033Y9 ");		/* row 0x39-0x20 = 25: one past the last */
	eqscr(0x00050005L, "LR: an out-of-range row leaves the cursor alone");

	/* home may not leave the sentinel where the ROM would clear on it:
	   the next character is placed by hand instead */
	reset(CK_LR);
	feed("\033H");
	eqscr(0L, "LR: ESC H parks at the (0,0) sentinel");
	feed("X");
	eqcell(0, 0, 'X', "LR: the character after home lands at (0,0)");
	eqscr(1L, "LR: ... and the cursor moves on to (0,1)");
	eqoutn("\000", 1, "LR: ... without the ROM ever seeing the character");

	/* LF while parked at the sentinel advances a row */
	reset(CK_LR);
	feed("\033H\n");
	eqscr(0x00010000L, "LR: LF at the (0,0) sentinel moves to row 1");

	/* clear screen blanks every cell */
	reset(CK_LR);
	fillscreen('#');
	feed("\033E");
	blankrun(0, 0, NR * NC, "LR: ESC E blanks all 2000 cells");
	eqscr(0L, "LR: ESC E homes the cursor");

	/* erase to end of line stops at the right margin */
	reset(CK_LR);
	fillscreen('#');
	feed("\033Y#f\033K");		/* row 3, column 70 */
	blankrun(3, 70, 10, "LR: ESC K blanks to the right margin");
	eqcell(3, 69, '#', "LR: ESC K leaves the cell before the cursor");
	eqcell(4, 0, '#', "LR: ESC K does not run into the next row");

	/* erase to end of screen runs to the bottom right */
	reset(CK_LR);
	fillscreen('#');
	feed("\033Y\"%\033J");		/* row 2, col 5 */
	eqcell(2, 4, '#', "LR: ESC J leaves the cell before the cursor");
	blankrun(2, 5, (NR - 1 - 2) * NC + NC - 5, "LR: ESC J blanks to the end");

	/* the four motions, and their clamps */
	reset(CK_LR);
	feed("\033Y%*\033A");
	eqscr(0x0004000aL, "LR: ESC A steps up");
	feed("\033B\033B");
	eqscr(0x0006000aL, "LR: ESC B steps down");
	feed("\033D");
	eqscr(0x00060009L, "LR: ESC D steps left");
	feed("\033C\033C");
	eqscr(0x0006000bL, "LR: ESC C steps right");

	reset(CK_LR);
	feed("\033Y %\033A");		/* row 0, column 5, then try to go up */
	eqscr(0x00000005L, "LR: ESC A at the top row stays on the top row");

	reset(CK_LR);
	feed("\033Y8o\033B\033C");	/* row 24, col 79 */
	eqscr(0x0018004fL, "LR: the bottom-right corner clamps both ways");

	/* An unclaimed sequence still reaches the SCREEN as its two bytes.
	   It no longer reaches the ROM: this layer stores the cells itself,
	   which is what BvidCHR would have done with them anyway, so the
	   assertion moves from the putchar stream to the cells.  What DOES
	   reach the ROM is one NUL per cellpark(). */
	reset(CK_LR);
	feed("\033&Q");
	eqcell(5, 5, 033, "LR: an unclaimed escape reaches the screen ...");
	eqcell(5, 6, '&', "LR: ... with its argument ...");
	eqcell(5, 7, 'Q', "LR: ... and the text after it");
	eqscr(0x00050008L, "LR: ... leaving the cursor past all three");

	/* CAN abandons a half-typed address */
	reset(CK_LR);
	feed("\033Y\030AB");
	eqcell(5, 5, 'A', "LR: CAN abandons ESC Y before it moves anything");
	eqcell(5, 6, 'B', "LR: ... and the following text is text again");
	eqscr(0x00050007L, "LR: ... two columns on from where it started");

	/* CR and BS are this layer's arithmetic now, not the ROM's */
	reset(CK_LR);
	feed("\r");
	eqscr(0x00050000L, "LR: CR goes to column 0 of the same row");

	reset(CK_LR);
	feed("\b");
	eqscr(0x00050004L, "LR: BS steps back one column");

	reset(CK_LR);
	feed("\033Y% \b");		/* row 5, column 0 */
	eqscr(0x00050000L, "LR: BS at column 0 stays there");

	/* LF is CR+LF, as ROM putchar's own expansion was */
	reset(CK_LR);
	feed("\033Y%*\n");		/* row 5, column 10 */
	eqscr(0x00060000L, "LR: LF drops a row and returns to column 0");

	/* the right margin is HELD: writing column 79 does not wrap yet */
	reset(CK_LR);
	feed("\033Y n");		/* row 0, column 78 */
	feed("AB");
	eqcell(0, 78, 'A', "LR: the last two columns of a row fill ...");
	eqcell(0, 79, 'B', "LR: ... to column 79 ...");
	eqscr(0x0000004fL, "LR: ... and the cursor waits ON column 79");
	feed(" C");
	eqcell(1, 0, ' ', "LR: the next character wraps to the row below");
	eqcell(1, 1, 'C', "LR: ... and the one after it follows");
	eqscr(0x00010002L, "LR: ... with the cursor on row 1");

	/* wrapping off the bottom row scrolls the screen */
	reset(CK_LR);
	fillrows();
	feed("\033Y8n");		/* row 24, column 78 */
	feed("XYZ");
	eqcell(0, 0, 'B', "LR: a wrap off the bottom rolls row 1 up to row 0");
	eqcell(23, 0, 'Y', "LR: ... and row 24 up to row 23 ...");
	eqcell(23, 78, 'X', "LR: ... carrying what was just written there");
	eqcell(24, 0, 'Z', "LR: the wrapped character lands on the new row");
	blankrun(24, 1, NC - 1, "LR: ... and the rest of that row is blank");
	eqscr(0x00180001L, "LR: ... with the cursor after it");

	/* so does a LF on the bottom row */
	reset(CK_LR);
	fillrows();
	feed("\033Y8 \n");		/* row 24, column 0, then LF */
	eqcell(0, 0, 'B', "LR: LF on the bottom row scrolls instead of falling off");
	eqcell(23, 0, 'Y', "LR: ... rolling the old bottom row up");
	blankrun(24, 0, NC, "LR: ... and blanking the row it uncovered");
	eqscr(0x00180000L, "LR: ... leaving the cursor on the bottom row");

	/* a run is one cellput and ONE hardware-cursor park, not n of them */
	reset(CK_LR);
	crsrun("hello", 5);
	eqcell(5, 5, 'h', "LR: crsrun stores the run ...");
	eqcell(5, 9, 'o', "LR: ... all five characters of it ...");
	eqscr(0x0005000aL, "LR: ... and advances the column by five");
	eqoutn("\000", 1, "LR: ... parking the 6845 exactly once");

	/* a run broken by a control character is three parks, not five */
	reset(CK_LR);
	crsrun("ab\rcd", 5);
	eqcell(5, 5, 'a', "LR: crsrun splits a run at a control character");
	eqcell(5, 0, 'c', "LR: ... resumes after the CR it obeyed ...");
	eqcell(5, 1, 'd', "LR: ... and carries on");
	eqoutn("\000\000\000", 3, "LR: ... with one park per piece");

	/* attributes travel with the character into the cell's high byte */
	reset(CK_LR);
	feed("\033pA\033qB");
	eqcell(5, 5, 'A', "LR: ESC p writes the character ...");
	eqattr(5, 5, 0x70, "LR: ... in reverse video");
	eqattr(5, 6, 0x07, "LR: ESC q goes back to ordinary video");

	reset(CK_LR);
	feed("\033eA\033fB");
	eqattr(5, 5, 0x0f, "LR: ESC e adds intensity to the video mode");
	eqattr(5, 6, 0x07, "LR: ESC f takes it away again");

	reset(CK_LR);
	feed("\033pA\033zB");
	eqattr(0, 0, 0x07, "LR: ESC z is back to ordinary video ...");
	eqcell(0, 0, 'B', "LR: ... having cleared and homed");

	/* erasing always leaves ordinary video behind */
	reset(CK_LR);
	feed("\033p\033l");
	eqattr(5, 5, 0x07, "LR: an erased cell is ordinary video");

	/* insert line: rows below the cursor move down, the cursor's is blank */
	reset(CK_LR);
	fillrows();
	feed("\033Y%%\033L");		/* row 5, column 5 */
	eqcell(4, 0, 'E', "LR: ESC L leaves the rows above alone");
	blankrun(5, 0, NC, "LR: ... blanks the row it opened ...");
	eqcell(6, 0, 'F', "LR: ... pushes the old row down ...");
	eqcell(24, 0, 'X', "LR: ... and the bottom row with it");
	eqscr(0x00050005L, "LR: ... without moving the cursor");

	/* delete line: rows below move up, the bottom row is blank */
	reset(CK_LR);
	fillrows();
	feed("\033Y%%\033M");
	eqcell(4, 0, 'E', "LR: ESC M leaves the rows above alone");
	eqcell(5, 0, 'G', "LR: ... pulls the row below up ...");
	eqcell(23, 0, 'Y', "LR: ... and the bottom row with it ...");
	blankrun(24, 0, NC, "LR: ... blanking the row it uncovered");
	eqscr(0x00050005L, "LR: ... without moving the cursor");

	/* delete character: the rest of the row comes left */
	reset(CK_LR);
	fillscreen('#');
	feed("\033Y%%\033Nx");
	eqcell(5, 5, 'x', "LR: ESC N makes room at the cursor");
	eqcell(5, 6, '#', "LR: ... with the row still behind it");
	blankrun(5, 79, 1, "LR: ... and a blank pulled in at the margin");

	/* the three erases the H19 has and this console did not */
	reset(CK_LR);
	fillscreen('#');
	feed("\033Y#f\033o");		/* row 3, column 70 */
	blankrun(3, 0, 71, "LR: ESC o erases back to the left margin");
	eqcell(3, 71, '#', "LR: ... and no further");

	reset(CK_LR);
	fillscreen('#');
	feed("\033Y#f\033l");
	blankrun(3, 0, NC, "LR: ESC l erases the whole line");
	eqcell(2, 79, '#', "LR: ... and only that line");
	eqcell(4, 0, '#', "LR: ... from margin to margin");

	reset(CK_LR);
	fillscreen('#');
	feed("\033Y\"%\033b");		/* row 2, column 5 */
	blankrun(0, 0, 2 * NC + 6, "LR: ESC b erases back to the top left");
	eqcell(2, 6, '#', "LR: ... stopping after the cursor");

	/* save and restore the cursor */
	reset(CK_LR);
	feed("\033j\033Y\"%\033k");
	eqscr(0x00050005L, "LR: ESC k returns to where ESC j was");

	/* a sequence this console cannot do is eaten whole, parameter and
	   all, so none of it is blitted as text */
	reset(CK_LR);
	feed("\033x5X");
	eqcell(5, 5, 'X', "LR: ESC x eats its parameter byte ...");
	eqscr(0x00050006L, "LR: ... and nothing else reaches the screen");

	reset(CK_LR);
	feed("\033(\033)\033@\033FY");
	eqcell(5, 5, 'Y', "LR: an unimplemented sequence is eaten whole");
	eqscr(0x00050006L, "LR: ... leaving the cursor one column on");

	/* BEL is the speaker, and is not blitted on a video console */
	reset(CK_LR);
	feed("\007");
	if (beeps != 1)
		fail("LR: BEL does not reach the speaker");
	else
		ok("LR: BEL reaches the speaker");
	eqout("", "LR: BEL is not passed to the video ROM");
}

/* ------------------------------------------------------------------ serial */

static sertests()
{
	reset(CK_SER);
	feed("\033Y%*");
	eqout("\033[6;11H", "serial: ESC Y 5 10 becomes ANSI CUP, 1-relative");

	reset(CK_SER);
	feed("\033=')");
	eqout("\033[8;10H", "serial: ESC = 7 9 becomes the same CUP");

	reset(CK_SER);
	feed("\033Y8o");
	eqout("\033[25;80H", "serial: the bottom-right corner is two-digit");

	reset(CK_SER);
	feed("\033Y9 ");
	eqout("", "serial: an out-of-range address emits nothing");

	reset(CK_SER);
	feed("\033A\033B\033C\033D");
	eqout("\033[A\033[B\033[C\033[D", "serial: the four motions become CSI A-D");

	reset(CK_SER);
	feed("\033H");
	eqout("\033[H", "serial: ESC H becomes CUP home");

	reset(CK_SER);
	feed("\033E");
	eqout("\033[H\033[2J", "serial: ESC E homes and clears");

	reset(CK_SER);
	feed("\033J\033K");
	eqout("\033[J\033[K", "serial: ESC J/K become ED/EL");

	reset(CK_SER);
	feed("\033b\033l\033o");
	eqout("\033[1J\033[2K\033[1K", "serial: the three other erases become ANSI");

	/* ESC M is delete-line here and reverse index on the far end, so
	   passing it through would scroll the terminal instead */
	reset(CK_SER);
	feed("\033L\033M\033N");
	eqout("\033[L\033[M\033[P", "serial: insert, delete line and delete character");

	reset(CK_SER);
	feed("\033j\033k");
	eqout("\0337\0338", "serial: ESC j/k become DEC save and restore");

	reset(CK_SER);
	feed("\033p");
	eqout("\033[0;7m", "serial: ESC p becomes reverse video");

	reset(CK_SER);
	feed("\033p\033q");
	eqout("\033[0;7m\033[0m", "serial: ESC q takes it off again");

	reset(CK_SER);
	feed("\033h\033e\033c");
	eqout("\033[0;4m\033[0;4;1m\033[0;4;1;5m",
	      "serial: underline, intensity and blink accumulate");

	/* a program that exits in reverse video must not leave the terminal
	   in it */
	reset(CK_SER);
	feed("\033p");
	outn = 0;
	crsreset();
	eqout("\033[0m", "serial: the warm boot puts the attribute back");

	reset(CK_SER);
	feed("\033x5\033y5\033(\033)\033@\033F\0333\0334");
	eqout("", "serial: an unimplemented sequence emits nothing at all");

	reset(CK_SER);
	feed("\033[2J");
	eqout("\033[2J", "serial: an ANSI sequence from the program passes through");

	reset(CK_SER);
	feed("\033&Q");
	eqout("\033&Q", "serial: an unclaimed escape passes through");

	reset(CK_SER);
	feed("hi\r\n");
	eqout("hi\r\n", "serial: ordinary text is untouched");

	reset(CK_SER);
	feed("\007");
	if (beeps != 1)
		fail("serial: BEL does not reach the speaker");
	else
		ok("serial: BEL reaches the speaker");
	eqout("\007", "serial: BEL still reaches the terminal's own bell");
}

/* --------------------------------------------------------------------- HR */

static hrtests()
{
	/* clear: any scr_state whose segment is not 0x3a makes AvidCHR clear
	   both planes on entry, and the FF is handled after that clear */
	reset(CK_HR);
	feed("\033E");
	eqscr(0x3b000000L, "HR: ESC E arms the ROM's own two-plane clear");
	eqout("\014", "HR: ... and drives it with FF");

	/* home is the ROM's FF, which on the bitmap driver homes only */
	reset(CK_HR);
	feed("\033H");
	eqout("\014", "HR: ESC H is FF, which AvidCHR treats as home");

	/* addressing is not implemented on HR, but it must be CONSUMED --
	   the row/column bytes may not fall through and be blitted */
	reset(CK_HR);
	feed("\033Y%*Z");
	eqout("Z", "HR: an unsupported address is swallowed, not printed");

	/* nor may the attribute and editing sequences reach the blitter */
	reset(CK_HR);
	feed("\033p\033L\033x5\033(Z");
	eqout("Z", "HR: attribute and editing sequences are swallowed too");
}

int main(argc, argv)
int argc;
char *argv[];
{
	lrtests();
	sertests();
	hrtests();
	if (fails != 0) {
		printf("crsrtest: %d FAILED\n", fails);
		return (1);
	}
	printf("crsrtest: all checks passed\n");
	return (0);
}
