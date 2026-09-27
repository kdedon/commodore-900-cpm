/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/*
 * pfill.c - PFILL.Z8K: start background copies of itself until the system
 * refuses, and print how many processes were live at the most.
 *
 *   PFILL      the parent: creates `PFILL n' for n = 1, 2, ... and prints
 *              function 145's count after each, then the refusal code.
 *              Setting flag n afterwards lets child n end.
 *   PFILL n    a child: waits on flag n, which keeps it live.
 */

#include "cpm.h"

#define	BDOS_FLAGWAIT	132
#define	BDOS_FLAGSET	133
#define	BDOS_CREATEPROC	144
#define	BDOS_PROCCNT	145
#define	NFLAG		8

struct pcreq {
	struct fcb	pq_fcb;
	char		pq_tlen;
	char		pq_tail[128];
};

static struct pcreq	req;

static VOID putd(v)
int v;
{
	if (v >= 10)
		putd(v / 10);
	conout('0' + v % 10);
}

int main(argc, argv)
int argc;
char *argv[];
{
	register int	n, k, i;

	if (argc > 1) {
		__bdos(BDOS_FLAGWAIT, (long) (argv[1][0] - '0'));
		return (0);
	}
	k = 0;
	for (n = 1; n < NFLAG; n++) {
		mkfcb("PFILL.Z8K", &req.pq_fcb);
		for (i = 0; i < 128; i++)
			req.pq_tail[i] = 0;
		req.pq_tail[0] = ' ';
		req.pq_tail[1] = '0' + n;
		req.pq_tlen = 2;
		if ((k = __bdos(BDOS_CREATEPROC, (long) &req) & 0xff) != 0)
			break;
		cputs("PFILL: live ");
		putd(__bdos(BDOS_PROCCNT, 0L) & 0xff);
		cputs("\r\n");
	}
	cputs("PFILL: refused ");
	putd(k);
	cputs("\r\n");
	while (--n > 0)
		__bdos(BDOS_FLAGSET, (long) n);
	return (0);
}
