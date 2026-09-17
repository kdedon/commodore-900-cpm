/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/*
 * biosdef.h -- CP/M-8000 BIOS entry and request wrappers (C900).
 *
 * One C entry serves every BIOS function:
 *
 *	long bios(d0, d1, d2)  int d0;  long d1, d2;
 *
 * Function codes and status values follow the standard CP/M-8000 BIOS
 * surface (c900oses/cpm8000/cpm8k13/STDBIO.H).  All arguments are widened to long by
 * these macros -- with a K&R compiler the caller alone fixes argument
 * widths, so never call bios() with a bare int or 0 in d1/d2.
 */
#ifndef BIOSDEF_H
#define BIOSDEF_H

extern long bios();

#define binit()		bios(0, 0L, 0L)		/* cold init		*/
#define bwboot()	bios(1, 0L, 0L)		/* warm boot		*/
#define bconstat()	((int)bios(2, 0L, 0L))	/* console status	*/
#define bconin()	((int)bios(3, 0L, 0L))	/* console input	*/
#define bconout(c)	bios(4, (long)(c), 0L)	/* console output	*/
#define blstout(c)	bios(5, (long)(c), 0L)	/* list output		*/
#define bpun(c)		bios(6, (long)(c), 0L)	/* punch output		*/
#define brdr()		((int)bios(7, 0L, 0L))	/* reader input		*/
#define bhome()		bios(8, 0L, 0L)		/* home drive		*/
#define bseldsk(d,l)	bios(9, (long)(d), (long)(l))	/* -> DPH or 0	*/
#define bsettrk(t)	bios(10, (long)(t), 0L)	/* set track		*/
#define bsetsec(s)	bios(11, (long)(s), 0L)	/* set sector		*/
#define bsetdma(a)	bios(12, (long)(a), 0L)	/* set DMA (XADDR)	*/
#define bread()		((int)bios(13, 0L, 0L))	/* read 128-byte record	*/
#define bwrite(m)	((int)bios(14, (long)(m), 0L))	/* write record	*/
#define blistst()	((int)bios(15, 0L, 0L))	/* list status		*/
#define bsectrn(s,x)	bios(16, (long)(s), (long)(x))	/* sector xlate	*/
#define bgetseg()	bios(18, 0L, 0L)	/* -> memory region tbl	*/
#define bgetiob()	((int)bios(19, 0L, 0L))	/* get IOBYTE		*/
#define bsetiob(b)	bios(20, (long)(b), 0L)	/* set IOBYTE		*/
#define bflush()	((int)bios(21, 0L, 0L))	/* flush disk buffers	*/
#define bsetvec(v,a)	bios(22, (long)(v), (long)(a))	/* set exc vec	*/

/*
 * Function 23 -- TIME.  The C900 addition (CP/M 3's BIOS TIME entry; the
 * first free code after fn 22).  `t' is the XADDR of a 5-byte TOD block
 * {date-word big-endian, hour BCD, minute BCD, second BCD} and `s' is 0
 * to read the clock into it, nonzero to set the clock from it.  Returns 0
 * on success and 0xff when no clock answered.
 */
#define btime(t,s)	((int)bios(23, (long)(t), (long)(s)))

#endif /* BIOSDEF_H */
