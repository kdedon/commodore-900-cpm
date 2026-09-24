/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/*
 * tektest.c - the Tek 4010 parser on the host, against a model.
 *
 * Each stream is fed to the parser over a surface split across two
 * buffers, most splits inside a raster, and in parallel drawn into an
 * array of one value per pixel: the scaling and the lines worked out
 * here by closed formula.  The array is packed into the bytes the
 * surface should hold and compared, with guard bytes past each buffer.
 * The alpha text the parser hands back is compared too.
 *
 * Build: cc -std=gnu89 (the parser and library are K&R).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gfx.h"
#include "tek.h"

#define	GUARD	4
#define	SENT	0xA5

static int fails;

/* ------------------------------------------------------------ the model */

static unsigned char *model;
static int mw, mh, mcol;

static int mx(x)
int x;
{
	return ((int) (((long) x * (mw - 1) * 2 + 1023) / 2046));
}

static int my(y)
int y;
{
	return (mh - 1 - (int) (((long) y * (mh - 1) * 2 + 779) / 1558));
}

static void mset(x, y)
int x, y;
{
	if (x >= 0 && y >= 0 && x < mw && y < mh)
		model[y * mw + x] = mcol;
}

static int iabs(a)
int a;
{
	return (a < 0 ? -a : a);
}

/* Step i's minor offset is round(i * minor / major), halves away from the start. */
static void mline(x0, y0, x1, y1)
int x0, y0, x1, y1;
{
	int dx, dy, sx, sy, i, n;

	dx = iabs(x1 - x0);
	dy = iabs(y1 - y0);
	sx = x1 < x0 ? -1 : 1;
	sy = y1 < y0 ? -1 : 1;
	if (dx >= dy) {
		for (i = 0; i <= dx; i++) {
			n = dx ? (int) ((2L * dy * i + dx) / (2L * dx)) : 0;
			mset(x0 + sx * i, y0 + sy * n);
		}
	} else {
		for (i = 0; i <= dy; i++) {
			n = (int) ((2L * dx * i + dy) / (2L * dy));
			mset(x0 + sx * n, y0 + sy * i);
		}
	}
}

/* Tek-unit segments and points the stream should produce */
struct seg {
	int	x0, y0, x1, y1;
};

/* ---------------------------------------------------------- the streams */

#define	HY(y)	(0x20 | ((y) >> 5))
#define	LY(y)	(0x60 | ((y) & 31))
#define	HX(x)	(0x20 | ((x) >> 5))
#define	LX(x)	(0x40 | ((x) & 31))
#define	P(x, y)	HY(y), LY(y), HX(x), LX(x)

#define	GS	0x1d
#define	FS	0x1c
#define	US	0x1f
#define	ESC	0x1b
#define	END	(-1)

struct stream {
	char		*name;
	int		bytes[64];
	struct seg	segs[8];	/* x0 < 0 ends; x1 < 0 is a point */
	char		*alpha;
};

static struct stream streams[] = {
	{ "full addresses",
	  { GS, P(100, 100), P(900, 100), P(900, 700), P(100, 100), US, END },
	  { { 100, 100, 900, 100 }, { 900, 100, 900, 700 },
	    { 900, 700, 100, 100 }, { -1 } },
	  "" },
	/*
	 * (512,390), then Lo-X alone, Lo-Y + Lo-X, Hi-Y + Lo-X,
	 * Lo-Y + Hi-X + Lo-X, and a 4014 extra byte (a second Lo-Y)
	 * that the last Lo-Y overrides.
	 */
	{ "short forms",
	  { GS, P(512, 390), 0x48, 0x70, 0x48, 0x2d, 0x48,
	    0x71, 0x35, 0x5c, 0x21, 0x6f, 0x63, 0x20, 0x4a, US, END },
	  { { 512, 390, 520, 390 }, { 520, 390, 520, 400 },
	    { 520, 400, 520, 432 }, { 520, 432, 700, 433 },
	    { 700, 433, 10, 35 }, { -1 } },
	  "" },
	{ "mixed alpha",
	  { 'A', 'B', GS, P(0, 0), '\n', 0, P(1023, 779), US, 'C', 'D',
	    GS, P(1023, 0), P(0, 779), '\r', 'E', END },
	  { { 0, 0, 1023, 779 }, { 1023, 0, 0, 779 }, { -1 } },
	  "ABCD\rE" },
	{ "second GS is dark",
	  { GS, P(50, 60), P(300, 60), GS, P(50, 500), P(300, 520), US,
	    END },
	  { { 50, 60, 300, 60 }, { 50, 500, 300, 520 }, { -1 } },
	  "" },
	{ "points",
	  { FS, P(0, 0), P(1023, 779), P(512, 390), US, 'x', END },
	  { { 0, 0, -1 }, { 1023, 779, -1 }, { 512, 390, -1 }, { -1 } },
	  "x" },
	{ "off the top",
	  { GS, P(200, 300), P(400, 1000), US, END },
	  { { 200, 300, 400, 1000 }, { -1 } },
	  "" },
	/* ESC FF clears and leaves alpha; ESC and another byte is eaten */
	{ "erase",
	  { GS, P(10, 10), P(1000, 770), ESC, '\f', 'y',
	    GS, P(30, 40), ESC, '8', P(60, 40), US, END },
	  { { 30, 40, 60, 40 }, { -1 } },
	  "y" },
};

#define	NSTREAM	(sizeof(streams) / sizeof(streams[0]))

/* ------------------------------------------------------------- the rig */

struct geom {
	char	*name;
	int	w, h, bpp;
	unsigned stride;
	long	splits[4];	/* 0 ends */
};

static struct geom geoms[] = {
	{ "HR mono", 1024, 800, 1, 128, { 128L * 400 + 37, HRSPLIT, 1 } },
	{ "HR colour", 640, 400, 4, 320, { HRSPLIT, 320L * 100 + 3 } },
	{ "mono 100x60", 100, 60, 1, 13, { 13L * 30 + 6, 13L * 59 + 12 } },
	{ "colour 51x30", 51, 30, 4, 26, { 26L * 15 + 11, 7 } },
};

#define	NGEOM	(sizeof(geoms) / sizeof(geoms[0]))

/* the model packed into the bytes a surface holds */
static void pack(g, out)
struct geom *g;
unsigned char *out;
{
	int x, y, v;
	long o;

	memset(out, 0, (size_t) g->stride * g->h);
	for (y = 0; y < g->h; y++)
		for (x = 0; x < g->w; x++) {
			v = model[y * g->w + x];
			if (g->bpp == 1) {
				o = (long) y * g->stride + x / 8;
				out[o] |= v << (7 - x % 8);
			} else {
				o = (long) y * g->stride + x / 2;
				out[o] |= v << (x % 2 ? 0 : 4);
			}
		}
}

static void run(g, split, st)
struct geom *g;
long split;
struct stream *st;
{
	struct gfxsurf s;
	struct tek t;
	unsigned char *b0, *b1, *want;
	long size, o;
	int i, got;
	char alpha[64];
	struct seg *sg;

	size = (long) g->stride * g->h;
	b0 = malloc(split + GUARD);
	b1 = malloc(size - split + GUARD);
	want = malloc(size);
	model = calloc(g->w * g->h, 1);
	memset(b0, 0, split);
	memset(b0 + split, SENT, GUARD);
	memset(b1, 0, size - split);
	memset(b1 + size - split, SENT, GUARD);
	mw = g->w;
	mh = g->h;
	mcol = (1 << g->bpp) - 1;

	gfx_ram(&s, g->w, g->h, g->bpp, g->stride, b0, b1, split);
	tekinit(&t, &s);
	got = 0;
	for (i = 0; st->bytes[i] != END; i++)
		if (tekput(&t, st->bytes[i]) && got < 63)
			alpha[got++] = st->bytes[i];
	alpha[got] = 0;

	for (sg = st->segs; sg->x0 >= 0; sg++)
		if (sg->x1 < 0)
			mset(mx(sg->x0), my(sg->y0));
		else
			mline(mx(sg->x0), my(sg->y0), mx(sg->x1), my(sg->y1));
	pack(g, want);

	if (strcmp(alpha, st->alpha) != 0) {
		printf("tektest: FAIL %s, %s: alpha \"%s\", want \"%s\"\n",
		    g->name, st->name, alpha, st->alpha);
		fails++;
	}
	if (t.mode != TEK_ALPHA) {
		printf("tektest: FAIL %s, %s: left in mode %d\n",
		    g->name, st->name, t.mode);
		fails++;
	}
	for (o = 0; o < size; o++) {
		got = o < split ? b0[o] : b1[o - split];
		if (got != want[o]) {
			printf("tektest: FAIL %s split %ld, %s: byte %ld "
			    "(raster %ld) is %02x, want %02x\n", g->name,
			    split, st->name, o, o / g->stride, got, want[o]);
			fails++;
			break;
		}
	}
	for (i = 0; i < GUARD; i++)
		if (b0[split + i] != SENT || b1[size - split + i] != SENT) {
			printf("tektest: FAIL %s split %ld, %s: guard hit\n",
			    g->name, split, st->name);
			fails++;
			break;
		}
	free(b0);
	free(b1);
	free(want);
	free(model);
}

/* The scaling, spelled out for the two HR geometries. */
static void corners()
{
	static struct {
		int w, h, tx, ty, x, y;
	} c[] = {
		{ 1024, 800, 0, 0, 0, 799 },
		{ 1024, 800, 1023, 779, 1023, 0 },
		{ 1024, 800, 100, 100, 100, 696 },
		{ 640, 400, 0, 0, 0, 399 },
		{ 640, 400, 1023, 779, 639, 0 },
		{ 640, 400, 100, 100, 62, 348 },
		{ 640, 400, 512, 390, 320, 199 },
	};
	int i;

	for (i = 0; i < (int) (sizeof(c) / sizeof(c[0])); i++) {
		mw = c[i].w;
		mh = c[i].h;
		if (mx(c[i].tx) != c[i].x || my(c[i].ty) != c[i].y) {
			printf("tektest: FAIL scale %dx%d (%d,%d) -> (%d,%d), "
			    "want (%d,%d)\n", mw, mh, c[i].tx, c[i].ty,
			    mx(c[i].tx), my(c[i].ty), c[i].x, c[i].y);
			fails++;
		}
	}
}

int main()
{
	int g, k, n;

	corners();
	n = 0;
	for (g = 0; g < (int) NGEOM; g++)
		for (k = 0; geoms[g].splits[k]; k++) {
			int j;

			for (j = 0; j < (int) NSTREAM; j++, n++)
				run(&geoms[g], geoms[g].splits[k], &streams[j]);
		}
	if (fails) {
		printf("tektest: %d failures\n", fails);
		return (1);
	}
	printf("tektest: ok, %d runs\n", n);
	return (0);
}
