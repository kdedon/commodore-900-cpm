/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/*
 * kbdoracle.c -- COHERENT's own Commodore 900 keyboard driver, compiled on
 * the host as the oracle for src/bios/kbd900.c (see tests/kbdtest.c).
 *
 * Nothing of the driver is copied.  The make rule passes -I for the
 * original C900 driver directory (commodore-900-coh-kernel3,
 * os/sys/z8001/rec).  This file #includes its kb.c unmodified, and
 * kbdorat.c its kbtab.c.  The kernel services the driver calls are stubbed below.
 * Port I/O goes to an array, so the register writes can be compared too.
 */

#include <stdio.h>
#include <string.h>

typedef int dev_t;

static unsigned char oport[0x10000];
static int olog[64][2];
static int olnum;
static unsigned char obuf[8];
static int on;

static int oinb(p)
int p;
{
	return (oport[p & 0xffff]);
}

static void ooutb(p, v)
int p, v;
{
	if (olnum < 64) {
		olog[olnum][0] = p;
		olog[olnum][1] = v & 0xff;
		olnum++;
	}
	oport[p & 0xffff] = v;
}

int v0in(c)
int c;
{
	if (on < (int)sizeof(obuf))
		obuf[on++] = c;
	return (c);
}

static int sphi() { return (0); }
static void spl(s) int s; { }
static void setivec(v, f) int v; void (*f)(); { }
static void clrivec(v) int v; { }

/* rec/kb.c declares these extern and defines them static, which its MWC
 * compiler took and gcc does not; a static declaration first is valid C. */
static void kbintr();
static void kbinit();
static void kbterm();
static void kbintend();

#define inb(p)		oinb(p)
#define outb(p, v)	ooutb(p, v)

#include "kb.c"

extern int oranktab();		/* tests/kbdorat.c: the table's extent	*/

/* Run kbinit() from `preset' port values; the writes go to log[]. */
int orainit(preset, log)
unsigned char *preset;
int (*log)[2];
{
	memcpy(oport, preset, sizeof oport);
	olnum = 0;
	kbinit();
	memcpy(log, olog, sizeof olog);
	return (olnum);
}

/*
 * One key event (scan code c, key-up u) in shift state s, through
 * kbintr().  Returns the bytes queued to the tty in out[], and their
 * count; *ns is the shift state afterwards.
 */
int orakey(c, u, s, ns, out)
int c, u, s;
int *ns;
unsigned char *out;
{
	if (c >= oranktab()) {		/* hrtty/kb.c:299: dropped	*/
		*ns = s;
		return (0);
	}
	kbsstate = s;
	kbopenf = 1;
	kbkeypad = 0;
	oport[0x1f] = u ? 0x04 : 0x00;
	oport[0x1b] = c | 0x80;		/* PA7: the pattern bit		*/
	on = 0;
	kbintr(0);
	*ns = kbsstate;
	memcpy(out, obuf, on);
	return (on);
}
