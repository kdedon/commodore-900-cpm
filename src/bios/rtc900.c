/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/* MSM58321 RTC through Z-CIO #1, following Mark Williams' C900 driver
 * firmware/hd/extracted/src/frankh/src/oki/date.c (07/31/85) and the
 * MSM58321 datasheet. Register ports are 2*r+1.
 * PB0..3 are bidirectional data; PB4 READ, PB5 WRITE, PB6 address latch,
 * PB7 STOP, and PC1 active-low /CS. Addresses do not auto-increment.
 * Preserve PC3 (keyboard FIFO acknowledge). ROM keyboard polling leaves
 * /CS asserted, so every RTC transaction sets and releases it explicitly.
 * Port B is also printer data: a printer driver must serialize access.
 * D10 bits 3:2 encode (4-year%4)%4; H10 bit 3 selects 24-hour mode. */
#include "stdio.h"

extern int	inb();
extern		outb();
extern		mem_cpy();

#define P_MCCR		0x0003
#define P_PCDD		0x000d
#define P_PBDATA	0x001d
#define P_PCDATA	0x001f
#define P_PBMS		0x0051
#define P_PBDD		0x0057

#define PB_D		0x0f		/* D0..D3			*/
#define PB_READ		0x10
#define PB_WRITE	0x20
#define PB_ADWR		0x40
#define PB_STOP		0x80

#define PC_CS		0x02		/* PC1 = /CS, active low	*/
#define PC_DDR		0x05		/* PC0,PC2 in; PC1,PC3 out	*/
#define MCCR_PBE	0x80		/* Port B enable		*/

#define R_S1		0
#define R_S10		1
#define R_MI1		2
#define R_MI10		3
#define R_H1		4
#define R_H10		5
#define R_W		6
#define R_D1		7
#define R_D10		8
#define R_MO1		9
#define R_MO10		10
#define R_Y1		11
#define R_Y10		12
#define R_RESET		13
#define NTIMEREG	13		/* registers 0..12 hold time	*/

#define H10_24H		0x08
#define H10_PM		0x04

/* TOD block offsets -- the BIOS function 23 contract */
#define TOD_DATEHI	0
#define TOD_DATELO	1
#define TOD_HOUR	2
#define TOD_MIN		3
#define TOD_SEC		4
#define TODLEN		5

#define RTC_OK		0
#define RTC_NONE	0xff		/* no clock responding		*/

/* 1978-01-01 is day 1 and was a Sunday; the two-digit-year
 * window used by this driver ends in 2077. */
#define BASEYEAR	1978
#define WRAPYEAR	78		/* yy >= 78 is 19yy, else 20yy	*/

static UBYTE regs[NTIMEREG];		/* last raw register image	*/
static UBYTE alt[NTIMEREG];		/* second read, for agreement	*/
static UBYTE todbuf[TODLEN];

static UBYTE dpm[] = { 31,28,31,30,31,30,31,31,30,31,30,31 };
/* days before the 1st of month m in a non-leap year */
static UWORD cum[] = { 0,31,59,90,120,151,181,212,243,273,304,334 };

/*
 * 1978..2077 contains exactly one century year, 2000, and it is a leap
 * year, so year%4 is exact over the whole representable range.
 */
static isleap(y)
int y;
{
	return ((y & 3) == 0);
}

/*
 * Is `b' a packed BCD byte -- both nibbles a decimal digit?  Weighing a
 * byte as 10*hi + lo and range-checking the sum is not the same test:
 * 0x1a weighs 20, which passes an hour range check, so the bad byte is
 * accepted and normalised into one the caller never wrote.
 */
static bcdok(b)
int b;					/* int, not UBYTE: UBYTE is signed
					   char here, and a char parameter is
					   promoted anyway.  Both nibbles are
					   masked, so a sign-extended argument
					   reads the same as a widened one. */
{
	return (((b >> 4) & 0x0f) < 10 && (b & 0x0f) < 10);
}

static mlen(m, y)			/* days in month m (1..12) of y */
int m, y;
{
	if (m == 2 && isleap(y))
		return (29);
	return ((int)dpm[m - 1]);
}

/*
 * Calendar date -> CP/M 3 date word (days since 1977-12-31, 1978-01-01 =
 * 1).  y is the full year.  Everything is UWORD: the largest date this
 * driver accepts is 36525 (2077-12-31), so no long arithmetic -- and
 * therefore none of its runtime -- is needed anywhere in this driver.
 * The leap-day count in [1978,y) is (y-1)/4 - 494 because 1977/4 = 494.
 */
static UWORD ymd2day(y, m, d)
int y, m, d;
{
	register UWORD n;

	n = (UWORD)365 * (UWORD)(y - BASEYEAR);
	n += (UWORD)((y - 1) / 4 - 494);
	n += cum[m - 1];
	if (m > 2 && isleap(y))
		n++;
	return (n + (UWORD)d);
}

/*
 * CP/M 3 date word -> calendar date.  Returns 0 and leaves the output parameters
 * untouched if the day count is 0 (= "no date") or beyond 2077-12-31.
 */
static day2ymd(day, yp, mp, dp)
UWORD day;
int *yp, *mp, *dp;
{
	register UWORD n;
	register int y, m, len;

	if (day == 0)
		return (0);
	n = day;
	y = BASEYEAR;
	for (;;) {
		len = isleap(y) ? 366 : 365;
		if (n <= (UWORD)len)
			break;
		n -= (UWORD)len;
		if (++y > BASEYEAR + 99)
			return (0);
	}
	for (m = 1; n > (UWORD)(len = mlen(m, y)); m++)
		n -= (UWORD)len;
	*yp = y;
	*mp = m;
	*dp = (int)n;
	return (1);
}

/************************************************************************/
/*	Pin-level protocol						*/
/************************************************************************/

/*
 * Read-modify-write helpers.  Port C is read-modify-written on its low
 * nibble only so PC3 (the keyboard FIFO acknowledge level) survives.
 */
static pbset(v)
int v;
{
	outb(P_PBDATA, v);
}

static rtcsel()				/* assert /CS			*/
{
	outb(P_PCDATA, (inb(P_PCDATA) & 0x0f) & ~PC_CS);
}

static rtcdesel()			/* release /CS			*/
{
	outb(P_PCDATA, (inb(P_PCDATA) & 0x0f) | PC_CS);
}

/*
 * Latch a register address.  D0..D3 must already be outputs.  `hold' is
 * the level of the strobe-less bits (STOP in practice) to carry through.
 */
static rtcaddr(reg, hold)
int reg, hold;
{
	pbset(hold | (reg & PB_D));
	pbset(hold | (reg & PB_D) | PB_ADWR);
	pbset(hold | (reg & PB_D));
}

/*
 * Read one register.  PB0..PB3 are flipped to inputs for the READ pulse
 * and back to outputs afterwards, so the CIO never fights the chip.
 */
static rtcrd(reg, hold)
int reg, hold;
{
	register int v;

	rtcaddr(reg, hold);
	outb(P_PBDD, inb(P_PBDD) | PB_D);	/* D0..D3 = inputs	*/
	pbset(hold | PB_READ);
	v = inb(P_PBDATA) & PB_D;
	pbset(hold);
	outb(P_PBDD, inb(P_PBDD) & ~PB_D);	/* D0..D3 = outputs	*/
	return (v);
}

static rtcwr(reg, val, hold)
int reg, val, hold;
{
	rtcaddr(reg, hold);
	pbset(hold | (val & PB_D));
	pbset(hold | (val & PB_D) | PB_WRITE);
	pbset(hold | (val & PB_D));
}

/*
 * Bring the CIO side of the link up.  Idempotent; called from biosinit.
 * Port B goes to bit-port mode, all eight bits outputs, all strobes and
 * STOP low, and Port B is enabled in MCCR without disturbing the
 * keyboard's Port A / Port C enables.  PCDD gains PC1 as an output (see
 * the file header) and /CS is left released.
 */
rtcinit()
{
	outb(P_PBMS, 0);
	outb(P_PBDD, 0);			/* all Port B bits output */
	outb(P_PBDATA, 0);			/* strobes + STOP low	  */
	outb(P_MCCR, inb(P_MCCR) | MCCR_PBE);
	outb(P_PCDD, PC_DDR);
	rtcdesel();
}

/************************************************************************/
/*	Register image <-> time						*/
/************************************************************************/

static readregs(buf, hold)
UBYTE *buf;
int hold;
{
	register int i;

	for (i = 0; i < NTIMEREG; i++)
		buf[i] = (UBYTE)rtcrd(i, hold);
}

/* Validate BCD fields, hour mode, and calendar bounds before decoding.
 * Invalid images, including all-zero/all-one bus reads, leave tod untouched. */
static decode(buf, tod)
UBYTE *buf, *tod;
{
	register int h;
	int y, m, d;
	UWORD day;

	if (buf[R_S1] > 9 || buf[R_S10] > 5)
		return (0);
	if (buf[R_MI1] > 9 || buf[R_MI10] > 5)
		return (0);
	if (buf[R_H1] > 9 || buf[R_W] > 6)
		return (0);
	if (buf[R_D1] > 9 || buf[R_MO1] > 9 || buf[R_MO10] > 1)
		return (0);
	if (buf[R_Y1] > 9 || buf[R_Y10] > 9)
		return (0);

	h = (int)(buf[R_H10] & 0x03) * 10 + (int)buf[R_H1];
	if ((buf[R_H10] & H10_24H) == 0) {	/* 12-hour -> 24-hour	*/
		if (h < 1 || h > 12)
			return (0);
		if (h == 12)
			h = 0;
		if (buf[R_H10] & H10_PM)
			h += 12;
	} else if (h > 23)
		return (0);

	m = (int)buf[R_MO10] * 10 + (int)buf[R_MO1];
	if (m < 1 || m > 12)
		return (0);
	y = (int)buf[R_Y10] * 10 + (int)buf[R_Y1];
	y += (y >= WRAPYEAR) ? 1900 : 2000;
	d = (int)(buf[R_D10] & 0x03) * 10 + (int)buf[R_D1];
	if (d < 1 || d > mlen(m, y))
		return (0);

	day = ymd2day(y, m, d);
	tod[TOD_DATEHI] = (UBYTE)(day >> 8);
	tod[TOD_DATELO] = (UBYTE)(day & 0xff);
	tod[TOD_HOUR] = (UBYTE)(((h / 10) << 4) | (h % 10));
	tod[TOD_MIN] = (UBYTE)((buf[R_MI10] << 4) | buf[R_MI1]);
	tod[TOD_SEC] = (UBYTE)((buf[R_S10] << 4) | buf[R_S1]);
	return (1);
}

/************************************************************************/
/*	The two public operations					*/
/************************************************************************/

/* Read two complete register images until they agree (up to four tries).
 * BUSY is not wired, so this detects rollover without asserting STOP,
 * which would disturb the clock's subsecond divider. Returns RTC_OK/NONE. */
rtcget(tod)
UBYTE *tod;
{
	register int try, i;

	rtcsel();
	for (try = 0; try < 4; try++) {
		readregs(regs, 0);
		readregs(alt, 0);
		for (i = 0; i < NTIMEREG; i++)
			if (regs[i] != alt[i])
				break;
		if (i == NTIMEREG)
			break;
	}
	rtcdesel();
	if (try >= 4 || !decode(regs, tod))
		return (RTC_NONE);
	return (RTC_OK);
}

/*
 * Set the clock from `tod'.  Returns RTC_OK, or RTC_NONE if the chip did
 * not take the value, which is how a machine with no RTC reports
 * itself.
 *
 * STOP is held high across the reload, as the datasheet requires, and
 * register 13 (the post-stage reset) is written before STOP falls so the
 * first second starts a full second after the set, not at a random point
 * in the divider chain.  The seconds field is written too: CP/M 3 never
 * stores seconds on disk, but a program that reads the clock back
 * immediately should see what it set.
 */
rtcput(tod)
UBYTE *tod;
{
	register int i;
	int y, m, d, h, mi, s, wd;
	UWORD day;

	/* UBYTE is signed char in the DRI layer; mask date bytes before widening
	 * so a low byte >= 0x80 does not sign-extend into the date word. */
	day = ((UWORD)(tod[TOD_DATEHI] & 0xff) << 8)
	    | (UWORD)(tod[TOD_DATELO] & 0xff);
	if (!day2ymd(day, &y, &m, &d))
		return (RTC_NONE);
	/* The three time bytes are BCD, and a nibble above nine is not a
	 * number at all.  Weighing 10*hi + lo and range-checking the sum
	 * ACCEPTS such a byte and then stores the weighed value back as
	 * BCD, so hour 0x1a went in and 0x20 came out: the caller's bad
	 * value was silently replaced by a plausible one it never asked
	 * for.  Reject the nibble instead -- this is the last layer below
	 * BDOS function 104 that can see the caller's bytes, and DATE and
	 * SET are not the only callers.  (A nibble check does not make the
	 * range checks below redundant: 0x99 seconds is valid BCD.)  */
	if (!bcdok(tod[TOD_HOUR]) || !bcdok(tod[TOD_MIN])
	    || !bcdok(tod[TOD_SEC]))
		return (RTC_NONE);
	h = ((tod[TOD_HOUR] >> 4) & 0x0f) * 10 + (tod[TOD_HOUR] & 0x0f);
	mi = ((tod[TOD_MIN] >> 4) & 0x0f) * 10 + (tod[TOD_MIN] & 0x0f);
	s = ((tod[TOD_SEC] >> 4) & 0x0f) * 10 + (tod[TOD_SEC] & 0x0f);
	if (h > 23 || mi > 59 || s > 59)
		return (RTC_NONE);
	wd = (int)((day - 1) % 7);		/* day 1 = Sunday	*/

	regs[R_S1] = (UBYTE)(s % 10);
	regs[R_S10] = (UBYTE)(s / 10);
	regs[R_MI1] = (UBYTE)(mi % 10);
	regs[R_MI10] = (UBYTE)(mi / 10);
	regs[R_H1] = (UBYTE)(h % 10);
	regs[R_H10] = (UBYTE)((h / 10) | H10_24H);
	regs[R_W] = (UBYTE)wd;
	regs[R_D1] = (UBYTE)(d % 10);
	/* D10's leap-year selection is the datasheet's countdown code: 00
	 * for the leap year and 01/10/11 for a surplus of 3/2/1. Within
	 * one year the two readings cannot be told apart, so this follows
	 * the published code (it costs nothing to match the datasheet). */
	regs[R_D10] = (UBYTE)((d / 10) | (((4 - (y & 3)) & 3) << 2));
	regs[R_MO1] = (UBYTE)(m % 10);
	regs[R_MO10] = (UBYTE)(m / 10);
	regs[R_Y1] = (UBYTE)((y % 100) % 10);
	regs[R_Y10] = (UBYTE)((y % 100) / 10);

	rtcsel();
	pbset(PB_STOP);				/* halt the counter	*/
	for (i = 0; i < NTIMEREG; i++)
		rtcwr(i, (int)regs[i], PB_STOP);
	rtcwr(R_RESET, 0, PB_STOP);		/* clear the post stage	*/
	readregs(alt, PB_STOP);			/* did it take?		*/
	pbset(0);				/* restart the counter	*/
	rtcdesel();

	for (i = 0; i < NTIMEREG; i++)
		if (alt[i] != regs[i])
			return (RTC_NONE);
	return (RTC_OK);
}

/************************************************************************/
/*	BIOS function 23 (TIME)						*/
/************************************************************************/

/*
 * long rtctime(todadr, set)   XADDR todadr;  WORD set;
 *
 * set == 0: read the clock into the caller's 5-byte block.
 * set != 0: program the clock from the caller's 5-byte block.
 * Returns 0 on success, 0xff when no clock answered; on a failed read
 * the caller's block is left untouched.
 */
long rtctime(todadr, set)
XADDR todadr;
WORD set;
{
	register int rc;

	if (set) {
		mem_cpy(todadr, (XADDR)todbuf, (long)TODLEN);
		rc = rtcput(todbuf);
	} else if ((rc = rtcget(todbuf)) == RTC_OK)
		mem_cpy((XADDR)todbuf, todadr, (long)TODLEN);
	return ((long)rc);
}
