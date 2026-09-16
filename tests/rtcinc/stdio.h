/*
 * host/rtcinc/stdio.h -- the DRI type layer, for host builds of the RTC
 * driver (host/rtctest.c).
 *
 * src/rtc900.c includes "stdio.h" and gets sys/stdio.h on the target.
 * Compiling it on the host with -Ihost/rtcinc puts this file in its place:
 * the real host <stdio.h> plus the same type macros sys/stdio.h defines --
 * and, critically, the SAME signedness and the SAME widths the target
 * compiler gives them:
 *
 *	UBYTE	plain char	SIGNED, exactly as sys/stdio.h's ALCYON arm
 *				makes it.  An unmasked byte therefore
 *				sign-extends here too, which is how the
 *				1978-04-19 set-path bug was first caught.
 *	UWORD	unsigned short	16 bits, as `unsigned int' is on the Z8001.
 *	WORD	short		16 bits.
 *	XADDR	long		a far pointer; on the host it carries a real
 *				host address, which mem_cpy() casts back.
 *
 * Nothing here is used by the target build.
 */
#ifndef RTCSHIM_STDIO
#define RTCSHIM_STDIO

#include_next <stdio.h>

#define UBYTE	char			/* SIGNED, per sys/stdio.h ALCYON */
#define UWORD	unsigned short
#define WORD	short
#define BYTE	char
#define XADDR	long
#define UBWORD(a) ((UWORD)a & 0xff)

#ifndef VOID
#define VOID	int
#endif

#endif
