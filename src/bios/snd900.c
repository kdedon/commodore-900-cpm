/*
 * snd900.c -- speaker tone generator for the C900 CP/M-8000 BIOS.
 *
 * The speaker is driven from counter/timer 2 of the IEEE/sound Z8036
 * (U66, I/O 0x80-0xFF).  CT2 runs in continuous square-wave mode with
 * External Output Enable set, which puts its output on port B bit 0;
 * PB0 feeds three paralleled 7407 open-collector buffers and then the
 * speaker.  A Z8036 register number r is at port ZCIO2 + 2*r + 1.
 *
 * U66 is shared: PB1 is the Centronics ACK input, PB2-PB7 are IEEE-488
 * control lines and port C carries the Centronics strobe, so MCCR and
 * PBDD are read-modify-written and the pattern registers, MICR, the
 * interrupt vector and port C are left alone.  No counter here enables
 *
 * Port B has to be enabled in MCCR for a C/T waveform to leave the
 * chip, and the boot ROM leaves U66 unprogrammed -- port B disabled,
 * every data-direction bit reading as output.  So when this driver is
 * the one enabling the port it first writes the whole direction
 * register, leaving PB0 the only output; enabling the port with the
 * ROM's directions in place would drive the IEEE-488 lines.  The port
 * is disabled again with the tone, so it is live only while sounding.
 * When port B is already enabled the owner's directions stand and only
 * PB0 is claimed.
 *
 * The tone length is timed by CT1, programmed one-shot and polled --
 * PB4, an IEEE-488 control line, so CT1 runs with External Output
 * Enable CLEAR: it counts without touching any pin.  Its external gate,
 * trigger and count inputs (PB7/PB6/PB5) are likewise left disabled.
 */

#define	ZCIO2		0x80		/* Z8036 #2 base port		*/
#define	MCCR		(ZCIO2+0x03)	/* Master configuration control	*/
#define	CT1CS		(ZCIO2+0x15)	/* C/T 1 command and status	*/
#define	CT2CS		(ZCIO2+0x17)	/* C/T 2 command and status	*/
#define	CT1TCMSB	(ZCIO2+0x2D)	/* C/T 1 time constant high	*/
#define	CT1TCLSB	(ZCIO2+0x2F)	/* C/T 1 time constant low	*/
#define	CT2TCMSB	(ZCIO2+0x31)	/* C/T 2 time constant high	*/
#define	CT2TCLSB	(ZCIO2+0x33)	/* C/T 2 time constant low	*/
#define	CT1MS		(ZCIO2+0x39)	/* C/T 1 mode specification	*/
#define	CT2MS		(ZCIO2+0x3B)	/* C/T 2 mode specification	*/
#define	PBDD		(ZCIO2+0x57)	/* Port B data direction	*/

#define	MCCR_PBE	0x80		/* Port B enable		*/
#define	MCCR_CT1E	0x40		/* Counter/timer 1 enable	*/
#define	MCCR_CT2E	0x20		/* Counter/timer 2 enable	*/
#define	CT1MS_DELAY	0x01		/* Single cycle, no external
					   output, one-shot		*/
#define	CT2MS_TONE	0xC2		/* Continuous, external output
					   enable, square wave		*/
#define	CTCS_RUN	0x06		/* Gate command + trigger command */
#define	CTCS_HALT	0x00		/* Gate closed			*/
#define	CTCS_CIP	0x01		/* Count in progress (read only)  */
#define	CT2MS_OFF	0x00		/* No external output: PB0 back to
					   the port data register	*/
#define	PBDD_PB0OUT	0xFE		/* Mask making PB0 an output	*/
#define	PBDD_SNDONLY	0xFE		/* PB0 output, PB1-PB7 inputs	*/

/*
 * The counters clock at half U66's PCLK, and U66's PCLK is SNDCLK,
 * 750 kHz.  A square wave toggles once per time-constant expiry, so
 *	f = SNDCLK/2 / (2 * TC)		TC = SNDCLK / (4 * f)
 * The time constant is 16 bits, which bounds both the frequency range
 * and the longest single one-shot delay: 65535 counts at SNDCLK/2 is
 * 174 ms, so SNDMAXMS milliseconds per CT1 count.
 */
#define	SNDCLK		750000L		/* U66 PCLK, Hz			*/
#define	SNDPERMS	375		/* Counter clocks per millisecond */
#define	SNDMAXMS	174		/* Longest one-shot delay, ms	*/
#define	SNDFMIN		3		/* Lowest frequency TC can express */
#define	SNDFMAX		20000		/* Highest frequency worth asking for */

#define	SNDPITCH	440		/* Bell tone, Hz		*/
#define	SNDLEN		125		/* Bell duration, ms		*/

/*
 * Poll bound for the duration wait.  One iteration is a subroutine call
 * and a byte input, tens of CPU cycles, so 65535 of them span far more
 * than SNDMAXMS of real time; the bound exists only so that a counter
 * that never reports terminal count cannot hang the BIOS with the
 * speaker on.
 */
#define	SNDPOLLS	65535

extern int inb();
extern outb();

static int sndpbe;	/* nonzero: this driver enabled port B */

/*
 * Stop the tone: close the software gates, release PB0 back to the port
 * data register, then drop the CT1 and CT2 enables in MCCR (and port B
 * itself if this driver enabled it) without disturbing the port A and
 * CT3 bits that belong to the IEEE-488 and Centronics lines.
 */
sndquiet()
{
	register int mccr;

	outb(CT2CS, CTCS_HALT);
	outb(CT1CS, CTCS_HALT);
	outb(CT2MS, CT2MS_OFF);
	mccr = inb(MCCR) & ~(MCCR_CT1E|MCCR_CT2E);
	if (sndpbe) {
		mccr &= ~MCCR_PBE;
		sndpbe = 0;
	}
	outb(MCCR, mccr);
}

/*
 * Sound `freq' Hz and start the `ms' millisecond duration timer, then
 * return.  The tone continues until sndwait() sees the timer expire or
 * sndquiet() is called.  A call while a tone is sounding retunes it and
 * restarts the duration.
 */
sndtone(freq, ms)
int freq;
unsigned ms;
{
	register unsigned tc;
	register unsigned dc;
	register int mccr;

	if (freq < SNDFMIN || freq > SNDFMAX || ms == 0)
		return;
	if (ms > SNDMAXMS)
		ms = SNDMAXMS;
	tc = (unsigned)(SNDCLK / (4L * (long)freq));
	dc = (unsigned)((long)ms * (long)SNDPERMS);

	mccr = inb(MCCR);
	if ((mccr & MCCR_PBE) == 0) {
		outb(PBDD, PBDD_SNDONLY);
		sndpbe = 1;
	} else
		outb(PBDD, inb(PBDD) & PBDD_PB0OUT);
	outb(CT2MS, CT2MS_TONE);
	outb(CT2TCMSB, tc >> 8);
	outb(CT2TCLSB, tc & 0xFF);
	outb(CT1MS, CT1MS_DELAY);
	outb(CT1TCMSB, dc >> 8);
	outb(CT1TCLSB, dc & 0xFF);
	outb(MCCR, mccr | MCCR_PBE | MCCR_CT1E | MCCR_CT2E);
	outb(CT2CS, CTCS_RUN);
	outb(CT1CS, CTCS_RUN);
}

sndwait()
{
	register unsigned n;

		if ((inb(CT1CS) & CTCS_CIP) == 0)
			return;
}

sndbeep()
{
	sndtone(SNDPITCH, (unsigned)SNDLEN);
	sndwait();
	sndquiet();
}
