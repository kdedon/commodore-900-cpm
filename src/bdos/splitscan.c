/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/*
 * splitscan.c -- the scan-and-patch half of the split-I/D shim.
 *
 * Runs in the relocated module (segment SPLITMSEG), entered as spscan()
 * through the module header's second slot.  The resident half
 * (splitld.c) has already mapped the data bank and the side-table
 * segment and cleared the side table and the scanner's LDR-target
 * bitmap; everything from here down is the walk itself.
 *
 * The shared linear scanner (zsplit.c) visits every instruction of the
 * freshly loaded text.  Each data-space-referencing instruction has its
 * first word recorded in the side table and replaced with SC #255; the
 * trap handler (splitsc.c) does the rest at run time.
 */

#include "c900cfg.h"
#include "zsplit.h"

extern int zscan();
extern int zsfix();

/* splitsc.c bank/table state (module-local) */
extern char	*spcode;
extern char	*spdata;
extern zw	*sptab;

/* splitent.s: the module header's live words */
extern zw	sptop;

static zw *sptext;	/* the loaded text, as words (the code bank) */

static int patch1(arg, off, w0, idp)
char *arg;
zw off, w0;
struct zid *idp;
{
	sptab[off] = w0;
	sptext[off] = SPLITSCW;
	return (0);
}

/* A marked word is embedded program data (an LDR target); if a patch
 * landed on it before the mark did, put the original word back. */
static int unpatch1(arg, off)
char *arg;
zw off;
{
	if (sptab[off] != 0) {
		sptext[off] = sptab[off];
		sptab[off] = 0;
	}
	return (0);
}

/*
 * Scan and patch the freshly loaded text (tsize bytes).  Returns 0.
 * A nonzero return from zscan1 (illegal encodings met) is tolerated:
 * the offline validation showed all known 0xEE0B binaries decode
 * cleanly, and an unknown binary's stray data decodes conservatively
 * (an unpatched region traps PRV/EPU or runs -- never silently reads
 * the wrong bank for patched instructions).
 */
int spscan(tsize)
zw tsize;
{
	struct zsctx cx;

	spcode = (char *)TPABASE;
	spdata = (char *)SPLITDBASE;
	sptab = (zw *)SPLITTBASE;
	sptext = (zw *)TPABASE;
	sptop = tsize;

	cx.text = (zw *)TPABASE;
	cx.nw = tsize >> 1;
	cx.mark = (char *)(SPLITTBASE + (long)SPLITMAXT);
	zscan(&cx, patch1, (char *)0);
	zsfix(&cx, unpatch1, (char *)0);
	return (0);
}
