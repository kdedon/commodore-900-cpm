/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/*
 * sstkt.c - SC #1 copies into segment 0x3F, which holds every process's
 * supervisor stack.  A Normal-mode program may write none of it outside
 * a trap frame handed to its own handler, so each copy below must leave
 * its target as it was.  Reads are allowed, which is how each target is
 * checked.  A copy within the TPA must still work.
 */

#include "cpm.h"

extern VOID	sc1cpy();	/* sstktsc.s: sc1cpy(src, dst, len) */

#define	SSEG	0x3F000000L
#define	NB	32

static struct {
	unsigned	off, len;
	char		*what;
} tgt[] = {
	{ 0x0000, 32, "headroom below the lowest stack" },
	{ 0xC000, 16, "another process's stack" },
	{ 0xEC00, 16, "own stack below the gate's frame" },
	{ 0xFC00, 16, "run past own stack top" },
	{ 0xFFF8, 16, "wrap to the segment start" },
};

/* The target's bytes, then 3F:0000, where a wrapped copy lands. */
static char	pat[NB], before[NB + 16], after[NB + 16];
static int	bad;

static VOID fail(s)
char *s;
{
	cputs("SSTKT: BAD ");
	cputs(s);
	cputs("\r\n");
	bad++;
}

/* n bytes at 3F:off, up to the segment end, then 16 at 3F:0000. */
static VOID peek(off, n, buf)
unsigned off, n;
char *buf;
{
	register int j;

	for (j = 0; j < NB + 16; j++)
		buf[j] = 0;
	if ((long)n > 0x10000L - off)
		n = (unsigned)(0x10000L - off);
	sc1cpy(SSEG | off, (long)buf, (long)n);
	sc1cpy(SSEG, (long)(buf + NB), 16L);
}

int main()
{
	register int i, j, n;
	static char a[4] = { 'S', 'S', 'T', 'K' }, b[4];

	for (j = 0; j < NB; j++)
		pat[j] = (char)(0xA5 ^ j);
	for (i = 0; i < sizeof tgt / sizeof tgt[0]; i++) {
		n = tgt[i].len;
		peek(tgt[i].off, n, before);
		sc1cpy((long)pat, SSEG | tgt[i].off, (long)n);
		peek(tgt[i].off, n, after);
		for (j = 0; j < NB + 16; j++)
			if (before[j] != after[j]) {
				fail(tgt[i].what);
				break;
			}
	}
	sc1cpy((long)a, (long)b, 4L);
	for (j = 0; j < 4; j++)
		if (a[j] != b[j]) {
			fail("TPA copy did not land");
			break;
		}
	cputs(bad ? "SSTKT: FAIL\r\n" : "SSTKT: PASS\r\n");
	return (0);
}
