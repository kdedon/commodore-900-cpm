/*
 * rtcchip.c -- a software OKI MSM58321 behind a software Z8036 Z-CIO #1,
 * for the host unit tests in host/rtctest.c.
 *
 * The driver under test (src/rtc900.c) reaches the chip ONLY through
 * inb()/outb() on six CIO ports, so standing in for the CIO and the module
 * is enough to run the real driver on the host.  Everything here follows
 * ~/git/C900/docs/RTC-58321.pdf, not the driver:
 *
 *  - Register Table: sixteen 4-bit registers, 0..0xC time and calendar,
 *    0xD the post-stage reset, 0xE/0xF the standard-signal select.  The
 *    address comes from a separate ADDRESS LATCH (block diagram) and there
 *    is NO auto-increment.
 *  - Supplement, "* mark: Writable.  Recognized as 0 while in read mode":
 *    the unused high bits of S10, MI10, W and MO10 read back 0, as does
 *    the whole reset register.  H10's 24/12 and PM/AM bits and D10's
 *    leap-year selection bits carry no "*" and read back normally.
 *  - Supplement, "PM/AM": "In 24 H mode, this will be 0".
 *  - Supplement, "D3 and D2 of 10 days digit": the leap-year selection
 *    code is 00 for a surplus of 0 (the leap year itself) and 01/10/11 for
 *    a surplus of 3/2/1 after dividing the year by 4.  February is 29 days
 *    long exactly when the code is 00.
 *  - Block diagram: the data pins are driven by a TRI-STATE CONTROL that
 *    is enabled by CS + READ.  At every other time the chip is off the
 *    bus; nothing routes the address latch back to the pins, so the module
 *    can never echo the address it was given.  What a CPU sees at those
 *    times is decided by the CIO and the board, not by the module, which
 *    is why float_mode below is a property of the model and not of the
 *    chip.
 *
 * Two things a real chip does that are NOT modelled, both deliberately:
 * BUSY (pin 14 is not wired on the C900) and the sub-microsecond switching
 * characteristics.  The datasheet's setup/hold/pulse-width figures are
 * sub-microsecond and a Z8001 OUTB takes several instructions, so no pulse
 * this driver generates should ever be too short -- but that is reasoning
 * from the timing table, not a bench measurement, and remains unverified.
 *
 * The clock does not run on its own here.  Tests inject carries at exactly
 * the register read they want one at, which is what makes the read-while-
 * ticking race reproducible rather than lucky.
 */
#include <stdio.h>
#include <string.h>

#include "rtcchip.h"

struct chip Chip;

#define PB_D		0x0f
#define PB_READ		0x10
#define PB_WRITE	0x20
#define PB_ADWR		0x40
#define PB_STOP		0x80

/* Bits a read returns; 0 where the Supplement's "*" says "recognized as 0
 * while in read mode".  Index = register address. */
static const unsigned char rdmask[16] = {
	0x0f, 0x07, 0x0f, 0x07, 0x0f, 0x0f, 0x07, 0x0f,
	0x0f, 0x0f, 0x01, 0x0f, 0x0f, 0x00, 0x00, 0x00
};

static void fault(const char *what)
{
	if (Chip.nfault < CHIP_MAXFAULT)
		Chip.fault[Chip.nfault] = what;
	Chip.nfault++;
}

void chip_reset(void)
{
	memset(&Chip, 0, sizeof Chip);
	Chip.present = 1;
	Chip.float_mode = FLOAT_LATCH;
	Chip.pc_pins = 0x05;		/* PC0 and PC2 read high		*/
	Chip.pcdd = 0x07;		/* what the ROM's kbd_init leaves	*/
	Chip.pcdata = 0x08;		/* PC3 = 1, the keyboard FIFO ack	*/
	Chip.pbdd = 0xff;
	Chip.tick_at_read = -1;
}

/* ---- the calendar behind the register file ---- */

static int mlen(int mo, int leap)
{
	static const int d[13] = {0,31,28,31,30,31,30,31,31,30,31,30,31};

	if (mo < 1 || mo > 12)
		return (31);
	return (mo == 2 && leap) ? 29 : d[mo];
}

/*
 * One second of carry propagation through the digit chain.  Decomposed and
 * recomposed rather than done digit by digit: the observable behaviour is
 * the same and the modulus of each field is then plain to read.
 */
void chip_tick(void)
{
	unsigned char *r = Chip.reg;
	int s, mi, h, d, mo, y, leap, pm, h24, roll;

	if (Chip.stopped) {
		Chip.nsuppressed++;
		return;
	}
	Chip.ntick++;

	s = (r[1] & 7) * 10 + (r[0] & 15);
	mi = (r[3] & 7) * 10 + (r[2] & 15);
	h24 = (r[5] & 8) != 0;
	pm = (r[5] & 4) != 0;
	h = (r[5] & 3) * 10 + (r[4] & 15);
	d = (r[8] & 3) * 10 + (r[7] & 15);
	mo = (r[10] & 1) * 10 + (r[9] & 15);
	y = (r[12] & 15) * 10 + (r[11] & 15);
	leap = (r[8] >> 2) & 3;

	roll = 0;
	if (++s > 59) {
		s = 0;
		if (++mi > 59) {
			mi = 0;
			if (h24) {
				if (++h > 23) { h = 0; roll = 1; }
			} else {
				if (++h > 12)
					h = 1;
				else if (h == 12) {
					pm = !pm;	/* 11 -> 12 flips	*/
					if (!pm)
						roll = 1;
				}
			}
		}
	}
	if (roll) {
		r[6] = (unsigned char)((r[6] + 1) % 7);	/* W free-runs	*/
		if (++d > mlen(mo, leap == 0)) {
			d = 1;
			if (++mo > 12) {
				mo = 1;
				y = (y + 1) % 100;
				/* The leap-year selection counts DOWN: code
				 * 00,11,10,01 is a surplus of 0,1,2,3, so a
				 * year later is a code lower.  Whether the
				 * chip does this itself is an inference from
				 * the datasheet's "leap year automatically
				 * adjustable" feature note, which says
				 * nothing else about it: the datasheet does
				 * not confirm the chip advances the field,
				 * but the countdown code only earns its
				 * shape if something decrements it. */
				leap = (leap + 3) & 3;
			}
		}
	}

	r[0] = (unsigned char)(s % 10);   r[1] = (unsigned char)(s / 10);
	r[2] = (unsigned char)(mi % 10);  r[3] = (unsigned char)(mi / 10);
	r[4] = (unsigned char)(h % 10);
	r[5] = (unsigned char)((h24 ? 8 : 0) | (pm ? 4 : 0) | (h / 10));
	r[7] = (unsigned char)(d % 10);
	r[8] = (unsigned char)((leap << 2) | (d / 10));
	r[9] = (unsigned char)(mo % 10);  r[10] = (unsigned char)(mo / 10);
	r[11] = (unsigned char)(y % 10);  r[12] = (unsigned char)(y / 10);
}

/* Seed the module the way a battery-backed part would come up. */
void chip_seed(int y, int mo, int d, int h, int mi, int s, int wd)
{
	unsigned char *r = Chip.reg;

	r[0] = (unsigned char)(s % 10);   r[1] = (unsigned char)(s / 10);
	r[2] = (unsigned char)(mi % 10);  r[3] = (unsigned char)(mi / 10);
	r[4] = (unsigned char)(h % 10);   r[5] = (unsigned char)(8 | (h / 10));
	r[6] = (unsigned char)wd;
	r[7] = (unsigned char)(d % 10);
	r[8] = (unsigned char)((((4 - (y & 3)) & 3) << 2) | (d / 10));
	r[9] = (unsigned char)(mo % 10);  r[10] = (unsigned char)(mo / 10);
	r[11] = (unsigned char)((y % 100) % 10);
	r[12] = (unsigned char)((y % 100) / 10);
}

/* ---- the CIO ---- */

static int cs_asserted(void)
{
	if (Chip.pcdd & 0x02)			/* PC1 still an input	*/
		return (0);
	return ((Chip.pcdata & 0x02) == 0);
}

/* what the module drives onto D0..D3, or -1 for "off the bus" */
static int chip_drives(void)
{
	int v;

	if (!Chip.present || !cs_asserted())
		return (-1);
	if (!(Chip.pbdata & PB_READ) || (Chip.pbdd & PB_READ))
		return (-1);			/* READ not driven high	*/
	v = Chip.reg[Chip.addr] & rdmask[Chip.addr];
	if (Chip.addr == 5 && (Chip.reg[5] & 8))
		v &= ~0x04;			/* PM/AM reads 0 in 24H	*/
	return (v);
}

int inb(port)
int port;
{
	int ins, pins, v;

	switch (port) {
	case 0x0003:
		return (Chip.mccr);
	case 0x0051:
		return (Chip.pbms);
	case 0x0057:
		return (Chip.pbdd);
	case 0x000d:
		return (Chip.pcdd);
	case 0x001f:
		return ((Chip.pcdata & ~Chip.pcdd) | (Chip.pc_pins & Chip.pcdd));
	case 0x001d:
		break;
	default:
		fault("read of a port the driver has no business touching");
		return (0);
	}

	/* Port B.  Bits the CIO drives read back as its output latch; bits
	 * it has programmed as inputs read the pin. */
	ins = Chip.pbdd & 0xff;
	v = chip_drives();
	if (v >= 0) {
		pins = v;
		if (!(ins & PB_D))
			fault("READ sampled with D0..D3 still outputs");
		Chip.nread++;
		if (Chip.nread == Chip.tick_at_read)
			chip_tick();
		if (Chip.tick_every && (Chip.nread % Chip.tick_every) == 0)
			chip_tick();
	} else if (Chip.float_mode == FLOAT_LATCH)
		pins = Chip.pbdata & PB_D;	/* nothing driving: the bus */
	else					/* keeps whatever the CIO   */
		pins = PB_D;			/* last left, or pulls up   */
	pins |= Chip.pbdata & ~PB_D;		/* strobes are CPU outputs  */
	return ((Chip.pbdata & ~ins) | (pins & ins));
}

outb(port, val)
int port, val;
{
	int drv, old, v;

	val &= 0xff;
	switch (port) {
	case 0x0003:
		Chip.mccr = val;
		return (0);
	case 0x0051:
		Chip.pbms = val;
		return (0);
	case 0x0057:
		Chip.pbdd = val;
		return (0);
	case 0x000d:
		Chip.pcdd = val;
		return (0);
	case 0x001f:
		if (!(val & 0x08))
			Chip.pc3_lost++;	/* keyboard FIFO ack dropped */
		Chip.pcdata = val;
		if (cs_asserted() && !Chip.cs)
			Chip.ncs++;
		Chip.cs = cs_asserted();
		return (0);
	case 0x001d:
		break;
	default:
		fault("write to a port the driver has no business touching");
		return (0);
	}

	old = Chip.pbdata & ~Chip.pbdd;		/* previously driven levels */
	Chip.pbdata = val;
	drv = val & ~Chip.pbdd;			/* driven now		    */

	if (!Chip.present)
		return (0);

	if ((drv & PB_ADWR) && !(old & PB_ADWR)) {
		if (!cs_asserted())
			fault("ADDRESS WRITE strobe with /CS deasserted");
		else
			Chip.addr = drv & PB_D;
	}
	if ((drv & PB_WRITE) && !(old & PB_WRITE)) {
		if (!cs_asserted())
			fault("WRITE strobe with /CS deasserted");
		else {
			if (Chip.pbdd & PB_D)
				fault("WRITE with D0..D3 programmed as inputs");
			Chip.nwrite++;
			if (Chip.drop_write == Chip.addr + 1)
				;		/* the write is swallowed   */
			else if (Chip.addr == 13) {
				Chip.nreset++;
				Chip.reset_while_stopped = Chip.stopped;
			} else
				Chip.reg[Chip.addr] = (unsigned char)(drv & PB_D);
		}
	}
	if ((drv & PB_READ) && !(old & PB_READ) && !cs_asserted())
		fault("READ strobe with /CS deasserted");

	v = (drv & PB_STOP) != 0;
	if (v && !Chip.stopped)
		Chip.nstop++;
	Chip.stopped = v;
	return (0);
}

/*
 * The BIOS's far-memory move.  On the host an XADDR carries a real host
 * address, so this is a plain copy -- the far-pointer arithmetic itself is
 * the compiler's business and is exercised on the target.
 */
mem_cpy(from, to, len)
long from, to, len;
{
	memcpy((void *)(long)to, (void *)(long)from, (size_t)len);
	return (0);
}
