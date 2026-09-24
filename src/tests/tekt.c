/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/*
 * tekt.c - the graphics module's Tek parser, called through the BIOS.
 *
 * Each stream is drawn twice on the same split surface: by the parser
 * linked here, and by the HR console's in the system's graphics module
 * (BIOS function 102).  The buffers, guard bytes included, must come out
 * equal.
 */

#include "cpm.h"
#include "gfx.h"
#include "tek.h"

#define	GUARD	4
#define	SENT	0xA5
#define	MAXB	600

#define	HY(y)	(0x20 | ((y) >> 5))
#define	LY(y)	(0x60 | ((y) & 31))
#define	HX(x)	(0x20 | ((x) >> 5))
#define	LX(x)	(0x40 | ((x) & 31))
#define	P(x, y)	HY(y), LY(y), HX(x), LX(x)

#define	GS	0x1d
#define	FS	0x1c
#define	US	0x1f
#define	ESC	0x1b

static char s0[] = { GS, P(100, 100), P(900, 100), P(900, 700),
		     P(100, 100), US, 0 };
static char s1[] = { FS, P(10, 10), P(512, 390), P(1000, 770), US,
		     GS, P(0, 0), P(1023, 779), 0 };
static char s2[] = { GS, P(50, 60), P(300, 60), GS, P(50, 500),
		     P(300, 520), US, 0 };
static char s3[] = { GS, P(10, 10), P(1000, 770), ESC, '\f',
		     GS, P(30, 40), P(600, 400), US, 0 };
static char *streams[] = { s0, s1, s2, s3, 0 };

/* mono 80 x 40 in 10-byte rasters; colour 40 x 30 in 20 */
static int	msplit[] = { 0, 1, 95, 200, 399, -1 };
static int	csplit[] = { 0, 55, 300, 590, -1 };

static unsigned char	a0[MAXB + GUARD], a1[MAXB + GUARD];
static unsigned char	b0[MAXB + GUARD], b1[MAXB + GUARD];
static struct gfxsurf	sa, sb;
static struct tek	t;
static int		bad;
static int		runs;

static char hexd[] = "0123456789ABCDEF";

static VOID phex(v)
int v;
{
	char	s[5];
	int	i;

	for (i = 3; i >= 0; i--, v >>= 4)
		s[i] = hexd[v & 15];
	s[4] = 0;
	cputs(s);
}

static VOID fail(what, n, bpp, split)
char *what;
int n, bpp, split;
{
	cputs("TEKT: FAIL ");
	cputs(what);
	cputs(" stream=");
	phex(n);
	cputs(" bpp=");
	phex(bpp);
	cputs(" split=");
	phex(split);
	cputs("\r\n");
	bad = 1;
}

static VOID run(n, bpp, w, h, stride, split)
int n, bpp, w, h, stride, split;
{
	register int	i;
	register char	*p;
	int		drawn;
	int		total;

	total = h * stride;
	for (i = 0; i < MAXB + GUARD; i++)
		a0[i] = a1[i] = b0[i] = b1[i] = SENT;
	for (i = 0; i < total; i++)
		if (i < split)
			a0[i] = b0[i] = 0;
		else
			a1[i - split] = b1[i - split] = 0;

	gfx_ram(&sa, w, h, bpp, stride, a0, a1, (long) split);
	gfx_ram(&sb, w, h, bpp, stride, b0, b1, (long) split);
	tekinit(&t, &sa);
	for (p = streams[n]; *p; p++)
		tekput(&t, *p & 0xff);
	__bios(BIOS_HRRAM, (long) &sb, (long) streams[n]);

	drawn = 0;
	for (i = 0; i < MAXB + GUARD; i++) {
		if (a0[i] != b0[i] || a1[i] != b1[i]) {
			fail("module and library differ", n, bpp, split);
			return;
		}
		if ((a0[i] != 0 && a0[i] != SENT) || (a1[i] != 0 && a1[i] != SENT))
			drawn = 1;
	}
	if (!drawn)
		fail("nothing drawn", n, bpp, split);
	runs++;
}

int main(argc, argv)
int argc;
char *argv[];
{
	register int	n, i;

	cputs("TEKT: start\r\n");
	for (n = 0; streams[n] && !bad; n++) {
		for (i = 0; msplit[i] >= 0 && !bad; i++)
			run(n, 1, 80, 40, 10, msplit[i]);
		for (i = 0; csplit[i] >= 0 && !bad; i++)
			run(n, 4, 40, 30, 20, csplit[i]);
	}
	if (bad) {
		cputs("TEKT: failed\r\n");
		return (1);
	}
	cputs("TEKT: ok ");
	phex(runs);
	cputs(" runs\r\n");
	return (0);
}
