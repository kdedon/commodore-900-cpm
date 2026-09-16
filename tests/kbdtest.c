/*
 * kbdtest.c -- src/bios/kbd900.h on the host, against COHERENT's driver.
 *
 * Neither emulator here has a keyboard (ours models CIO #1 as a register
 * file and always selects serial), so this is the only place the owned
 * keyboard decoder runs before a person types on it.  The oracle is
 * COHERENT's C900 driver itself, compiled unmodified (tests/kbdoracle.c).
 *
 *   1. kbdinit() writes COHERENT's kbinit() sequence, register for register
 *      and value for value, less the three interrupt writes the BIOS leaves
 *      out (PAIV, PACS=C0, MICR).
 *   2. Every scan code 0..127, down and up, in every one of the 64 shift
 *      states: kbd900.c emits exactly the bytes COHERENT's kbintr() queues,
 *      after the one documented filter (bytes 0x00 and 0x80-0xFF dropped),
 *      and ends in exactly COHERENT's shift state.
 *   3. kbdpoll(): an idle poll touches nothing, a ready poll reads and acks
 *      in COHERENT's order, and an arrow's second byte comes from the queue.
 *
 * Exit status is the result.  Build: cc -std=gnu89 (K&R, like the source).
 */

#include <stdio.h>
#include <string.h>

static unsigned char port[0x10000];
static int wlog[64][2];
static int wn;
static int rn;

int inb(p)
int p;
{
	rn++;
	return (port[p & 0xffff]);
}

outb(p, v)
int p, v;
{
	if (wn < 64) {
		wlog[wn][0] = p;
		wlog[wn][1] = v & 0xff;
		wn++;
	}
	port[p & 0xffff] = v;
}

#include "../src/bios/kbd900.h"

extern int oranktab();
extern int orainit();
extern int orakey();

static int fails;

#define FAIL(args)	(fails++ < 20 ? (printf args, 0) : 0)

static checkinit()
{
	static unsigned char preset[0x10000];
	int olog[64][2];
	int n, i, j;

	/* Nonzero values in the registers both drivers read-modify-write,
	 * so a wrong mask shows up. */
	memset(preset, 0, sizeof preset);
	preset[KB_PCDD] = 0xa5;
	preset[KB_PCDPP] = 0x5f;
	preset[KB_PCSIOC] = 0xfe;
	preset[KB_MCC] = 0x90;
	preset[0x01] = 0x80;
	n = orainit(preset, olog);
	memcpy(port, preset, sizeof port);
	wn = 0;
	kbdinit();
	j = 0;
	for (i = 0; i < n; i++) {
		if (olog[i][0] == 0x05 || olog[i][0] == 0x01
		 || (olog[i][0] == KB_PACS && olog[i][1] == 0xc0))
			continue;	/* the interrupt writes */
		if (j >= wn || wlog[j][0] != olog[i][0] || wlog[j][1] != olog[i][1])
			FAIL(("kbdtest: init write %d: COHERENT %04x=%02x, ours %04x=%02x\n",
			    i, olog[i][0], olog[i][1],
			    j < wn ? wlog[j][0] : 0, j < wn ? wlog[j][1] : 0));
		j++;
	}
	if (j != wn)
		FAIL(("kbdtest: init: COHERENT (less interrupts) %d writes, ours %d\n", j, wn));
	return (n);
}

static checkkeys()
{
	unsigned char ob[8], want[8];
	char q[2];
	int s, c, u, n, m, i, k, ns, cases;

	cases = 0;
	for (s = 0; s < 64; s++)
		for (c = 0; c < 128; c++)
			for (u = 0; u < 2; u++) {
				cases++;
				n = orakey(c, u, s, &ns, ob);
				for (i = m = 0; i < n; i++)
					if (ob[i] != 0 && (ob[i] & 0x80) == 0)
						want[m++] = ob[i];
				kbstate = s;
				k = kbdkey(c, u ? KB_UP : 0, q);
				if (k != m || memcmp(q, want, m) != 0)
					FAIL(("kbdtest: SC%02X %s state %02x: COHERENT %d byte(s) %02x %02x, ours %d byte(s) %02x %02x\n",
					    c, u ? "up" : "down", s, m,
					    m > 0 ? want[0] : 0, m > 1 ? want[1] : 0,
					    k, k > 0 ? q[0] & 0xff : 0, k > 1 ? q[1] & 0xff : 0));
				if (kbstate != ns)
					FAIL(("kbdtest: SC%02X %s state %02x: COHERENT state %02x, ours %02x\n",
					    c, u ? "up" : "down", s, ns, kbstate));
			}
	return (cases);
}

/* Present one key on the chip: IP set, scan code with PA7, PC2 = up. */
static key(c, u)
int c, u;
{
	port[KB_PACS] = KB_IP;
	port[KB_PADATA] = c | 0x80;
	port[KB_PCDATA] = u ? KB_UP : 0;
	wn = 0;
	rn = 0;
}

static idle()
{
	port[KB_PACS] = 0;
	wn = 0;
	rn = 0;
}

static expect(what, got, want)
char *what;
int got, want;
{
	if (got != want)
		FAIL(("kbdtest: poll %s: got %02x, want %02x\n", what, got, want));
}

static checkpoll()
{
	memset(port, 0, sizeof port);
	kbdinit();

	idle();
	expect("idle", kbdpoll(), 0);
	expect("idle reads", rn, 1);
	expect("idle writes", wn, 0);

	key(0x1e, 0);				/* a */
	expect("a", kbdpoll(), 'a');
	expect("ack writes", wn, 3);
	if (wn == 3 && (wlog[0][0] != KB_PACS || wlog[0][1] != 0x20
	 || wlog[1][0] != KB_PCDATA || wlog[1][1] != 0x70
	 || wlog[2][0] != KB_PCDATA || wlog[2][1] != 0x78))
		FAIL(("kbdtest: ack is not PACS=20 PCDATA=70 PCDATA=78\n"));
	key(0x1e, 1);
	expect("a up", kbdpoll(), 0);

	key(0x2a, 0);				/* left shift down */
	expect("shift", kbdpoll(), 0);
	key(0x03, 0);				/* 2 -> @ */
	expect("shift 2", kbdpoll(), '@');
	key(0x2a, 1);				/* left shift up */
	expect("shift up", kbdpoll(), 0);
	key(0x03, 0);
	expect("2", kbdpoll(), '2');

	key(0x1d, 0);				/* ctrl down */
	kbdpoll();
	key(0x2e, 0);				/* c -> ^C */
	expect("ctrl c", kbdpoll(), 0x03);
	key(0x02, 0);				/* 1: no control entry */
	expect("ctrl 1", kbdpoll(), 0);
	key(0x1d, 1);
	kbdpoll();

	key(0x5f, 0);				/* up arrow -> ESC A */
	expect("up 1", kbdpoll(), 033);
	idle();
	expect("up 2", kbdpoll(), 'A');
	expect("up 2 reads", rn, 0);
	expect("after up", kbdpoll(), 0);
	return (0);
}

main()
{
	int ninit, cases;

	if (oranktab() != KB_NKEY || sizeof kbtab != 4 * KB_NKEY)
		FAIL(("kbdtest: COHERENT's table has %d rows, kbd900.c %d (KB_NKEY %d)\n",
		    oranktab(), (int)(sizeof kbtab / 4), KB_NKEY));
	ninit = checkinit();
	cases = checkkeys();
	checkpoll();
	if (fails) {
		printf("kbdtest: FAIL -- %d mismatch(es)\n", fails);
		return (1);
	}
	printf("kbdtest: PASS -- init (%d COHERENT writes), %d key events identical to COHERENT, poll path\n",
	    ninit, cases);
	return (0);
}
