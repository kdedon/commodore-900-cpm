/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/*
 * bioset.c - Exercise allowed and refused BDOS function 50 calls. SECTRAN
 * is the side-effect-free allowed probe.
 */

#include "cpm.h"

static struct biospb pb;
static char tod[5];		/* BIOS 23 deposits {date-word,h,m,s} here */

static long biosc(code, p1, p2)
int code;
long p1, p2;
{
	pb.code = code;
	pb.p1 = p1;
	pb.p2 = p2;
	return (__bdosl(BDOS_BIOSCALL, (long) &pb));
}

static VOID crlf()
{
	conout('\r');
	conout('\n');
}

static VOID report(name, code, p1, wantref)
char *name;
int code;
long p1;
int wantref;
{
	long	r;

	r = biosc(code, p1, 0L);
	cputs("BIOSET: ");
	cputs(name);
	cputs(r == BIOS_REFUSED ? " REFUSED" : " ALLOWED");
	cputs((r == BIOS_REFUSED) == (wantref != 0) ? " ok" : " WRONG");
	crlf();
}

int main(argc, argv)
int argc;
char **argv;
{
	long	r;

	crlf();
	if (sizeof (struct biospb) != 10) {
		cputs("BIOSET: struct biospb is not a 5-word block");
		crlf();
		return (1);
	}

	/* the allowed code, with a value only a real BIOS call can give
	   back: SECTRAN of 4242 is 4242 */
	r = biosc(BIOS_SECTRAN, 4242L, 0L);
	cputs("BIOSET: sectran(4242) = ");
	if (r == BIOS_REFUSED)
		cputs("REFUSED");
	else
		putdec((unsigned) r);
	crlf();

	report("init(0)", 0, 0L, 1);
	report("wboot(1)", 1, 0L, 1);
	report("conout(4)", 4, 0L, 1);
	report("gmrta(18)", 18, 0L, 1);
	report("setxvec(22)", 22, 0L, 1);
	report("undefined(99)", 99, 0L, 1);
	report("sectran(16)", 16, 0L, 0);
	report("flush(21)", 21, 0L, 0);
	/* TIME reads five bytes THROUGH its parameter, so unlike every
	   other probe here it needs a real buffer: P1 = 0 would deposit
	   the clock at segment 0 offset 0.  Answers RTC_OK or RTC_NONE
	   depending on whether this machine has a chip; both are
	   ALLOWED, which is what is under test. */
	report("time(23)", 23, (long) tod, 0);
	crlf();
	return (0);
}
