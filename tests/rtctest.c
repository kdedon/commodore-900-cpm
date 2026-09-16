/*
 * rtctest.c -- host unit tests for the C900 clock: the real src/rtc900.c
 * and the real user/date.c, compiled with the host cc and run against the
 * software MSM58321 in host/rtcchip.c.
 *
 * Both sources are compiled VERBATIM.  The DRI type layer comes from
 * host/rtcinc/stdio.h, which gives UBYTE the same SIGNED char and UWORD
 * the same 16 bits the target compiler gives them -- so a missing 0xff
 * mask misbehaves here exactly as it does on the machine.  That is how the
 * "every date from 1978-04-19 onward" set-path bug was caught, and test
 * signext_setpath below is its regression.
 *
 * date.c is compiled with -Dstatic= -Dmain=date_main so the harness can
 * see its TOD block and call it as a function.
 *
 * What this cannot cover: strobe timing, real silicon, and anything about
 * the Z8001 code generation.  The emulator target `make verify-rtc' covers
 * the whole path on the machine; this covers the cases a boot cannot reach
 * cheaply -- every day of the century, both ends of the year window, a
 * carry landing at a chosen register, and a chip that lies.
 *
 * Build: see the verify-rtc-host target in ../Makefile.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "rtcchip.h"

#define TOD_DATEHI	0
#define TOD_DATELO	1
#define TOD_HOUR	2
#define TOD_MIN		3
#define TOD_SEC		4
#define TODLEN		5

#define RTC_OK		0
#define RTC_NONE	0xff

extern int rtcinit(), rtcget(), rtcput();
extern long rtctime();

/* date.c, with -Dstatic= -Dmain=date_main */
extern int date_main(int argc, char **argv);
extern char tod[];
extern int blk[];

static int ntest, nfail;
static const char *curtest = "";

static void ck(int ok, const char *what)
{
	ntest++;
	if (!ok) {
		nfail++;
		printf("FAIL [%s] %s\n", curtest, what);
	}
}

static void ckeq(long got, long want, const char *what)
{
	ntest++;
	if (got != want) {
		nfail++;
		printf("FAIL [%s] %s: got %ld, want %ld\n",
		       curtest, what, got, want);
	}
}

static void ckstr(const char *got, const char *want, const char *what)
{
	ntest++;
	if (strcmp(got, want) != 0) {
		nfail++;
		printf("FAIL [%s] %s: got \"%s\", want \"%s\"\n",
		       curtest, what, got, want);
	}
}

static void nofaults(void)
{
	int i;

	ntest++;
	if (Chip.nfault == 0)
		return;
	nfail++;
	for (i = 0; i < Chip.nfault && i < CHIP_MAXFAULT; i++)
		printf("FAIL [%s] protocol: %s\n", curtest, Chip.fault[i]);
	if (Chip.nfault > CHIP_MAXFAULT)
		printf("FAIL [%s] ... and %d more\n", curtest,
		       Chip.nfault - CHIP_MAXFAULT);
}

/* ------------------------------------------------------------------ */
/* an independent calendar, so the driver is never checked against a	*/
/* rearrangement of its own arithmetic					*/
/* ------------------------------------------------------------------ */

static int t_isleap(int y)
{
	return (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
}

static int t_mlen(int y, int m)
{
	static const int d[13] = {0,31,28,31,30,31,30,31,31,30,31,30,31};

	return (m == 2 && t_isleap(y)) ? 29 : d[m];
}

/* Sakamoto's method; 0 = Sunday */
static int t_wday(int y, int m, int d)
{
	static const int t[13] = {0,0,3,2,5,0,3,5,1,4,6,2,4};

	if (m < 3)
		y -= 1;
	return (y + y/4 - y/100 + y/400 + t[m] + d) % 7;
}

/* days since 1977-12-31 */
static long t_day(int y, int m, int d)
{
	long n = 0;
	int i;

	for (i = 1978; i < y; i++)
		n += t_isleap(i) ? 366 : 365;
	for (i = 1; i < m; i++)
		n += t_mlen(y, i);
	return (n + d);
}

#define BCD(n)	((((n) / 10) << 4) | ((n) % 10))

/* ------------------------------------------------------------------ */
/* helpers								*/
/* ------------------------------------------------------------------ */

static void live(int y, int mo, int d, int h, int mi, int s)
{
	chip_reset();
	chip_seed(y, mo, d, h, mi, s, t_wday(y, mo, d));
	rtcinit();
	Chip.nread = Chip.nwrite = Chip.nreset = Chip.nstop = 0;
	Chip.ntick = Chip.nsuppressed = 0;
}

static long todday(char *t)
{
	return (((long)(t[TOD_DATEHI] & 0xff) << 8) | (t[TOD_DATELO] & 0xff));
}

/* ------------------------------------------------------------------ */
/* 1. bring-up: the CIO side						*/
/* ------------------------------------------------------------------ */

static void t_init(void)
{
	curtest = "init";
	chip_reset();
	Chip.mccr = 0x14;			/* what kbd_init leaves	*/
	Chip.pcdd = 0x07;
	rtcinit();

	ckeq(Chip.pbms, 0, "Port B put in bit-port mode");
	ckeq(Chip.pbdd, 0, "Port B all outputs after init");
	ckeq(Chip.mccr & 0x80, 0x80, "MCCR Port B enable set");
	ckeq(Chip.mccr & 0x14, 0x14, "MCCR keyboard Port A/C enables kept");
	ckeq(Chip.pcdd, 0x05, "PCDD makes PC1 an output and nothing else");
	ck((Chip.pcdata & 0x02) != 0, "/CS left released after init");
	ckeq(Chip.pc3_lost, 0, "PC3 (keyboard FIFO ack) never driven low");
	ckeq(Chip.stopped, 0, "STOP left low");
	nofaults();
}

/* Every strobe must happen inside a /CS transaction, and PC3 must survive
 * it: the keyboard shares this CIO. */
static void t_transaction(void)
{
	char t[TODLEN];

	curtest = "transaction";
	live(2026, 7, 31, 14, 32, 10);
	ckeq(rtcget(t), RTC_OK, "read of a live clock");
	ckeq(Chip.ncs, 1, "one /CS assertion per read");
	ck((Chip.pcdata & 0x02) != 0, "/CS released after the read");
	ckeq(Chip.pc3_lost, 0, "PC3 survives a read");
	nofaults();

	Chip.ncs = 0;
	ckeq(rtcput(t), RTC_OK, "set from the block just read");
	ckeq(Chip.ncs, 1, "one /CS assertion per set");
	ck((Chip.pcdata & 0x02) != 0, "/CS released after the set");
	ckeq(Chip.pc3_lost, 0, "PC3 survives a set");
	nofaults();
}

/* ------------------------------------------------------------------ */
/* 2. reading								*/
/* ------------------------------------------------------------------ */

static void t_read(void)
{
	char t[TODLEN];

	curtest = "read";
	live(2026, 7, 31, 14, 32, 10);
	ckeq(rtcget(t), RTC_OK, "status");
	ckeq(todday(t), t_day(2026, 7, 31), "date word");
	ckeq(t[TOD_HOUR] & 0xff, 0x14, "hour BCD");
	ckeq(t[TOD_MIN] & 0xff, 0x32, "minute BCD");
	ckeq(t[TOD_SEC] & 0xff, 0x10, "second BCD");
	ckeq(Chip.nread, 26, "two thirteen-register images, no retry");
	ckeq(Chip.nwrite, 0, "a read writes no register");
	ckeq(Chip.nstop, 0, "a read never raises STOP");
	nofaults();
}

/* The chip can be left in 12-hour mode; fn 23 always reports 24-hour. */
static void t_twelvehour(void)
{
	char t[TODLEN];
	int cases[][4] = {		/* h1, h10(with PM), expect	*/
		{ 2, 0x04, 0x14, 5 },	/* 02:05 PM -> 14:05		*/
		{ 2, 0x00, 0x02, 5 },	/* 02:05 AM -> 02:05		*/
		{ 2, 0x01, 0x00, 5 },	/* 12:05 AM -> 00:05		*/
		{ 2, 0x05, 0x12, 5 }	/* 12:05 PM -> 12:05		*/
	};
	int i;

	curtest = "12-hour";
	for (i = 0; i < 4; i++) {
		live(2026, 7, 31, 0, 0, 0);
		Chip.reg[4] = (unsigned char)cases[i][0];
		Chip.reg[5] = (unsigned char)cases[i][1];
		Chip.reg[2] = (unsigned char)cases[i][3];
		Chip.reg[3] = 0;
		ckeq(rtcget(t), RTC_OK, "12-hour image accepted");
		ckeq(t[TOD_HOUR] & 0xff, cases[i][2], "12-hour conversion");
	}
	/* 11 PM: H10 = PM|1, H1 = 1 */
	live(2026, 7, 31, 0, 0, 0);
	Chip.reg[4] = 1;
	Chip.reg[5] = 0x04 | 1;
	ckeq(rtcget(t), RTC_OK, "11 PM accepted");
	ckeq(t[TOD_HOUR] & 0xff, 0x23, "11 PM -> 23");
	nofaults();
}

/*
 * Images that must be rejected.  Each is set up by seeding a good clock
 * and then breaking exactly one thing, so a rejection can only come from
 * the field that was broken.
 */
static void t_reject(void)
{
	char t[TODLEN], keep[TODLEN];
	int i;

	curtest = "reject";

	/* no module fitted, both readings of an undriven bus */
	live(2026, 7, 31, 14, 32, 10);
	Chip.present = 0;
	Chip.float_mode = FLOAT_LATCH;
	memset(t, 0x5a, sizeof t);
	memcpy(keep, t, sizeof t);
	ckeq(rtcget(t), RTC_NONE, "absent module (latch) reports no clock");
	ck(memcmp(t, keep, TODLEN) == 0, "a failed read leaves the block alone");

	live(2026, 7, 31, 14, 32, 10);
	Chip.present = 0;
	Chip.float_mode = FLOAT_HIGH;
	ckeq(rtcget(t), RTC_NONE, "absent module (bus high) reports no clock");

	/* the address-echo image: what a CIO that returned its own output
	 * latch mid-read would produce.  The module itself cannot make this
	 * -- its data pins are tri-stated except during a READ pulse -- but
	 * it must be rejected all the same. */
	live(2026, 7, 31, 14, 32, 10);
	for (i = 0; i < 13; i++)
		Chip.reg[i] = (unsigned char)i;
	ckeq(rtcget(t), RTC_NONE, "the 0,1,2,...,12 address-echo image");

	/* one broken field at a time */
	live(2026, 7, 31, 14, 32, 10);
	Chip.reg[1] = 6;
	ckeq(rtcget(t), RTC_NONE, "S10 = 6");
	live(2026, 7, 31, 14, 32, 10);
	Chip.reg[0] = 10;
	ckeq(rtcget(t), RTC_NONE, "S1 = 10");
	live(2026, 7, 31, 14, 32, 10);
	Chip.reg[3] = 6;
	ckeq(rtcget(t), RTC_NONE, "MI10 = 6");
	live(2026, 7, 31, 14, 32, 10);
	Chip.reg[6] = 7;
	ckeq(rtcget(t), RTC_NONE, "W = 7");
	live(2026, 7, 31, 14, 32, 10);
	Chip.reg[5] = 8 | 2;
	Chip.reg[4] = 4;			/* 24 in 24-hour mode	*/
	ckeq(rtcget(t), RTC_NONE, "hour 24 in 24-hour mode");
	live(2026, 7, 31, 14, 32, 10);
	Chip.reg[5] = 0;
	Chip.reg[4] = 0;			/* hour 0 in 12-hour mode */
	ckeq(rtcget(t), RTC_NONE, "hour 0 in 12-hour mode");
	live(2026, 7, 31, 14, 32, 10);
	Chip.reg[5] = 1;
	Chip.reg[4] = 3;			/* 13 in 12-hour mode	*/
	ckeq(rtcget(t), RTC_NONE, "hour 13 in 12-hour mode");
	live(2026, 7, 31, 14, 32, 10);
	Chip.reg[9] = 3;
	Chip.reg[10] = 1;			/* month 13		*/
	ckeq(rtcget(t), RTC_NONE, "month 13");
	live(2026, 7, 31, 14, 32, 10);
	Chip.reg[9] = 0;
	Chip.reg[10] = 0;			/* month 0		*/
	ckeq(rtcget(t), RTC_NONE, "month 0");
	live(2026, 11, 30, 14, 32, 10);
	Chip.reg[7] = 1;
	Chip.reg[8] = (Chip.reg[8] & 0x0c) | 3;	/* 31 November		*/
	ckeq(rtcget(t), RTC_NONE, "31 November");
	live(2026, 2, 28, 14, 32, 10);
	Chip.reg[7] = 9;
	Chip.reg[8] = (Chip.reg[8] & 0x0c) | 2;	/* 29 February 2026	*/
	ckeq(rtcget(t), RTC_NONE, "29 February in a common year");
	live(2026, 7, 31, 14, 32, 10);
	Chip.reg[7] = 0;
	Chip.reg[8] = Chip.reg[8] & 0x0c;	/* day 0		*/
	ckeq(rtcget(t), RTC_NONE, "day 0");

	/* and the one that must NOT be rejected */
	live(2000, 2, 29, 14, 32, 10);
	ckeq(rtcget(t), RTC_OK, "29 February 2000 accepted");
	ckeq(todday(t), t_day(2000, 2, 29), "29 February 2000 date word");
	nofaults();
}

/*
 * The two-digit year window, at both ends: 78..99 is 19yy and 00..77 is
 * 20yy, so the range is exactly 1978-01-01 .. 2077-12-31.
 */
static void t_window(void)
{
	char t[TODLEN];

	curtest = "window";

	live(1978, 1, 1, 0, 0, 0);
	ckeq(rtcget(t), RTC_OK, "1978-01-01 read");
	ckeq(todday(t), 1, "1978-01-01 is date word 1");

	live(1999, 12, 31, 23, 59, 59);
	ckeq(rtcget(t), RTC_OK, "1999-12-31 read");
	ckeq(todday(t), t_day(1999, 12, 31), "yy = 99 is 1999");

	live(2000, 1, 1, 0, 0, 0);
	ckeq(rtcget(t), RTC_OK, "2000-01-01 read");
	ckeq(todday(t), t_day(2000, 1, 1), "yy = 00 is 2000");

	live(2077, 12, 31, 23, 59, 59);
	ckeq(rtcget(t), RTC_OK, "2077-12-31 read");
	ckeq(todday(t), t_day(2077, 12, 31), "yy = 77 is 2077");
	ckeq(todday(t), 36525, "the last representable day is 36525");
	nofaults();
}

/* ------------------------------------------------------------------ */
/* 3. setting								*/
/* ------------------------------------------------------------------ */

/*
 * A set must leave the chip holding exactly the right register image: the
 * digits, the 24-hour flag, the derived day of week, and the leap-year
 * selection in the datasheet's own code.
 *
 * That code is a countdown, not the surplus: the Supplement's table reads
 * 00 for a surplus of 0 (the leap year itself, "92, 96, 00") and 01, 10,
 * 11 for a surplus of 3, 2, 1, i.e. code = (4 - year%4) % 4.  Within one
 * year the two are indistinguishable -- February is 29 days long when the
 * code is 00 under either reading -- so this check is the only thing that
 * can hold the driver to the published code.
 */
static void setcheck(int y, int mo, int d, int h, int mi, int s)
{
	char t[TODLEN];
	long day;
	char what[80];

	day = t_day(y, mo, d);
	chip_reset();
	rtcinit();
	Chip.nread = Chip.nwrite = Chip.nstop = Chip.nreset = 0;
	t[TOD_DATEHI] = (char)(day >> 8);
	t[TOD_DATELO] = (char)(day & 0xff);
	t[TOD_HOUR] = (char)BCD(h);
	t[TOD_MIN] = (char)BCD(mi);
	t[TOD_SEC] = (char)BCD(s);

	sprintf(what, "%04d-%02d-%02d set", y, mo, d);
	ckeq(rtcput(t), RTC_OK, what);

	ckeq(Chip.reg[0], s % 10, "S1");
	ckeq(Chip.reg[1], s / 10, "S10");
	ckeq(Chip.reg[2], mi % 10, "MI1");
	ckeq(Chip.reg[3], mi / 10, "MI10");
	ckeq(Chip.reg[4], h % 10, "H1");
	ckeq(Chip.reg[5], 0x08 | (h / 10), "H10 = 24-hour flag + tens");
	ckeq(Chip.reg[6], t_wday(y, mo, d), "W = the true day of week");
	ckeq(Chip.reg[7], d % 10, "D1");
	ckeq(Chip.reg[8] & 3, d / 10, "D10 day tens");
	ckeq((Chip.reg[8] >> 2) & 3, (4 - (y & 3)) & 3,
	     "D10 leap-year selection is the datasheet's countdown code");
	ckeq(Chip.reg[9], mo % 10, "MO1");
	ckeq(Chip.reg[10], mo / 10, "MO10");
	ckeq(Chip.reg[11], (y % 100) % 10, "Y1");
	ckeq(Chip.reg[12], (y % 100) / 10, "Y10");

	/* the datasheet's write sequence: STOP high across the reload and
	 * the post-stage reset written before it falls */
	ckeq(Chip.nstop, 1, "STOP raised once for the reload");
	ckeq(Chip.stopped, 0, "STOP released at the end");
	ckeq(Chip.nreset, 1, "the post-stage reset register was written");
	ckeq(Chip.reset_while_stopped, 1, "reset written while STOP was high");
	nofaults();
}

static void t_set(void)
{
	curtest = "set";
	setcheck(1978, 1, 1, 0, 0, 0);
	setcheck(1978, 4, 19, 12, 0, 0);	/* the sign-extension day */
	setcheck(1979, 12, 31, 23, 59, 59);	/* surplus 3: code 01	  */
	setcheck(2000, 2, 29, 6, 7, 8);		/* surplus 0: code 00	  */
	setcheck(2004, 3, 1, 7, 8, 9);
	setcheck(2005, 7, 4, 12, 34, 56);	/* surplus 1: code 11	  */
	setcheck(2026, 7, 31, 14, 32, 10);	/* surplus 2: code 10	  */
	setcheck(2077, 12, 31, 23, 59, 59);
}

/* A set the chip does not take must be reported, not silently believed. */
static void t_setfails(void)
{
	char t[TODLEN];
	long day;

	curtest = "set-fails";

	day = t_day(2026, 7, 31);
	chip_reset();
	rtcinit();
	t[TOD_DATEHI] = (char)(day >> 8);
	t[TOD_DATELO] = (char)(day & 0xff);
	t[TOD_HOUR] = 0x14;
	t[TOD_MIN] = 0x32;
	t[TOD_SEC] = 0x10;

	Chip.drop_write = 9 + 1;		/* MO1 never lands	*/
	ckeq(rtcput(t), RTC_NONE, "a swallowed register write is caught");
	Chip.drop_write = 0;

	chip_reset();
	rtcinit();
	Chip.present = 0;
	ckeq(rtcput(t), RTC_NONE, "a set with no module reports no clock");

	/* out-of-range blocks are rejected before the chip is touched */
	chip_reset();
	rtcinit();
	Chip.nwrite = 0;
	t[TOD_DATEHI] = 0;
	t[TOD_DATELO] = 0;
	ckeq(rtcput(t), RTC_NONE, "date word 0 rejected");
	t[TOD_DATEHI] = (char)((36526L) >> 8);
	t[TOD_DATELO] = (char)(36526L & 0xff);
	ckeq(rtcput(t), RTC_NONE, "date word past 2077-12-31 rejected");
	t[TOD_DATEHI] = (char)(day >> 8);
	t[TOD_DATELO] = (char)(day & 0xff);
	t[TOD_HOUR] = 0x24;
	ckeq(rtcput(t), RTC_NONE, "hour 24 rejected");
	t[TOD_HOUR] = (char)0x99;		/* high bit set: signedness */
	ckeq(rtcput(t), RTC_NONE, "hour 0x99 rejected");
	t[TOD_HOUR] = 0x14;
	t[TOD_MIN] = 0x60;
	ckeq(rtcput(t), RTC_NONE, "minute 60 rejected");
	t[TOD_MIN] = (char)0x99;
	ckeq(rtcput(t), RTC_NONE, "minute 0x99 rejected");
	t[TOD_MIN] = 0x32;
	t[TOD_SEC] = 0x60;
	ckeq(rtcput(t), RTC_NONE, "second 60 rejected");
	ckeq(Chip.nwrite, 0, "a rejected block never reaches the chip");
	nofaults();
}

/*
 * The regression for the bug this harness was written to find: UBYTE is a
 * SIGNED char, so a date word whose LOW BYTE is >= 0x80 sign-extends into
 * the high byte unless it is masked, and the set path then sees a day
 * count far past 2077 and refuses the date.
 *
 * The first such word is 128 = 1978-05-08, and the affected dates are not
 * a tail but every other run of 128 days (128..255, 384..511, ...), which
 * is half the century.  The sweep in t_century covers all of them; this
 * names the first, and checks a word on each side of the boundary so the
 * check cannot pass by rejecting everything.
 */
static void t_signext(void)
{
	char t[TODLEN];
	long day;
	int i;
	static const int daysin[] = {127, 128, 129, 255, 256, 383, 384};

	curtest = "signext-setpath";
	ckeq(t_day(1978, 5, 8), 128, "date word 128 is 1978-05-08");
	ckeq(t_day(1978, 5, 7) & 0xff, 0x7f, "the day before is still 0x7f");

	for (i = 0; i < (int)(sizeof daysin / sizeof daysin[0]); i++) {
		day = daysin[i];
		chip_reset();
		rtcinit();
		t[TOD_DATEHI] = (char)(day >> 8);
		t[TOD_DATELO] = (char)(day & 0xff);
		t[TOD_HOUR] = 0x23;
		t[TOD_MIN] = 0x59;
		t[TOD_SEC] = 0x59;
		ckeq(rtcput(t), RTC_OK, "a date word across the 0x80 boundary");
		ckeq(rtcget(t), RTC_OK, "and it reads back");
		ckeq(todday(t), day, "and it is the same day");
	}

	/* 1978-05-08 itself, digit by digit */
	chip_reset();
	rtcinit();
	t[TOD_DATEHI] = 0;
	t[TOD_DATELO] = (char)0x80;
	t[TOD_HOUR] = 0x23;
	t[TOD_MIN] = 0x59;
	t[TOD_SEC] = 0x59;
	ckeq(rtcput(t), RTC_OK, "1978-05-08 sets");
	ckeq(Chip.reg[7], 8, "D1 = 8");
	ckeq(Chip.reg[8] & 3, 0, "D10 = 0");
	ckeq(Chip.reg[9], 5, "MO1 = 5");
	ckeq(Chip.reg[11], 8, "Y1 = 8");
	nofaults();
}

/*
 * Every day of the representable century, through the real chip: set it,
 * read it back, and check the block, the register image and the day of
 * week against the independent calendar above.  36525 round trips.
 */
static void t_century(void)
{
	char t[TODLEN], u[TODLEN];
	long day, prev;
	int y, mo, d, bad;

	curtest = "century";
	bad = 0;
	prev = 0;
	for (y = 1978; y <= 2077 && bad < 5; y++)
	    for (mo = 1; mo <= 12 && bad < 5; mo++)
		for (d = 1; d <= t_mlen(y, mo) && bad < 5; d++) {
			day = t_day(y, mo, d);
			if (day != prev + 1) {
				printf("FAIL [century] %04d-%02d-%02d: the "
				       "date word jumped %ld -> %ld\n",
				       y, mo, d, prev, day);
				nfail++;
				bad++;
			}
			prev = day;

			chip_reset();
			rtcinit();
			t[TOD_DATEHI] = (char)(day >> 8);
			t[TOD_DATELO] = (char)(day & 0xff);
			t[TOD_HOUR] = 0x13;
			t[TOD_MIN] = 0x45;
			t[TOD_SEC] = 0x06;
			if (rtcput(t) != RTC_OK) {
				printf("FAIL [century] %04d-%02d-%02d "
				       "(word %ld) would not set\n",
				       y, mo, d, day);
				nfail++;
				bad++;
				continue;
			}
			if (Chip.reg[6] != t_wday(y, mo, d)) {
				printf("FAIL [century] %04d-%02d-%02d wrote "
				       "weekday %d, want %d\n", y, mo, d,
				       Chip.reg[6], t_wday(y, mo, d));
				nfail++;
				bad++;
			}
			if (((Chip.reg[8] >> 2) & 3) != ((4 - (y & 3)) & 3)) {
				printf("FAIL [century] %04d-%02d-%02d wrote "
				       "leap code %d, want %d\n", y, mo, d,
				       (Chip.reg[8] >> 2) & 3,
				       (4 - (y & 3)) & 3);
				nfail++;
				bad++;
			}
			if (rtcget(u) != RTC_OK) {
				printf("FAIL [century] %04d-%02d-%02d would "
				       "not read back\n", y, mo, d);
				nfail++;
				bad++;
				continue;
			}
			if (memcmp(t, u, TODLEN) != 0) {
				printf("FAIL [century] %04d-%02d-%02d did not "
				       "round trip: %02x%02x %02x:%02x:%02x\n",
				       y, mo, d, u[0] & 0xff, u[1] & 0xff,
				       u[2] & 0xff, u[3] & 0xff, u[4] & 0xff);
				nfail++;
				bad++;
			}
		}
	ntest++;
	ckeq(prev, 36525, "the century is 36525 contiguous days");
	if (bad)
		printf("FAIL [century] stopped after %d failures\n", bad);
}

/* ------------------------------------------------------------------ */
/* 3a. the New Year, and the leap-year selection across it		*/
/* ------------------------------------------------------------------ */

/*
 * Nothing in this file used to cross a 1 January, and the leap-year
 * selection in D10 is the one field whose behaviour across one is an
 * INFERENCE rather than a datasheet statement: the Supplement gives the
 * code (00 for the leap year itself, 01/10/11 for a surplus of 3/2/1) but
 * says nothing about who advances it.  The front-page feature list says
 * "leap year automatically adjustable" and the code is a countdown, so
 * both host/rtcchip.c and the emulator model it as a 2-bit down-counter
 * decremented on each year carry.
 *
 * These tests settle the MODEL and the DRIVER, and they cannot settle the
 * SILICON: the part is not here.  What they are worth is this -- if the
 * chip does advance the field, then the datasheet code is the one that
 * still lands on 29 February afterwards, and that is now demonstrated end
 * to end rather than argued.  The last two checks are the ones that make
 * the question survivable either way: the driver never reads the leap
 * field at all, so a chip that does NOT advance it, and is therefore
 * lying about the year's leapness, still produces the right date.
 *
 * The chip is stepped with chip_tick() rather than by waiting, and the
 * calendar is jumped forward by poking the month/day digits while LEAVING
 * D10's high two bits alone -- so every leap-year selection tested below
 * after the first one is a value the model's own carry chain produced.
 */

/* set the month and day, keeping D10's leap-year selection */
static void poke_md(int mo, int d)
{
	Chip.reg[7] = (unsigned char)(d % 10);
	Chip.reg[8] = (unsigned char)((Chip.reg[8] & 0x0c) | (d / 10));
	Chip.reg[9] = (unsigned char)(mo % 10);
	Chip.reg[10] = (unsigned char)(mo / 10);
}

static void poke_hms(int h, int mi, int s)
{
	Chip.reg[0] = (unsigned char)(s % 10);
	Chip.reg[1] = (unsigned char)(s / 10);
	Chip.reg[2] = (unsigned char)(mi % 10);
	Chip.reg[3] = (unsigned char)(mi / 10);
	Chip.reg[4] = (unsigned char)(h % 10);
	Chip.reg[5] = (unsigned char)(8 | (h / 10));	/* 24-hour	*/
}

static int chip_leap(void)
{
	return ((Chip.reg[8] >> 2) & 3);
}

static int chip_yy(void)
{
	return ((Chip.reg[12] & 15) * 10 + (Chip.reg[11] & 15));
}

/* what the DRIVER makes of the register file right now */
static long driver_day(void)
{
	char t[TODLEN];

	if (rtcget(t) != RTC_OK)
		return (-1);
	return (todday(t));
}

static void t_newyear(void)
{
	char t[TODLEN];

	curtest = "newyear";

	/* ---- 1979-12-31 23:59:59, one second before the year carry ---- */
	live(1979, 12, 31, 23, 59, 59);
	ckeq(driver_day(), t_day(1979, 12, 31), "the last day of 1979 reads");
	ckeq(chip_leap(), 1, "1979 is a surplus of 3: the code is 01");
	ckeq(Chip.reg[6], t_wday(1979, 12, 31), "1979-12-31 is a Monday");

	/* ---- the carry ---- */
	chip_tick();
	ckeq(driver_day(), t_day(1980, 1, 1), "the year carried to 1980-01-01");
	ckeq(chip_yy(), 80, "the year digits are 80");
	ckeq(Chip.reg[9] + Chip.reg[10] * 10, 1, "the month wrapped to January");
	ckeq(Chip.reg[7] + (Chip.reg[8] & 3) * 10, 1, "the day wrapped to the 1st");
	ckeq(Chip.reg[6], t_wday(1980, 1, 1), "the weekday advanced to Tuesday");
	ckeq(chip_leap(), 0,
	     "the leap-year selection counted down 01 -> 00 for 1980");
	nofaults();

	/* ---- 29 February, authorised by a field NOTHING WROTE ---- *
	 * The month and day are poked forward; D10's high bits still hold
	 * the value the carry above produced, so reaching 02/29 here is the
	 * model's own auto-advance being taken at its word.		*/
	poke_md(2, 28);
	poke_hms(23, 59, 59);
	ckeq(driver_day(), t_day(1980, 2, 28), "1980-02-28 reads back");
	ckeq(chip_leap(), 0, "the auto-advanced code is still 00 in February");
	chip_tick();
	ckeq(driver_day(), t_day(1980, 2, 29),
	     "1980 has a 29 February on the auto-advanced code");
	poke_hms(23, 59, 59);
	chip_tick();
	ckeq(driver_day(), t_day(1980, 3, 1),
	     "and 29 February is followed by 1 March");
	nofaults();

	/* ---- the next New Year, and a year that is NOT leap ---- */
	poke_md(12, 31);
	poke_hms(23, 59, 59);
	chip_tick();
	ckeq(driver_day(), t_day(1981, 1, 1), "1980 carried into 1981");
	ckeq(chip_leap(), 3,
	     "the selection counted down 00 -> 11, a surplus of 1");
	poke_md(2, 28);
	poke_hms(23, 59, 59);
	chip_tick();
	ckeq(driver_day(), t_day(1981, 3, 1),
	     "1981 has no 29 February: 02/28 carries straight to 03/01");
	ckeq(chip_leap(), 3, "and no year carry happened, so the code held");
	nofaults();

	/* ---- three more carries, back to a leap code of 00 ---- */
	poke_md(12, 31);
	poke_hms(23, 59, 59);
	chip_tick();				/* -> 1982 */
	ckeq(chip_leap(), 2, "1982: the code is 10");
	poke_md(12, 31);
	poke_hms(23, 59, 59);
	chip_tick();				/* -> 1983 */
	ckeq(chip_leap(), 1, "1983: the code is 01");
	poke_md(12, 31);
	poke_hms(23, 59, 59);
	chip_tick();				/* -> 1984 */
	ckeq(chip_yy(), 84, "four carries later the year is 1984");
	ckeq(chip_leap(), 0, "1984 is leap again and the code is back to 00");
	ckeq(chip_leap(), ((4 - (1984 & 3)) & 3),
	     "four years of counting down agree with rtcput()'s formula");
	nofaults();

	/* ---- the driver does not depend on any of the above ---- *
	 * decode() masks D10 with 0x03 and asks its own isleap(), so a chip
	 * that never advanced the field -- and is therefore claiming the
	 * wrong leapness -- still yields the right date.  This is why the
	 * open question is not a liability.				*/
	live(1980, 2, 29, 12, 0, 0);
	Chip.reg[8] = (unsigned char)((Chip.reg[8] & 3) | (3 << 2));
	ckeq(chip_leap(), 3, "the chip now claims 1980 is three years off leap");
	ckeq(driver_day(), t_day(1980, 2, 29),
	     "the driver reads 29 February anyway: it never looks at the field");
	live(1979, 2, 28, 12, 0, 0);
	Chip.reg[7] = 9;
	Chip.reg[8] = (unsigned char)((Chip.reg[8] & 0x0c) | 2);
	Chip.reg[8] = (unsigned char)((Chip.reg[8] & 3) | (0 << 2));
	ckeq(driver_day(), -1,
	     "and 1979-02-29 is refused however the field is set");
	nofaults();

	/* ---- the set path across the same boundary ---- */
	live(1979, 6, 1, 0, 0, 0);
	t[TOD_DATEHI] = (char)(t_day(1980, 2, 29) >> 8);
	t[TOD_DATELO] = (char)(t_day(1980, 2, 29) & 0xff);
	t[TOD_HOUR] = 0x23;
	t[TOD_MIN] = 0x59;
	t[TOD_SEC] = 0x59;
	ckeq(rtcput(t), RTC_OK, "1980-02-29 23:59:59 sets");
	ckeq(chip_leap(), 0, "rtcput wrote the leap code for 1980");
	chip_tick();
	ckeq(driver_day(), t_day(1980, 3, 1), "and it rolls into 1 March");
	nofaults();
}

/* ------------------------------------------------------------------ */
/* 4. the read-while-ticking race					*/
/* ------------------------------------------------------------------ */

static void t_race(void)
{
	char t[TODLEN];

	curtest = "race";

	/* nothing moves: two images, no retry */
	live(2026, 7, 31, 14, 32, 10);
	ckeq(rtcget(t), RTC_OK, "quiet clock reads");
	ckeq(Chip.nread, 26, "a quiet read is exactly two images");

	/* a carry lands in the middle of the first image.  Without the
	 * double read this comes back as 14:32:60 or with the minute
	 * already carried and the second not. */
	live(2026, 7, 31, 14, 32, 59);
	Chip.tick_at_read = 7;
	ckeq(rtcget(t), RTC_OK, "a straddled read still succeeds");
	ckeq(Chip.ntick, 1, "the carry really happened");
	ckeq(Chip.nread, 52, "the straddled image was thrown away and retaken");
	ckeq(t[TOD_HOUR] & 0xff, 0x14, "hour after the carry");
	ckeq(t[TOD_MIN] & 0xff, 0x33, "minute after the carry");
	ckeq(t[TOD_SEC] & 0xff, 0x00, "second after the carry, never 0x60");

	/* the same at a midnight, where a torn image would keep yesterday */
	live(2026, 7, 31, 23, 59, 59);
	Chip.tick_at_read = 3;
	ckeq(rtcget(t), RTC_OK, "a straddled midnight still succeeds");
	ckeq(todday(t), t_day(2026, 8, 1), "the date carried with the time");
	ckeq(t[TOD_HOUR] & 0xff, 0x00, "midnight hour");

	/* a clock that carries under every single read can never be read
	 * coherently: four tries and then fail closed, rather than serve a
	 * time that never existed */
	live(2026, 7, 31, 14, 32, 10);
	Chip.tick_every = 1;
	ckeq(rtcget(t), RTC_NONE, "an unreadable clock fails closed");
	ckeq(Chip.nread, 104, "exactly four tries were made");
	nofaults();
}

/* ------------------------------------------------------------------ */
/* 5. BIOS function 23 itself						*/
/* ------------------------------------------------------------------ */

static void t_fn23(void)
{
	char t[TODLEN], keep[TODLEN];
	long day;

	curtest = "fn23";
	live(2026, 7, 31, 14, 32, 10);
	memset(t, 0x5a, sizeof t);
	ckeq((int)rtctime((long)t, (short)0), RTC_OK, "fn 23 read status");
	ckeq(todday(t), t_day(2026, 7, 31), "fn 23 read date word");
	ckeq(t[TOD_SEC] & 0xff, 0x10, "fn 23 read seconds");

	day = t_day(2004, 3, 1);
	t[TOD_DATEHI] = (char)(day >> 8);
	t[TOD_DATELO] = (char)(day & 0xff);
	t[TOD_HOUR] = 0x07;
	t[TOD_MIN] = 0x08;
	t[TOD_SEC] = 0x09;
	ckeq((int)rtctime((long)t, (short)1), RTC_OK, "fn 23 set status");
	memset(t, 0, sizeof t);
	ckeq((int)rtctime((long)t, (short)0), RTC_OK, "fn 23 read back");
	ckeq(todday(t), day, "fn 23 round trip date");
	ckeq(t[TOD_HOUR] & 0xff, 0x07, "fn 23 round trip hour");

	Chip.present = 0;
	memset(t, 0x5a, sizeof t);
	memcpy(keep, t, sizeof t);
	ckeq((int)rtctime((long)t, (short)0), RTC_NONE, "fn 23 no clock");
	ck(memcmp(t, keep, TODLEN) == 0,
	   "a failed fn 23 read leaves the caller's block untouched");
	nofaults();
}

/* ------------------------------------------------------------------ */
/* 6. DATE, the transient (user/date.c)					*/
/* ------------------------------------------------------------------ */

char out[4096];
static int outn;
static char *inq[4];
static int inqn, inqi;
static int keyhit;

static void outreset(void)
{
	outn = 0;
	out[0] = 0;
}

int conout(c)
int c;
{
	if (outn < (int)sizeof out - 1) {
		out[outn++] = (char)c;
		out[outn] = 0;
	}
	return (c);
}

int cputs(s)
char *s;
{
	while (*s)
		conout(*s++);
	return (0);
}

int __bdos(func, param)
int func;
long param;
{
	int *b;
	char *p;
	long addr;
	int n;

	switch (func) {
	case 2:					/* console output	*/
		return (conout((int)param));
	case 10:				/* read console buffer	*/
		p = (char *)(long)param;
		n = 0;
		if (inqi < inqn) {
			strcpy(p + 2, inq[inqi]);
			n = (int)strlen(inq[inqi]);
			inqi++;
		}
		p[1] = (char)n;
		return (0);
	case 11:				/* console status	*/
		return (keyhit);
	case 1:					/* console input	*/
		keyhit = 0;
		return ('\r');
	case 50:				/* direct BIOS call	*/
		b = (int *)(long)param;
		/* the five-word block the SC-trap gate marshals: code, then
		 * the two XADDRs.  On the host an address is wider than the
		 * target's 32 bits, so P1 carries the top and P2 the low
		 * sixteen -- the same split date.c builds. */
		addr = ((long)b[1] << 16) | (b[2] & 0xffff);
		if (b[0] != 23) {
			printf("FAIL [date] fn 50 asked for BIOS %d\n", b[0]);
			nfail++;
			return (0xff);
		}
		if (addr != (long)tod) {
			printf("FAIL [date] fn 50 P1 does not point at the "
			       "TOD block\n");
			nfail++;
			return (0xff);
		}
		if (b[3] != 0) {
			printf("FAIL [date] fn 50 P2 segment is not zero\n");
			nfail++;
			return (0xff);
		}
		return ((int)rtctime((long)tod, (short)b[4]) & 0xff);
	}
	return (0);
}

/* run DATE with the given arguments and return its exit status */
static int date(char *a1, char *a2)
{
	char *av[3];
	int ac;

	av[0] = "DATE";
	av[1] = a1;
	av[2] = a2;
	ac = a1 == 0 ? 1 : (a2 == 0 ? 2 : 3);
	outreset();
	return (date_main(ac, av));
}

static void t_date(void)
{
	curtest = "date";

	live(2026, 7, 31, 14, 32, 10);
	ckeq(date(0, 0), 0, "DATE exit status");
	ckstr(out, "Fri 07/31/26 14:32:10\r\n", "DATE on a live clock");

	/* both ends of the two-digit year window, end to end */
	live(2026, 7, 31, 14, 32, 10);
	ckeq(date("01/01/78", "00:00:00"), 0, "DATE 01/01/78 exit status");
	ckstr(out, "Sun 01/01/78 00:00:00\r\n", "the first representable day");
	ckeq(date(0, 0), 0, "read back 1978");
	ckstr(out, "Sun 01/01/78 00:00:00\r\n", "1978 read back from the chip");

	ckeq(date("12/31/77", "23:59:59"), 0, "DATE 12/31/77 exit status");
	ckstr(out, "Fri 12/31/77 23:59:59\r\n", "the last representable day");
	ckeq(date(0, 0), 0, "read back 2077");
	ckstr(out, "Fri 12/31/77 23:59:59\r\n", "2077 read back from the chip");

	/* 12/31/99 and 01/01/00 straddle the window's fold */
	ckeq(date("12/31/99", "23:59:59"), 0, "DATE 12/31/99 exit status");
	ckstr(out, "Fri 12/31/99 23:59:59\r\n", "yy = 99 is 1999");
	ckeq(date("01/01/00", "00:00:00"), 0, "DATE 01/01/00 exit status");
	ckstr(out, "Sat 01/01/00 00:00:00\r\n", "yy = 00 is 2000");
	ckeq(date("02/29/00", "12:00:00"), 0, "DATE 02/29/00 exit status");
	ckstr(out, "Tue 02/29/00 12:00:00\r\n", "2000 is a leap year");

	nofaults();
}

static void t_datebad(void)
{
	static char *bad[][2] = {
		{ "13/45/99", "99:99:99" },	/* every field wrong	*/
		{ "02/30/26", "12:00:00" },	/* no 30 February	*/
		{ "02/29/26", "12:00:00" },	/* 2026 is not a leap year */
		{ "00/01/26", "12:00:00" },	/* month 0		*/
		{ "12/00/26", "12:00:00" },	/* day 0		*/
		{ "12/32/26", "12:00:00" },	/* day 32		*/
		{ "07/31/26", "24:00:00" },	/* hour 24		*/
		{ "07/31/26", "12:60:00" },	/* minute 60		*/
		{ "07/31/26", "12:00:60" },	/* second 60		*/
		{ "7/31/26", "12:00:00" },	/* one-digit month	*/
		{ "07-31-26", "12:00:00" },	/* wrong separator	*/
		{ "07/31/26", "12:00" },	/* short time		*/
		{ "07/31/26", "12:00:000" },	/* long time		*/
		{ "07/31/2026", "12:00:00" }	/* four-digit year	*/
	};
	int i;

	curtest = "date-bad";
	for (i = 0; i < (int)(sizeof bad / sizeof bad[0]); i++) {
		live(2026, 7, 31, 14, 32, 10);
		ckeq(date(bad[i][0], bad[i][1]), 1, bad[i][0]);
		ck(strncmp(out, "Usage: DATE", 11) == 0, bad[i][1]);
		ckeq(Chip.nwrite, 0, "a rejected argument never sets the chip");
	}

	live(2026, 7, 31, 14, 32, 10);
	ckeq(date("Q", 0), 1, "DATE Q is rejected");
	ck(strncmp(out, "Usage: DATE", 11) == 0, "DATE Q prints the usage");
	nofaults();
}

static void t_datenoclock(void)
{
	curtest = "date-noclock";

	live(2026, 7, 31, 14, 32, 10);
	Chip.present = 0;
	ckeq(date(0, 0), 1, "DATE exit status with no clock");
	ckstr(out, "No clock in this machine\r\n", "DATE reports no clock");

	live(2026, 7, 31, 14, 32, 10);
	Chip.present = 0;
	ckeq(date("07/31/26", "14:32:10"), 1, "DATE set with no clock");
	ckstr(out, "No clock: the time was not set\r\n",
	      "DATE says the time was not stored");
	nofaults();
}

static void t_dateset(void)
{
	curtest = "date-set";

	live(2026, 7, 31, 14, 32, 10);
	inq[0] = "03/01/04";
	inq[1] = "07:08:09";
	inqn = 2;
	inqi = 0;
	ckeq(date("SET", 0), 0, "DATE SET exit status");
	ckstr(out, "Enter today's date (MM/DD/YY): \r\n"
		   "Enter the time (HH:MM:SS): \r\n"
		   "Mon 03/01/04 07:08:09\r\n",
	      "DATE SET prompts, stores and shows");

	live(2026, 7, 31, 14, 32, 10);
	inq[0] = "02/30/04";
	inq[1] = "07:08:09";
	inqn = 2;
	inqi = 0;
	ckeq(date("SET", 0), 1, "DATE SET rejects a bad date");
	ck(strstr(out, "Bad date or time") != 0, "DATE SET says why");
	ckeq(Chip.nwrite, 0, "a rejected DATE SET never touches the chip");

	/* DATE C prints until a key is waiting */
	live(2026, 7, 31, 14, 32, 10);
	keyhit = 1;
	ckeq(date("C", 0), 0, "DATE C exit status");
	ckstr(out, "Fri 07/31/26 14:32:10\r\n", "DATE C stops on a keypress");
	nofaults();
}

/* ------------------------------------------------------------------ */

int main(argc, argv)
int argc;
char **argv;
{
	t_init();
	t_transaction();
	t_read();
	t_twelvehour();
	t_reject();
	t_window();
	t_set();
	t_setfails();
	t_signext();
	t_century();
	t_newyear();
	t_race();
	t_fn23();
	t_date();
	t_datebad();
	t_datenoclock();
	t_dateset();

	printf("rtctest: %d checks, %d failed\n", ntest, nfail);
	if (nfail) {
		printf("rtctest: FAIL\n");
		return (1);
	}
	printf("rtctest: PASS\n");
	return (0);
}
