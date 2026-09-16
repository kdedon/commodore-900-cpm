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

#define putchar(c)	hostput(c)
#define VSET(off, ch)	(vram[off] = (ch))
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
			vram[r * 0xa0 + c * 2] = ch;
}

static int cell(r, c)
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

	reset(CK_LR);
	feed("\033&Q");

	/* CAN abandons a half-typed address */
	reset(CK_LR);
	feed("\033Y\030AB");

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
