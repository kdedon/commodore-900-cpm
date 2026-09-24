/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/*
 * gdost.c - GSX through BDOS function 115 against pixels worked out by
 * hand.
 *
 * With no HR screen gdos() must refuse Open.  BIOS function 34 then
 * opens a 64 x 32 x 1 surface here, and gdos() draws a polyline, a fill
 * and text on it and closes it; a call after Close must be refused, and
 * so must an Open from console 1.  Detaching console 0 must unbind the
 * workstation, and GDOST Z, which selects console 0 without owning it,
 * must be refused Open.
 * It opens the surface again and starts GDOST Y, a second process, which
 * must be refused Open and drawing.  It exits with the surface open, and
 * GDOST X, loaded over it, must be refused a draw that would land in the
 * dead run's surface, then open its own.
 * Prints GDOST OK, GDOST Y OK, GDOST Z OK, GDOST X OK, or GDOST FAIL n
 * naming the check.
 */

#include "cpm.h"
#include "gfx.h"

#define	BDOS_DELAY	141
#define	BDOS_CREATEPROC	144
#define	BDOS_PROCCNT	145
#define	BDOS_ATTACH	146
#define	BDOS_DETACH	147
#define	BDOS_SETCON	148
#define	BDOS_GETCON	153

#define	W	64
#define	H	32
#define	NB	(W / 8 * H)
#define	GUARD	16

static unsigned char	buf[NB + GUARD];
static unsigned char	img[NB];
static struct gfxsurf	surf = { W, H, 1, W / 8, 0, 0, (long)(NB / 2) };

static int	contrl[6], intin[10], ptsin[8], intout[45], ptsout[12];
static struct gdospb	pb = { contrl, intin, ptsin, intout, ptsout };
static struct biospb	bp;

static int	step;

static VOID fail()
{
	cputs("GDOST FAIL ");
	putdec(step);
	cputs("\r\n");
	__bdos(BDOS_WBOOT, 0L);
}

static VOID check(ok)
int ok;
{
	step++;
	if (!ok)
		fail();
}

/* Normalised coordinates landing mid-pixel on device (x, y). */
static VOID pt(i, x, y)
int i, x, y;
{
	ptsin[2 * i] = 512 * x + 100;
	ptsin[2 * i + 1] = 1024 * y + 200;
}

static VOID op(code, npts, nint)
int code, npts, nint;
{
	contrl[0] = code;
	contrl[1] = npts;
	contrl[3] = nint;
}

/* Device (x, y) set in img: raster 31 - y, leftmost pixel in the MSB. */
static VOID px(x, r)
int x, r;
{
	img[r * (W / 8) + x / 8] |= 0x80 >> (x % 8);
}

static VOID worked()
{
	static unsigned char tee[8] = {
		0x7F, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x00
	};
	int x, r, c;

	for (x = 2; x <= 20; x++)	/* polyline (2,28)-(20,28)-(20,20) */
		px(x, 3);
	for (r = 3; r <= 11; r++)
		px(20, r);
	for (r = 19; r <= 26; r++)	/* solid fill (30,5)..(40,12) */
		for (x = 30; x <= 40; x++)
			px(x, r);
	for (r = 0; r < 8; r++)		/* 'T' at (44,4), 6 above to its top */
		for (c = 0; c < 8; c++)
			if (tee[r] >> (7 - c) & 1)
				px(44 + c, 21 + r);
}

/* The first letter of the command tail, or 0. */
static int mode()
{
	int i;

	for (i = 1; i <= _base->buff[0]; i++)
		if (_base->buff[i] != ' ')
			return (_base->buff[i]);
	return (0);
}

/* Open Workstation on surf, through BIOS function 34. */
static long bopen()
{
	op(1, 0, 10);
	return (__bdosl(BDOS_BIOSCALL, (long)&bp));
}

static VOID diag()
{
	op(6, 2, 0);
	pt(0, 0, 0);
	pt(1, 63, 31);
}

static int all(v, n)
int v, n;
{
	int i;

	for (i = 0; i < n; i++)
		if (buf[i] != v)
			return (0);
	return (1);
}

/* Run GDOST m beside this process and wait for it to end. */
static VOID child(m)
int m;
{
	static struct {
		struct fcb	fcb;
		char		tlen;
		char		tail[128];
	} req;
	int n, i;

	mkfcb("GDOST.Z8K", &req.fcb);
	req.tlen = 2;
	req.tail[0] = ' ';
	req.tail[1] = m;
	n = __bdos(BDOS_PROCCNT, 0L);
	check(__bdos(BDOS_CREATEPROC, (long)&req) == 0);
	for (i = 0; __bdos(BDOS_PROCCNT, 0L) > n; i++) {
		if (i > 3000)
			fail();
		__bdos(BDOS_DELAY, 1L);
	}
}

int main()
{
	int i;

	surf.seg0 = buf;
	surf.seg1 = buf + NB / 2;
	for (i = 0; i < NB + GUARD; i++)
		buf[i] = 0x55;
	for (i = 0; i < 10; i++)
		intin[i] = 1;
	bp.code = BIOS_GSX;
	bp.p1 = (long)&surf;
	bp.p2 = (long)&pb;

	/* The parent has the workstation: nothing here may take or use it. */
	if (mode() == 'Y') {
		check(bopen() == -1L);
		check(gdos(&pb) == -1);
		diag();
		check(gdos(&pb) == -1);
		check(all(0x55, NB + GUARD));
		cputs("GDOST Y OK\r\n");
		return (0);
	}

	if (mode() == 'Z') {
		check(__bdos(BDOS_SETCON, 0L) == 0);
		check(bopen() == -1L);
		cputs("GDOST Z OK\r\n");
		return (0);
	}

	/* The last run's surface was here: its end must have unbound it. */
	if (mode() == 'X') {
		diag();
		check(gdos(&pb) == -1);
		check(all(0x55, NB + GUARD));
		check(bopen() == 0L);
		op(2, 0, 0);
		check(gdos(&pb) == 0);
		cputs("GDOST X OK\r\n");
		return (0);
	}

	op(1, 0, 10);
	check(gdos(&pb) == -1);		/* 1: no HR screen */

	check(bopen() == 0L);		/* 2 */
	check(contrl[2] == 6 && contrl[4] == 45 && intout[0] == W - 1
	      && intout[1] == H - 1 && intout[13] == 2);	/* 3 */

	op(6, 3, 0);
	pt(0, 2, 28);
	pt(1, 20, 28);
	pt(2, 20, 20);
	check(gdos(&pb) == 0);		/* 4 */

	op(9, 4, 0);
	pt(0, 30, 5);
	pt(1, 40, 5);
	pt(2, 40, 12);
	pt(3, 30, 12);
	check(gdos(&pb) == 0);		/* 5 */

	op(8, 1, 1);
	intin[0] = 'T';
	pt(0, 44, 4);
	check(gdos(&pb) == 0);		/* 6 */

	op(2, 0, 0);
	check(gdos(&pb) == 0);		/* 7 */

	worked();
	for (i = 0; i < NB; i++)
		check(buf[i] == img[i]);	/* 8 on: the surface */
	for (i = NB; i < NB + GUARD; i++)
		check(buf[i] == 0x55);		/* guard */

	op(6, 3, 0);
	check(gdos(&pb) == -1);		/* closed */

	check(__bdos(BDOS_SETCON, 1L) == 0);	/* not console 0: no Open */
	check(__bdos(BDOS_GETCON, 0L) == 1);
	i = (int)bopen();
	__bdos(BDOS_SETCON, 0L);
	check(i == -1);

	check(bopen() == 0L);
	check(__bdos(BDOS_DETACH, 0L) == 0);
	diag();
	check(gdos(&pb) == -1);		/* unbound with the console */
	check(bopen() == -1L);		/* not its owner now */
	check(__bdos(BDOS_ATTACH, 0L) == 0);
	child('Z');
	check(all(0, NB));

	check(bopen() == 0L);		/* held through GDOST Y, left open */
	child('Y');
	check(all(0, NB));
	diag();
	check(gdos(&pb) == 0);
	cputs("GDOST OK\r\n");
	return (0);
}
