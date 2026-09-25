/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/* Choose console 0.  Linked into the boot module. */
#include "romabi.h"
#include "c900cfg.h"

/* crsmode()'s console kinds */
#define CK_SER	0
#define CK_LR	1

/* bootinfo.h's console codes.  Including the header would compile its
 * static helpers here, where nothing calls them. */
#define	BI_CON_SER	1
#define	BI_CON_LR	3
#define	BI_CON_HR	4

extern int mapseg();
extern crsmode(), hrinit();

/*
 * vprobe(base) -- is there framebuffer RAM at physical base<<8?
 *
 * COHERENT's vprobe: map a scratch segment onto the framebuffer's base,
 * save the word at offset 0, write and read back 0x55AA, then its
 * complement 0xAA55, and restore the word on either exit.  Both patterns
 * must survive.
 *
 * ONE ADDITION, for an undecoded bus.  COHERENT relies on undecoded reads
 * returning garbage.  If instead the bus
 * floated and held the last value driven onto it, a read straight after a
 * write would echo the pattern and pass.  So each write at offset 0 is
 * followed by a write of the OTHER pattern at offset 2 before offset 0 is
 * read back: an echo then returns the wrong pattern at both offsets and
 * fails.  Real framebuffer RAM has a word at offset 2 as well (a character
 * cell, or 16 bitmap pixels), and it is saved and restored the same way.
 */
static vprobe(base)
int base;
{
	register unsigned *p;
	unsigned s0, s2, r0, r2;
	int hit;

	mapseg(VPROBESEG, base, 0x02);
	p = (unsigned *)VPROBEADDR;
	s0 = p[0];
	s2 = p[1];
	p[0] = 0x55AA;
	p[1] = 0xAA55;
	r0 = p[0];
	r2 = p[1];
	hit = (r0 == 0x55AA && r2 == 0xAA55);
	if (hit) {
		p[0] = 0xAA55;
		p[1] = 0x55AA;
		r0 = p[0];
		r2 = p[1];
		hit = (r0 == 0xAA55 && r2 == 0x55AA);
	}
	p[0] = s0;
	p[1] = s2;
	mapseg(VPROBESEG, VPROBEHOME, 0x02);
	return (hit);
}

/*
 * crsinit(bicon) -- choose console 0, never from the ROM's
 * con_alt/con_hires flags.
 *
 *   BI_CON_SER			serial   } kboot DECIDED (its own probe, or
 *   BI_CON_LR			low-res  } the entry's `console serial'):
 *   BI_CON_HR			hi-res   } taken as sent, no probe here
 *
 *   BI_CON_ANY, BI_CON_VID,	FALLBACK: nobody decided, so probe HR,
 *   a value not known here,	then the text framebuffer, as COHERENT's
 *   no v3 handoff, or none	vidsel does; serial if
 *				neither answers.  This is how CP/M comes up
 *				from a loader that says nothing, or none.
 *
 * Returns the kind chosen: CK_SER is 0, so nonzero means video.
 */
crsinit(bicon)
int bicon;
{
	if (bicon == BI_CON_SER)
		return (crsmode(CK_SER));
	if (bicon == BI_CON_HR)
		return (hrinit());
	if (bicon == BI_CON_LR)
		return (crsmode(CK_LR));
	if (vprobe(VPHRBASE))
		return (hrinit());
	if (vprobe(VPLRBASE))
		return (crsmode(CK_LR));
	return (crsmode(CK_SER));
}
