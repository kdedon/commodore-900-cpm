/*
 * rtcchip.h -- the software MSM58321 + Z-CIO the host RTC tests drive the
 * real driver against.  See host/rtcchip.c.
 */
#ifndef RTCCHIP_H
#define RTCCHIP_H

#define CHIP_MAXFAULT	8

/* What a CPU sees on D0..D3 when nothing is driving them.  Which of these
 * a real board does is unknown -- no known software distinguishes them,
 * and this model exists to make the driver reject both. */
#define FLOAT_LATCH	0	/* the CIO's own output latch reads back	*/
#define FLOAT_HIGH	1	/* the bus pulls up to all ones		*/

struct chip {
	int	present;		/* 0 = no module fitted		*/
	int	float_mode;
	unsigned char reg[16];		/* the sixteen 4-bit registers	*/
	int	addr;			/* the address latch		*/
	int	stopped;		/* STOP level			*/

	/* the CIO side */
	int	pbdata, pbdd, pbms, pcdata, pcdd, mccr;
	int	pc_pins;		/* levels on PC0..PC3 as inputs	*/
	int	cs;

	/* what the module saw */
	int	nread, nwrite, nreset, nstop, ncs, ntick, nsuppressed;
	int	reset_while_stopped;
	int	pc3_lost;		/* PC3 driven low = keyboard hurt */

	/* fault injection */
	int	tick_at_read;		/* carry just after this read	*/
	int	tick_every;		/* carry after every Nth read	*/
	int	drop_write;		/* reg+1 whose write is swallowed */

	int	nfault;
	const char *fault[CHIP_MAXFAULT];
};

extern struct chip Chip;

extern void chip_reset(void);
extern void chip_seed(int y, int mo, int d, int h, int mi, int s, int wd);
extern void chip_tick(void);

#endif
