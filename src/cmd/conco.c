/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/*
 * conco.c - The creator that PARKS INSIDE CREATION.  src/cmd/concm.c is
 *	     the other half and its banner describes the whole
 *	     arrangement.
 *
 * This program waits for CONCM to be parked at its operator prompt
 * holding the filesystem lock, says so (which is the emulator's cue to
 * release the answer CONCM is waiting for), and then asks for a process.
 * pcrgen() picks a descriptor, finds the lock held and parks there --
 * which is the window the whole target is about.
 *
 * THE WAIT IS ONE-SIDED, so it is not a timing constant in the usual
 * sense: CONCM's prompt is unanswered until this program prints, so it
 * stays parked however long this waits.  The delay only has to be LONG
 * ENOUGH for CONCM to get from its create call to the prompt: a file
 * create, a record write and a close, which is a handful of disk
 * operations.
 *
 * The mark print and the create are adjacent with no BDOS call between
 * them, because CONCM becomes runnable the moment the answer lands and
 * this program must be inside pcrgen() by then.
 */

#include "cpm.h"

#define	BDOS_CREATEPROC	144		/* XDOS create process		*/
#define	BDOS_DELAY	141		/* XDOS delay, in ticks		*/

#define	SETTLE		150		/* ticks; about a second and a half */

struct pcreq {
	struct fcb	pq_fcb;
	char		pq_tlen;
	char		pq_tail[128];
};

static struct pcreq	req;

static char *why[] = {
	"",
	"no such program",
	"not enough memory in the new page",
	"read error loading it",
	"program load error",
	"no free process descriptor",
	"no free 64 KB page -- this needs a 1 MB machine",
	"a second split-I/D program would need a second data bank",
	"no such console"
};

static char lbuf[80];

static VOID numline(tag, n)
char	*tag;
int	n;
{
	register char	*p;
	register int	d, seen;

	for (p = lbuf; *tag != 0; )
		*p++ = *tag++;
	seen = 0;
	for (d = 10000; d > 0; d /= 10)
		if (n / d != 0 || seen || d == 1) {
			*p++ = '0' + (n / d) % 10;
			seen = 1;
		}
	*p++ = '\r';
	*p++ = '\n';
	*p = 0;
	cputs(lbuf);
}


int main(argc, argv)
int argc;
char *argv[];
{
	register int	i, k;
	register char	*p;

	cputs("CONCO: O alive\r\n");

	/*  MHELLO prints the command tail back, so the tail is the
	    evidence of WHOSE request was loaded.  */
	mkfcb("MHELLO.Z8K", &req.pq_fcb);
	for (i = 0; i < 128; i++)
		req.pq_tail[i] = 0;
	i = 0;
	for (p = " QQ"; *p != 0; p++)
		req.pq_tail[i++] = *p;
	req.pq_tlen = (char)i;

	__bdos(BDOS_DELAY, (long)SETTLE);

	cputs("CONCO: asking\r\n");
	k = __bdos(BDOS_CREATEPROC, (long) &req) & 0xff;

	numline("CONCO: my request answered ", k);
	if (k != 0) {
		cputs("CONCO: refused: ");
		cputs(k > 0 && k < 9 ? why[k] : "unknown");
		cputs("\r\n");
	}
	cputs("CONCO: O done\r\n");
	return (0);
}
