/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/* CP/M-86 INT 0xe0 bridge: CL selects the function, DX the parameter;
 * return results in AX and BX. Translate guest pointers and random-record
 * byte order for native BDOS calls. */

#include "i86.h"

/* ------------------------------------------------------------------ */
/* what the seam reports to its caller				       */

int	i86bdosfn;		/* function of the last call, for a message */
i32	i86nbdos;		/* K2's third instrument: calls serviced	*/

i16	i86dmaoff;		/* the guest's DMA offset (fn 26)	*/
i16	i86dmaseg;		/* ... and base paragraph (fn 51)	*/

/*
 * The multi-sector count the native BDOS is currently holding.
 *
 * It is mirrored here because it is HALF of every DMA bound: our BDOS's
 * multio() (src/bdos/bdosrw.c:327) loops the count and adds SECLEN to
 * the DMA address between records, so the window a transfer touches is
 * count * 128 bytes and not the 128 a single record needs.  Function 49
 * is refused here (pmap[] below), so function 44 is the only door a
 * CP/M-86 guest has to it, and bdosmisc.c:158 resets it to one per
 * program -- which is what i86bdosinit() does.
 */
static int	i86mult = 1;

/* Collect consecutive function-2 output into native function 111 blocks.
 * Flush before other calls and when the interpreter stops. */
#define I86OBUF	128		/* pending function 2 characters	*/

static char	obuf[I86OBUF];
static int	onbuf;
static struct {			/* the native CCB for the batch		*/
	char	*a;
	i16	n;
} octl;

/*
 * i86oflush -- send whatever function 2 has collected, as one function
 * 111.  A no-op when there is nothing pending, which is why it can be
 * called on every path without a test at the call site.
 *
 * `octl.a' is a host pointer, and on the target that IS the XADDR our
 * BDOS reads out of a character control block (src/cmd/cpm.h:1-9); the
 * buffer is ours, not the guest's, so there is no segment check to make
 * -- which is also why this is the one thing here that does not go
 * through i86addr().
 *
 * PUBLIC because the seam is not the only way a run can end: the
 * caller's loop also stops on a bad instruction, a HLT or its own step
 * limit, and none of those comes through here.  i86.c and
 * tests/i86test.c call it once when their loop ends, so a guest that
 * crashes mid-line still gets the line it had written.
 */
int i86oflush()
{
	register int n;

	if ((n = onbuf) == 0)
		return (0);
	onbuf = 0;
	octl.a = obuf;
	octl.n = (i16)n;
	i86sys(111, (i16)0, (char *)&octl);
	return (n);
}

/* Report CP/M 2.2 compatibility to avoid CP/M 3-specific FCB attributes
 * in CP/M-86 guests. */
i16	i86ver = 0x2022;

/* ------------------------------------------------------------------ */
/* parameter classes						       */

#define P_NONE	0		/* no parameter				*/
#define P_BYTE	1		/* DL					*/
#define P_WORD	2		/* DX, a value				*/
#define P_FCB	3		/* DS:DX, a 36-byte FCB			*/
#define P_STR	4		/* DS:DX, a `$'-terminated string	*/
#define P_BUF	5		/* DS:DX, a console read buffer		*/
#define P_NO	6		/* not mapped in stage one		*/

#define I86FCB	36		/* sizeof(struct fcb) -- src/cmd/cpm.h	*/
#define I86REN	52		/* fn 23: old FCB at 0, new at 16	*/
#define I86DMA	128		/* one CP/M record			*/

/* Parameter classes for supported calls. Refuse native-pointer APIs and
 * environment operations without a guest representation. */
static i8 pmap[53] = {
	P_NONE,		/*  0 system reset -- handled before this table	*/
	P_NONE,		/*  1 console input				*/
	P_BYTE,		/*  2 console output -- collected, never reached	*/
	P_NONE,		/*  3 reader input				*/
	P_BYTE,		/*  4 punch output				*/
	P_BYTE,		/*  5 list output				*/
	P_BYTE,		/*  6 direct console i/o				*/
	P_NONE,		/*  7 get i/o byte				*/
	P_BYTE,		/*  8 set i/o byte				*/
	P_STR,		/*  9 print string				*/
	P_BUF,		/* 10 read console buffer			*/
	P_NONE,		/* 11 console status				*/
	P_NONE,		/* 12 version -- handled before this table	*/
	P_NONE,		/* 13 reset disk system				*/
	P_BYTE,		/* 14 select disk				*/
	P_FCB,		/* 15 open file					*/
	P_FCB,		/* 16 close file					*/
	P_FCB,		/* 17 search first				*/
	P_FCB,		/* 18 search next				*/
	P_FCB,		/* 19 delete file				*/
	P_FCB,		/* 20 read sequential				*/
	P_FCB,		/* 21 write sequential				*/
	P_FCB,		/* 22 make file					*/
	P_FCB,		/* 23 rename file -- I86REN bytes, see below	*/
	P_NONE,		/* 24 login vector				*/
	P_NONE,		/* 25 current disk				*/
	P_WORD,		/* 26 set DMA offset -- handled before this table */
	P_NO,		/* 27 get addr(alloc)				*/
	P_NONE,		/* 28 write protect disk				*/
	P_NONE,		/* 29 get read-only vector			*/
	P_FCB,		/* 30 set file attributes				*/
	P_NO,		/* 31 get addr(disk parms)			*/
	P_BYTE,		/* 32 get/set user code				*/
	P_FCB,		/* 33 read random				*/
	P_FCB,		/* 34 write random				*/
	P_FCB,		/* 35 compute file size				*/
	P_FCB,		/* 36 set random record				*/
	P_WORD,		/* 37 reset drive				*/
	P_NO,		/* 38 MP/M access drive				*/
	P_NO,		/* 39 MP/M free drive				*/
	P_FCB,		/* 40 write random with zero fill		*/
	P_NO,		/* 41 does not exist				*/
	P_FCB,		/* 42 lock record (a no-op here)			*/
	P_FCB,		/* 43 unlock record (ditto)			*/
	P_BYTE,		/* 44 set multi-sector count			*/
	P_BYTE,		/* 45 set BDOS error mode -- PIP's fourth call	*/
	P_BYTE,		/* 46 get disk free space -> the DMA buffer	*/
	P_NO,		/* 47 chain to program				*/
	P_NONE,		/* 48 flush buffers				*/
	P_NO,		/* 49 get/set SCB: ours, not CP/M-86's		*/
	P_NO,		/* 50 direct BIOS call				*/
	P_WORD,		/* 51 set DMA base -- handled before this table	*/
	P_NO		/* 52 get DMA base				*/
};

/* Reason codes for the refusals, so a caller can print one sentence. */
#define BR_NONE	0
#define BR_FN	1		/* a function stage one does not map	*/
#define BR_ADDR	2		/* the parameter left its segment	*/
#define BR_SEG	3		/* the DMA base is a paragraph we lack	*/
#define BR_VEC	4		/* an interrupt that is not 0E0h	*/
#define BR_DIV	5		/* vector 0: the guest divided by zero	*/

static int breason;

char *i86berr()
{
	switch (breason) {
	case BR_NONE:	return ("ok");
	case BR_FN:	return ("BDOS function not mapped in stage one");
	case BR_ADDR:	return ("parameter runs past the end of its segment");
	case BR_SEG:	return ("DMA base is a paragraph we did not assign");
	case BR_VEC:	return ("interrupt vector other than 0E0h");
	case BR_DIV:	return ("divide error (INT 0)");
	}
	return ("unknown");
}

/* ------------------------------------------------------------------ */

/*
 * How many bytes are still addressable at slot:off.  A slot the loader
 * bound has the full 64 KB from its base; one E1s's slow path had to
 * bias has less, and the difference is exactly the bias (i86exec.c).
 */
static i32 room(m, s, off)
struct i86 *m;
int s;
i16 off;
{
	return (0x10000L - ((i32)(m->so[s & 3] & 0xffff) + (i32)(off & 0xffff)));
}

/*
 * Re-issue the native set-DMA with whatever the guest's two halves now
 * say.  Called from load time and from functions 26 and 51, so there is
 * one place where a DMA address is formed and one place where it is
 * checked.
 */
static int setdma(m)
struct i86 *m;
{
	register char *p;
	register int i;

	/* The base is a paragraph, and it is subject to exactly the check
	 * a segment-register write is subject to: we can only form an
	 * address inside a segment we hold.  A guest DMA base we never
	 * handed out is the same finding as K3's, arriving through a
	 * different door, so it is reported as one. */
	p = (char *)0;
	for (i = 0; i < i86nseg; i++) {
		if (i86dmaseg == i86spar[i]) {
			p = i86sbase[i];
			break;
		}
	}
	if (p == (char *)0) {
		i86segbad = i86dmaseg;
		breason = BR_SEG;
		return (0);
	}
	if ((i32)(i86dmaoff & 0xffff) + (i32)I86DMA > 0x10000L) {
		breason = BR_ADDR;
		return (0);
	}
	i86sys(26, (i16)0, p + (i86dmaoff & 0xffff));
	return (1);
}

/*
 * dmafits -- does the DMA window hold the whole of the transfer the
 * native BDOS is about to make?
 *
 * setdma() above checks ONE record, because that is what a DMA address
 * has to be good for to be a DMA address at all, and a guest is
 * entitled to place a high one while the count is large and then lower
 * the count before any I/O.  This is the check that goes at the five
 * functions multio() shells: they are the only calls whose length is
 * the count rather than a record.
 */
static int dmafits()
{
	return ((i32)(i86dmaoff & 0xffff) + (i32)i86mult * (i32)I86DMA
		<= 0x10000L);
}

/* The five functions src/bdos/bdosrw.c multio() shells. */
static int ismulti(fn)
int fn;
{
	return (fn == 20 || fn == 21 || fn == 33 || fn == 34 || fn == 40);
}

/*
 * i86bdosinit -- the state CP/M-86 gives a program before its first
 * instruction: the DMA buffer is the base page's own 128-byte tail
 * buffer, DS:0080.  Our BDOS has to be told, because the guest will
 * not tell it until it wants a different one, and PIP reads its command
 * tail through that buffer.
 */
int i86bdosinit(m)
struct i86 *m;
{
	i86dmaseg = m->sr[S_DS];
	i86dmaoff = 0x80;
	i86mult = 1;		/* src/bdos/bdosmisc.c:158		*/
	i86bdosfn = -1;
	breason = BR_NONE;
	onbuf = 0;
	i86nbdos = 0;
	return (setdma(m));
}

/*
 * i86bdos -- service the interrupt i86step() just refused to service.
 *
 * Returns B_RUN to resume the guest, B_EXIT when it has terminated, or
 * one of the refusals, with i86berr() naming it and i86bdosfn holding
 * the function that asked.  IP is already past the INT, so a resumed
 * guest continues at the instruction after it; a refusal leaves it
 * there too, because there is no sense in re-executing an INT whose
 * function we will refuse again.
 */
/* Guest FCB random records are little-endian; native BDOS uses big-endian
 * bytes at 33..35. Swap around relevant calls, excluding rename because
 * those bytes belong to its second name. */
static ranswap(p, fn)
char *p;
int fn;
{
	register int t;

	switch (fn) {
	case 33:	/* read random			*/
	case 34:	/* write random			*/
	case 35:	/* compute file size		*/
	case 36:	/* set random record		*/
	case 40:	/* write random with zero fill	*/
		t = p[33];
		p[33] = p[35];
		p[35] = (char)t;
	}
	return (0);
}

int i86bdos(m)
struct i86 *m;
{
	register int fn, cls;
	register char *p;
	i16 dx;
	int r;
	i32 n;

	breason = BR_NONE;
	if (i86intno != 0xe0) {
		/* Vector 0 is the divide error i86exec.c raises, and it
		 * is a fault in the guest rather than a hole in us; the
		 * distinction is worth keeping in the message. */
		breason = i86intno == 0 ? BR_DIV : BR_VEC;
		i86oflush();	/* a fault is a refusal somebody reads */
		i86bdosfn = -1;
		return (i86intno == 0 ? B_TRAP : B_VEC);
	}
	fn = (int)(m->r[R_CX] & 0xff);
	dx = m->r[R_DX];
	i86bdosfn = fn;
	i86nbdos++;

	/* ---- console output, collected rather than passed on. */

	if (fn == 2) {
		if (onbuf >= I86OBUF)
			i86oflush();
		obuf[onbuf++] = (char)(dx & 0xff);
		/* Our function 2 falls out of the switch to the default
		 * return value (src/bdos/bdosmain.c:288), so a deferred
		 * one answers what an immediate one would have: zero in
		 * both of CP/M-86's result places. */
		m->r[R_AX] = 0;
		m->r[R_BX] = 0;
		return (B_RUN);
	}
	i86oflush();		/* everything below is observable	*/

	/* ---- the three functions that never reach the native BDOS. */

	if (fn == 0) {
		/* System reset.  Our function 0 is warmboot() and does
		 * not return (src/bdos/bdosmain.c:258); the shim is an
		 * ordinary program and must return to ITS caller, so
		 * this is the one function the seam answers itself. */
		return (B_EXIT);
	}
	if (fn == 12) {
		m->r[R_AX] = i86ver;
		m->r[R_BX] = i86ver;
		return (B_RUN);
	}
	if (fn == 26 || fn == 51) {
		/* A REFUSED set-DMA must leave both halves holding what
		 * the native BDOS was actually told.  They used to be
		 * committed first and refused afterwards, so a guest
		 * whose DMA was rejected went on with a DMA nothing
		 * agreed about -- which the multi-sector bound below
		 * then reads. */
		i16 savoff, savseg;

		savoff = i86dmaoff;
		savseg = i86dmaseg;
		if (fn == 26)
			i86dmaoff = dx;
		else
			i86dmaseg = dx;
		if (!setdma(m)) {
			i86dmaoff = savoff;
			i86dmaseg = savseg;
			return (breason == BR_SEG ? B_SEG : B_ADDR);
		}
		m->r[R_AX] = 0;
		m->r[R_BX] = 0;
		return (B_RUN);
	}

	if (fn > 52) {
		/* 53-58 memory allocation and 59 program load.  Each is
		 * a real CP/M-86 function and none is in stage one
		 * (CPM86-STAGE-ONE.md §2.3): allocation is quantised to
		 * 64 KB here and a guest loading a second guest is a
		 * design question, not a mapping.  Refusing by name
		 * beats answering. */
		breason = BR_FN;
		return (B_FN);
	}
	cls = pmap[fn];
	if (cls == P_NO) {
		breason = BR_FN;
		return (B_FN);
	}

	/* The DMA window has to hold the WHOLE transfer, not its first
	 * record: multio() writes count * 128 bytes starting at the DMA
	 * address, so a count of two at 0xff80 would put its second
	 * record outside the guest's segment altogether.  Refused here,
	 * before the native BDOS is told, because there is no partial
	 * transfer to undo afterwards. */
	if (ismulti(fn) && !dmafits()) {
		breason = BR_ADDR;
		return (B_ADDR);
	}

	switch (cls) {
	case P_NONE:
		r = i86sys(fn, (i16)0, (char *)0);
		break;
	case P_BYTE:
		r = i86sys(fn, (i16)(dx & 0xff), (char *)0);
		/* Function 44 is the only door a CP/M-86 guest has to the
		 * count; the native BDOS answers 0xff for a count it did
		 * not take (src/bdos/bdosmain.c:603) and leaves its own
		 * alone, so this follows it. */
		if (fn == 44 && r == 0)
			i86mult = (int)(dx & 0xff);
		break;
	case P_WORD:
		r = i86sys(fn, dx, (char *)0);
		break;
	case P_FCB:
		n = fn == 23 ? (i32)I86REN : (i32)I86FCB;
		p = i86addr(m, S_DS, dx, n);
		if (p == (char *)0) {
			breason = BR_ADDR;
			return (B_ADDR);
		}
		ranswap(p, fn);
		r = i86sys(fn, (i16)0, p);
		ranswap(p, fn);
		break;
	case P_STR:
		/* Function 9's string ends at a `$' the guest put there.
		 * The native BDOS will scan for it; if there is none
		 * before the end of the guest's segment, that scan runs
		 * out of the segment, so the scan happens HERE first and
		 * the call is refused if it does not terminate. */
		p = i86addr(m, S_DS, dx, 1L);
		if (p == (char *)0) {
			breason = BR_ADDR;
			return (B_ADDR);
		}
		n = room(m, S_DS, dx);
		while (n > 0 && *p != '$') {
			p++;
			n--;
		}
		if (n <= 0) {
			breason = BR_ADDR;
			return (B_ADDR);
		}
		r = i86sys(fn, (i16)0, i86addr(m, S_DS, dx, 1L));
		break;
	case P_BUF:
		/* Byte 0 of a console buffer is the maximum the caller
		 * will accept, so the buffer is that plus the two count
		 * bytes -- and the check has to use the guest's own
		 * number, not a guess. */
		p = i86addr(m, S_DS, dx, 1L);
		if (p == (char *)0) {
			breason = BR_ADDR;
			return (B_ADDR);
		}
		n = (i32)(*p & 0xff) + 2;
		p = i86addr(m, S_DS, dx, n);
		if (p == (char *)0) {
			breason = BR_ADDR;
			return (B_ADDR);
		}
		r = i86sys(fn, (i16)0, p);
		break;
	default:
		breason = BR_FN;
		return (B_FN);
	}

	/*
	 * The result, in the three places CP/M-86 leaves it: AL for a
	 * byte, AX for a word, BX equal to AX.  Our BDOS already returns
	 * CP/M 3's word form -- the high byte carries the physical error
	 * code when function 45 put the program in return mode
	 * (src/bdos/bdosmain.c:613) -- and CP/M-86 uses AH for exactly
	 * that, so the word passes straight through.
	 */
	m->r[R_AX] = (i16)r;
	m->r[R_BX] = (i16)r;
	return (B_RUN);
}
