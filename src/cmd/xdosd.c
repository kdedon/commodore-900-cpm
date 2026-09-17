/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/*
 * xdosd.c - Coordinate a child process through queues, a delay, and an
 * event flag.
 */

#include "cpm.h"

#define	X_FLGWT		132
#define	X_MAKEQ		134
#define	X_OPENQ		135
#define	X_READQ		137
#define	X_CWRITEQ	140
#define	X_DELAY		141
#define	X_CREATE	144
#define	X_PROCCNT	145

#define	XFAIL	0x00ff

#define	DTICKS	10		/* tick periods to sleep for.  Ten is
				   100 ms of a 100 Hz tick and, on the
				   emulator's instruction-scaled CT3, a
				   few dozen trips round the robin --
				   comfortably more than the threshold
				   the verify target asserts.	*/
#define	XFLAG	3		/* the flag E sets and D waits on	*/

struct pcreq {
	struct fcb	pq_fcb;
	char		pq_tlen;
	char		pq_tail[128];
};

struct xqmake {
	char	qm_name[8];
	short	qm_msglen;
	short	qm_ndep;
};

struct xqopen {
	char	qo_name[8];
	short	qo_id;
};

struct xqmsg {
	short	qx_id;
	char	qx_msg[16];
};

static struct pcreq	req;
static struct xqmake	mk;
static struct xqopen	op;
static struct xqmsg	msg;

static char *why[] = {
	"",
	"no such program",
	"not enough memory in the new page",
	"read error loading it",
	"program load error",
	"no free process descriptor",
	"no free 64 KB page -- this needs a 1 MB machine",
	"a second split-I/D program would need a second data bank"
};

static VOID setname(d, s)
register char	*d, *s;
{
	register int	i;

	for (i = 0; i < 8; i++)
		d[i] = *s ? *s++ : ' ';
}

static int makeq(name, len, depth)
char	*name;
int	len, depth;
{
	setname(mk.qm_name, name);
	mk.qm_msglen = len;
	mk.qm_ndep = depth;
	return (__bdos(X_MAKEQ, (long)&mk));
}

static int openq(name)
char	*name;
{
	setname(op.qo_name, name);
	op.qo_id = -1;
	if (__bdos(X_OPENQ, (long)&op) != 0)
		return (-1);
	return (op.qo_id);
}

/*
 * ONE cputs() FOR A WHOLE LINE, and it is not a tidy-up.
 *
 * These two lines used to be `cputs(tag); putdec(n); cputs("\r\n")' --
 * three BDOS calls -- and verify-xdos2 greps for the finished text
 * `XDOSD: live=2'.  A process is only ever switched away from AT a BDOS
 * gate or by the tick in Normal mode, so the gaps BETWEEN those three
 * calls are switch points: the other process's next line can land inside
 * `XDOSD: live=' and `2', and once C5 moved the dispatcher's cost by a
 * few instructions it did (`XDOSD: live=XDOSE: alive' / `2').
 *
 * That splice is correct behaviour and the target is right to grep for
 * the whole string.  So the program stops offering a gap: the line is
 * composed here and printed by one call, which no switch can cut because
 * nothing switches a process that is inside the BDOS.
 */

static char	clbuf[40];

static VOID countline(tag, n)
char	*tag;
int	n;
{
	register char	*p;
	register int	d, seen;

	for (p = clbuf; *tag != 0; )
		*p++ = *tag++;
	seen = 0;
	for (d = 10000; d > 0; d /= 10)
		if (n / d != 0 || seen || d == 1) {
			*p++ = '0' + ((n / d) % 10);
			seen = 1;
		}
	*p++ = '\r';
	*p++ = '\n';
	*p = 0;
	cputs(clbuf);
}


int main(argc, argv)
int	argc;
char	*argv[];
{
	register int	i, k;
	int		qmsg, qstop;
	int		live0;

	cputs("XDOSD: start\r\n");

	/*  Both queues before the child, because the child opens them
	    the moment it runs and it runs as soon as 144 returns.  */
	if (makeq("XDOSQ", 8, 4) != 0 || makeq("XDOSS", 2, 2) != 0) {
		cputs("XDOSD: FAIL could not make the queues\r\n");
		return (1);
	}
	if ((qmsg = openq("XDOSQ")) < 0 || (qstop = openq("XDOSS")) < 0) {
		cputs("XDOSD: FAIL could not open the queues\r\n");
		return (1);
	}

	mkfcb("XDOSE.Z8K", &req.pq_fcb);
	req.pq_tlen = 0;
	for (i = 0; i < 128; i++)
		req.pq_tail[i] = 0;
	k = __bdos(X_CREATE, (long)&req);
	if (k != 0) {
		cputs("XDOSD: no second process: ");
		cputs(k > 0 && k < 8 ? why[k] : "refused");
		cputs("\r\n");
		return (1);
	}
	live0 = (int)(__bdos(X_PROCCNT, 0L) & 0xff);
	countline("XDOSD: live=", live0);

	/* ---- 141: sleep, and let E have the machine ---- */

	cputs("XDOSD: delay\r\n");
	k = __bdos(X_DELAY, (long)DTICKS);
	cputs("XDOSD: delayed\r\n");
	if (k != 0)
		cputs("XDOSD: FAIL 141 did not return 0\r\n");

	/*  Tell E to stop its counting loop.  Not blocking: if E has
	    already stopped there is nobody to drain this, and D must not
	    wait for that.  */
	msg.qx_id = qstop;
	setname(msg.qx_msg, "S");
	__bdos(X_CWRITEQ, (long)&msg);

	/* ---- 132: block on a flag only E can set ---- */

	k = __bdos(X_FLGWT, (long)XFLAG);
	cputs("XDOSD: flag\r\n");
	if (k != 0)
		cputs("XDOSD: FAIL 132 did not return 0\r\n");

	/* ---- 137: block on a queue only E can fill ---- */

	msg.qx_id = qmsg;
	for (i = 0; i < 8; i++)
		msg.qx_msg[i] = 0;
	k = __bdos(X_READQ, (long)&msg);
	if (k != 0) {
		cputs("XDOSD: FAIL 137 did not return 0\r\n");
		return (1);
	}
	msg.qx_msg[8] = 0;
	cputs("XDOSD: msg ");
	cputs(msg.qx_msg);
	cputs("\r\n");

	/*  E ended with function 143, MP/M's Terminate.  If that did
	    what function 0 does, its page and its descriptor are back in
	    the pool and this is the only process left.  E is still
	    running when the message arrives -- it prints one more line
	    and then terminates -- so wait for it, one tick at a time
	    and never for ever.  A bound rather than a spin because a
	    test that cannot finish cannot report.	*/
	/*  "this is the only process left" was written on a machine where
	    it was.  Since C10 the cold boot starts a session on every other
	    console (src/bdos/proc.c pcoldses), so what E's Terminate has to
	    produce is ONE FEWER than the count printed above, not the
	    literal 1.  The bound is unchanged.		*/
	for (i = 0; i < 200
	     && (int)(__bdos(X_PROCCNT, 0L) & 0xff) >= live0; i++)
		__bdos(X_DELAY, 1L);
	countline("XDOSD: after=", (int)__bdos(X_PROCCNT, 0L));
	cputs("XDOSD: done\r\n");
	return (0);
}
