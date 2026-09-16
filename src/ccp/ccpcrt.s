/ ****** ccpcrt.s -- runtime startup for the transient CCP ***********
/
/  This is ccpif.s after the CCP left the system image, crossed with
/  user/crt0.s.  What each half contributed:
/
/  from user/crt0.s -- the transient entry contract.  pgmld enters a
/	segmented (0xEE01) program at its first text byte, so this
/	object must be FIRST on the link line, and text offset 2 must
/	hold a warm-boot stub because ccprun's initial "return address"
/	is pgldaddr+2.  BSS is not cleared by the loader, so the
/	startup clears it from the base page's lbss/bsslen.
/
/  from sys/ccpif.s -- the loop.  The CCP's main_ processes ONE command
/	line and returns; the loop is outside it.  What is gone is
/	everything ccpif.s held that a reload would have destroyed: the
/	sysinit latch (now in sys/ccprun.c, which is resident), the
/	bdosinit_ call (same), and the autost_/submit_/morecmds_/
/	usercmd_ storage, which is now in the CCP state (sys/ccpsv.h).
/	The saved stack pointer is gone too -- main_ is called from a
/	loop that never grows the frame, so there is nothing to reset.
/
/  The BSS clear below DOES clear the CCP's copy of that state, and that
/  is correct: the state itself is RESIDENT, one per process, and main_
/  reads it back through BDOS 150 as its first act (sys/ccpsv.h).  It
/  used to be a page at TPA 0xFA00 that this startup had to leave alone,
/  because clearing it would have destroyed the very thing that had to
/  survive the reload.  Nothing above @MXTPA belongs to the CCP any more,
/  and nothing here has to know where the state lives.
/
/ ********************************************************************

	.globl	start
	.globl	main_
	.shri

start:
	jr	begin		/ text offset 0: the entry point
	ldk	r5, $0		/ text offset 2: warm-boot stub, in case
	sc	2		/   anything returns through stwo

begin:
	popl	rr2, (rr14)	/ discard the stwo return address
	popl	rr2, (rr14)	/ rr2 = far base page pointer

	/ clear BSS: base page lbss (offset 24) and bsslen (offset 28);
	/ both fit one segment, so only the low length word counts
	ldl	rr4, rr2(24)
	ldl	rr6, rr2(28)
	ld	r0, r7
	test	r0
	jr	eq, ccploop
	sub	r1, r1
1:
	ldb	(rr4), rl1
	inc	r5, $1
	djnz	r0, 1b

ccploop:
	sub	r13, r13	/ clear frame pointer
	call	main_		/ one command line
	jr	ccploop		/ and the next

/ ****** the BDOS gate *********************************************
/
/  UWORD bdos(func, parm)  WORD func;  LONG parm;
/
/  Identical to user/bdossc.s's __bdos, under the name ccp.c and
/  ccpext.c already call.  When the CCP was resident this symbol was a
/  plain C call into xbdos() through src/seam.c; now the CCP is a
/  program like any other and reaches the BDOS through SC #2, which is
/  what makes it interceptable by an RSX -- the thing sys/rsx.c said a
/  resident CCP could never be.
/
/  Register contract of the gate (sys/bdosglue.s): r5 = function,
/  rr6 = LONG parameter, result in r7.  rr6 is callee-saved in the C
/  ABI, so it is saved and restored around the trap.
/
/ *****************************************************************

	.globl	bdos_

bdos_:
	pushl	(rr14), rr6
	ld	r5, rr14(8)	/ func
	ldl	rr6, rr14(10)	/ param
	sc	2
	ld	r1, r7		/ result -> C int return
	popl	rr6, (rr14)
	ret
