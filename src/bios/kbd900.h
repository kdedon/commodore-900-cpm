/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/*
 * kbd900.h -- the video console's keyboard, read by the BIOS itself.
 *
 * Code, not declarations: a header so that one copy is #included by
 * bios900.c, in place of the ROM's kbd_poll (0x3f1e) and kbd_decode
 * (0x3f62), and compiled on the host by the tests.
 *
 * Behaviour follows COHERENT's Commodore 900 keyboard driver rather than
 * the ROM: its init and acknowledge sequences, its decode with arrows as
 * ESC A/D/C/B, and its scancode table dumped verbatim into the rows
 * below.  A code past the end of the table is dropped.
 *
 * COHERENT takes the port A pattern-match interrupt (vector 8).  The BIOS
 * polls the same interrupt-pending bit instead.  So kbdinit() writes
 * COHERENT's sequence minus its three interrupt writes (PAIV, PACS=C0
 * "set IE", MICR|=80).  IE stays clear, and the tick's vectored interrupt
 * never sees port A.
 *
 * Contract, the same as the ROM path it replaces: kbdpoll() returns 0 when
 * there is nothing, otherwise one 7-bit character.  Key-up events (PC2)
 * and modifier keys update the shift state and return nothing.  Codes
 * COHERENT produces at 0x80 and above (function keys, keypad-mode codes,
 * Alt+key) have no CP/M meaning and are dropped, as is NUL.  An arrow is
 * two characters, ESC and a letter.  The second one is queued and returned
 * by the next poll without touching the chip.
 */

#define KB_MCC		0x03	/* master configuration control		*/
#define KB_PCDPP	0x0b	/* port C data path polarity		*/
#define KB_PCDD		0x0d	/* port C data direction		*/
#define KB_PCSIOC	0x0f	/* port C special I/O control		*/
#define KB_PACS		0x11	/* port A command and status		*/
#define KB_PADATA	0x1b	/* port A data: KD0-KD6, PA7 = pattern	*/
#define KB_PCDATA	0x1f	/* port C data: PC2 key-up, PC3 KBDCLR	*/
#define KB_PAMS		0x41	/* port A mode specification		*/
#define KB_PAHS		0x43	/* port A handshake specification	*/
#define KB_PADPP	0x45	/* port A data path polarity		*/
#define KB_PADD		0x47	/* port A data direction		*/
#define KB_PASIOC	0x49	/* port A special I/O control		*/
#define KB_PAPPR	0x4b	/* port A pattern polarity		*/
#define KB_PAPTR	0x4d	/* port A pattern transition		*/
#define KB_PAPMR	0x4f	/* port A pattern mask			*/
#define KB_ENABLE	0x0205	/* keyboard enable (bit 0 is IEEE REN)	*/

#define KB_IP		0x20	/* PACS read: interrupt pending		*/
#define KB_CLRIP	0x20	/* PACS write: clear IP and IUS		*/
#define KB_UP		0x04	/* PCDATA: PC2 set = key released	*/
#define KB_CLR		0x70	/* PC3 low; PC0-PC2 write-protected	*/
#define KB_SET		0x78	/* PC3 high; the RTC's PC1 untouched	*/

/* k_flag bits, kbtab.h */
#define KF_DUP		0x01	/* send the character twice ("00")	*/
#define KF_CAP		0x02	/* caps lock selects the upper entry	*/
#define KF_LOCK		0x08	/* software-locking shift		*/
#define KF_INV		0x10	/* disabled key				*/
#define KF_C		0x20	/* has a control entry			*/
#define KF_SHIFT	0x40	/* a shift key; k_control is its bit	*/
#define KF_NL		0x80	/* num lock selects the upper entry	*/

/* shift-state bits, kbtab.h */
#define KS_S1		0x01	/* left shift				*/
#define KS_S2		0x02	/* right shift				*/
#define KS_NL		0x04	/* num lock				*/
#define KS_CL		0x08	/* caps lock				*/
#define KS_CT		0x10	/* control				*/
#define KS_AL		0x20	/* alt					*/

/* {k_flag, k_lower, k_upper, k_control} per scan code, from rec/kbtab.c */
static unsigned char kbtab[] = {
	0x10,0x00,0x00,0x00,	/* SC00 */
	0x00,0x1b,0x1b,0x00,	/* SC01 */
	0x00,0x31,0x21,0x00,	/* SC02 */
	0x20,0x32,0x40,0x00,	/* SC03 */
	0x00,0x33,0x23,0x00,	/* SC04 */
	0x00,0x34,0x24,0x00,	/* SC05 */
	0x00,0x35,0x25,0x00,	/* SC06 */
	0x00,0x36,0x5e,0x00,	/* SC07 */
	0x00,0x37,0x26,0x00,	/* SC08 */
	0x00,0x38,0x2a,0x00,	/* SC09 */
	0x00,0x39,0x28,0x00,	/* SC0A */
	0x00,0x30,0x29,0x00,	/* SC0B */
	0x00,0x2d,0x5f,0x00,	/* SC0C */
	0x00,0x3d,0x2b,0x00,	/* SC0D */
	0x00,0x08,0x08,0x00,	/* SC0E */
	0x00,0x09,0x09,0x00,	/* SC0F */
	0x22,0x71,0x51,0x11,	/* SC10 */
	0x22,0x77,0x57,0x17,	/* SC11 */
	0x22,0x65,0x45,0x05,	/* SC12 */
	0x22,0x72,0x52,0x12,	/* SC13 */
	0x22,0x74,0x54,0x14,	/* SC14 */
	0x22,0x79,0x59,0x19,	/* SC15 */
	0x22,0x75,0x55,0x15,	/* SC16 */
	0x22,0x69,0x49,0x09,	/* SC17 */
	0x22,0x6f,0x4f,0x0f,	/* SC18 */
	0x22,0x70,0x50,0x10,	/* SC19 */
	0x20,0x5b,0x7b,0x1b,	/* SC1A */
	0x20,0x5d,0x7d,0x1d,	/* SC1B */
	0x20,0x0d,0x0d,0x0d,	/* SC1C */
	0x40,0x00,0x00,0x10,	/* SC1D */
	0x22,0x61,0x41,0x01,	/* SC1E */
	0x22,0x73,0x53,0x13,	/* SC1F */
	0x22,0x64,0x44,0x04,	/* SC20 */
	0x22,0x66,0x46,0x06,	/* SC21 */
	0x22,0x67,0x47,0x07,	/* SC22 */
	0x22,0x68,0x48,0x08,	/* SC23 */
	0x22,0x6a,0x4a,0x0a,	/* SC24 */
	0x22,0x6b,0x4b,0x0b,	/* SC25 */
	0x22,0x6c,0x4c,0x0c,	/* SC26 */
	0x00,0x3b,0x3a,0x00,	/* SC27 */
	0x00,0x27,0x22,0x00,	/* SC28 */
	0x20,0x60,0x7e,0x00,	/* SC29 */
	0x40,0x00,0x00,0x01,	/* SC2A */
	0x20,0x5c,0x7c,0x1c,	/* SC2B */
	0x22,0x7a,0x5a,0x1a,	/* SC2C */
	0x22,0x78,0x58,0x18,	/* SC2D */
	0x22,0x63,0x43,0x03,	/* SC2E */
	0x22,0x76,0x56,0x16,	/* SC2F */
	0x22,0x62,0x42,0x02,	/* SC30 */
	0x22,0x6e,0x4e,0x0e,	/* SC31 */
	0x22,0x6d,0x4d,0x0d,	/* SC32 */
	0x00,0x2c,0x3c,0x00,	/* SC33 */
	0x00,0x2e,0x3e,0x00,	/* SC34 */
	0x00,0x2f,0x3f,0x00,	/* SC35 */
	0x40,0x00,0x00,0x02,	/* SC36 */
	0x04,0x2a,0x2a,0xd4,	/* SC37 */
	0x40,0x00,0x00,0x20,	/* SC38 */
	0x00,0x20,0x20,0x00,	/* SC39 */
	0x40,0x00,0x00,0x08,	/* SC3A */
	0x20,0xc0,0xc0,0xc0,	/* SC3B */
	0x20,0xc1,0xc1,0xc1,	/* SC3C */
	0x20,0xc2,0xc2,0xc2,	/* SC3D */
	0x20,0xc3,0xc3,0xc3,	/* SC3E */
	0x20,0xc4,0xc4,0xc4,	/* SC3F */
	0x20,0xc5,0xc5,0xc5,	/* SC40 */
	0x20,0xc6,0xc6,0xc6,	/* SC41 */
	0x20,0xc7,0xc7,0xc7,	/* SC42 */
	0x20,0xc8,0xc8,0xc8,	/* SC43 */
	0x20,0xc9,0xc9,0xc9,	/* SC44 */
	0x20,0xcd,0xcd,0xcd,	/* SC45 */
	0x20,0xce,0xce,0xce,	/* SC46 */
	0x04,0x37,0x37,0xdd,	/* SC47 */
	0x04,0x38,0x38,0xde,	/* SC48 */
	0x04,0x39,0x39,0xdf,	/* SC49 */
	0x04,0x2d,0x2d,0xe3,	/* SC4A */
	0x04,0x34,0x34,0xda,	/* SC4B */
	0x04,0x35,0x35,0xdb,	/* SC4C */
	0x04,0x36,0x36,0xdc,	/* SC4D */
	0x04,0x2b,0x2b,0xe2,	/* SC4E */
	0x04,0x31,0x31,0xd7,	/* SC4F */
	0x04,0x32,0x32,0xd8,	/* SC50 */
	0x04,0x33,0x33,0xd9,	/* SC51 */
	0x04,0x30,0x30,0xd6,	/* SC52 */
	0x05,0x30,0x30,0xe0,	/* SC53 */
	0x20,0xca,0xca,0xca,	/* SC54 */
	0x20,0x7f,0x7f,0x1f,	/* SC55 */
	0x20,0xcb,0xcb,0xcb,	/* SC56 */
	0x20,0xcc,0xcc,0xcc,	/* SC57 */
	0x04,0x01,0x01,0xd2,	/* SC58 */
	0x04,0x2e,0x2e,0xd3,	/* SC59 */
	0x04,0x2f,0x2f,0xd5,	/* SC5A */
	0x20,0xcf,0xcf,0xcf,	/* SC5B */
	0x20,0xd0,0xd0,0xd0,	/* SC5C */
	0x20,0xd1,0xd1,0xd1,	/* SC5D */
	0x04,0x0d,0x0d,0xe1,	/* SC5E */
	0x00,0x80,0x80,0x00,	/* SC5F */
	0x00,0x81,0x81,0x00,	/* SC60 */
	0x00,0x82,0x82,0x00,	/* SC61 */
	0x00,0x83,0x83,0x00,	/* SC62 */
	0x20,0xe4,0xe4,0xe4,	/* SC63 */
	0x20,0xe5,0xe5,0xe5	/* SC64 */
};
#define KB_NKEY		0x65	/* rows above				*/

/* cursor keys, indexed by CUP CLEFT CRIGHT CDOWN (0x80-0x83) */
static char kbcurs[] = { 'A', 'D', 'C', 'B' };

static int kbstate;		/* kbsstate: KS_ bits held		*/
static char kbq[2];		/* this key's characters		*/
static int kbqn;		/* how many kbq[] holds			*/
static int kbqi;		/* how many of those are delivered	*/

/*
 * Program port A for the keyboard, once, at console init: hrtty/kb.c
 * kbinit() in its own order, less the interrupt writes (see the top).
 */
kbdinit()
{
	outb(KB_PADD, 0xff);			/* port A all input	*/
	outb(KB_PCDD, (inb(KB_PCDD) & ~0x0c) | 0x04);	/* PC2 in, PC3 out */
	outb(KB_PADPP, 0);
	outb(KB_PCDPP, inb(KB_PCDPP) & ~0x0c);
	outb(KB_PASIOC, 0);
	outb(KB_PCSIOC, inb(KB_PCSIOC) & ~0x0c);
	outb(KB_PAMS, 0x1a);		/* single buffered, match only	*/
	outb(KB_PAHS, 0);
	outb(KB_PAPPR, 0x80);		/* match = PA7 high		*/
	outb(KB_PAPTR, 0x00);
	outb(KB_PAPMR, 0x80);
	outb(KB_PACS, KB_CLRIP);	/* drop a stale IP		*/
	outb(KB_MCC, inb(KB_MCC) | 0x04);	/* port A enable	*/
	outb(KB_ENABLE, 0x02);		/* keyboard enable		*/
	outb(KB_PCDATA, KB_CLR);	/* strobe KBDCLR		*/
	outb(KB_PCDATA, KB_SET);
	kbstate = 0;
	kbqn = kbqi = 0;
}

/*
 * One key event through rec/kb.c kbintr()'s state machine (keypad mode
 * off, device open).  Writes this key's characters to q[] and returns how
 * many: 0, 1 or 2.
 */
static int kbdkey(c, u, q)
register int c;
int u;
char *q;
{
	register unsigned char *kp;
	register int f;
	register int shift;

	if (c >= KB_NKEY)
		return (0);
	kp = &kbtab[c * 4];
	f = kp[0];
	if (f & KF_INV)
		return (0);
	if (f & (KF_SHIFT | KF_LOCK)) {
		if (f & KF_LOCK) {
			if (u)
				kbstate ^= kp[3];
		} else if (u)
			kbstate &= ~kp[3];
		else
			kbstate |= kp[3];
		return (0);
	}
	if (u)
		return (0);
	if (kbstate != 0) {
		shift = (kbstate & (KS_S1 | KS_S2)) != 0;
		if ((f & KF_CAP) != 0 && (kbstate & KS_CL) != 0
		 || (f & KF_NL) != 0 && (kbstate & KS_NL) != 0)
			shift ^= 1;
		if (kbstate & KS_CT) {
			if ((f & KF_C) == 0)
				return (0);
			c = kp[3];
		} else if (shift)
			c = kp[2];
		else
			c = kp[1];
		if (kbstate & KS_AL)
			c |= 0x80;
	} else
		c = kp[1];
	if (c >= 0x80 && c <= 0x83) {
		q[0] = 033;
		q[1] = kbcurs[c - 0x80];
		return (2);
	}
	if (c == 0 || (c & 0x80) != 0)
		return (0);
	q[0] = c;
	if (f & KF_DUP) {
		q[1] = c;
		return (2);
	}
	return (1);
}

/*
 * One non-blocking poll: a queued character, else the chip.  The read and
 * acknowledgement are kbintr():288-289 and kbintend(): PC2, PADATA, clear
 * IP, strobe PC3.
 */
kbdpoll()
{
	register int c;
	register int u;

	if (kbqi < kbqn)
		return (kbq[kbqi++] & 0x7f);
	if ((inb(KB_PACS) & KB_IP) == 0)
		return (0);
	u = inb(KB_PCDATA) & KB_UP;
	c = inb(KB_PADATA) & 0x7f;
	outb(KB_PACS, KB_CLRIP);
	outb(KB_PCDATA, KB_CLR);
	outb(KB_PCDATA, KB_SET);
	kbqn = kbdkey(c, u, kbq);
	kbqi = 0;
	if (kbqn == 0)
		return (0);
	kbqi = 1;
	return (kbq[0] & 0x7f);
}
