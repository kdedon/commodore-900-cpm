/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/*
 * derr.c -- F1(b): a refused transfer must not be reported as a success.
 *
 * src/bdos/dskutil.c rdwrt() used to leave its retry loop and `return(0)':
 *
 *	while ( do_phio(&rwpkt) )
 *	    if ( error( parm ? 1 : 0 ) ) break;
 *	return(0);
 *
 * error() returns non-zero in exactly two cases and in NEITHER did the
 * transfer happen -- function 45's return-error mode, and the default mode's
 * operator answering `C' (continue with bad data).  The `C' case is the
 * sharper of the two, because error mode 0 does not set errcode either: the
 * BDOS reported a completed write to a record that never reached the medium,
 * with nothing anywhere to say otherwise.
 *
 * The device error is real, not simulated.  This program runs on an image
 * whose drive-B: partition is TRUNCATED: B:'s directory and its first data
 * block are on the medium and the block after that is not, so the hard-disk
 * controller answers 92h (drive not ready) for it -- and one `C' from the
 * scripted operator is all the session needs.
 */

#include "cpm.h"

#define	TARGET	"B:F1E.TXT"

static struct fcb	f;
static char		rec[SECLEN];

int main(argc, argv)
int argc;
char *argv[];
{
	register int	i, k;

	cputs("DERR: start\r\n");

	/*  Error mode stays at its default: the operator is asked, and this
	    is the path where a `C' answer used to become a silent success. */
	mkfcb(TARGET, &f);
	__bdos(BDOS_DELETE, (long) &f);
	mkfcb(TARGET, &f);
	if (__bdos(BDOS_MAKE, (long) &f) == 255) {
		cputs("DERR: FAIL -- cannot create the target on B:\r\n");
		return (1);
	}

	for (i = 0; i < SECLEN; i++)
		rec[i] = 'e';

	/*  ONE write, and RANDOM record 1 rather than sequential record 0.
	    That distinction is the whole reason this session needs exactly
	    one operator answer.  Record 0 of a freshly allocated block is
	    the first record of a physical sector, so the BIOS zeroes a
	    buffer for it instead of reading the sector (src/bios/bios900.c
	    dskwrite, mode 2): the medium is never touched, nothing fails
	    there, and the dirty buffer left behind fails later at eviction,
	    inside some unrelated call.  Record 1 is a read-modify-write of a
	    sector that is not on the medium, so the refusal happens in THIS
	    call and nothing is left over to fail in the next one.  */
	f.ran0 = 0;
	f.ran1 = 0;
	f.ran2 = 1;
	setdma(rec);
	k = __bdos(BDOS_WRITERAN, (long) &f);
	__bdos(BDOS_CLOSE, (long) &f);

	if (k == 0)
		cputs("DERR: refused=unreported\r\n");
	else {
		cputs("DERR: refused=reported rc=");
		putdec((unsigned) k);
		cputs("\r\n");
	}
	cputs("DERR: done\r\n");
	return (0);
}
