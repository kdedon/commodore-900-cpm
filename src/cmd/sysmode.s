/ sysmode.s -- Run a counted register-only loop in segmented System mode via BDOS 62.
/ LDCTL verifies System mode and supplies the returned FCW. rr14 switches
/ to the supervisor stack, so the loop must not use C frames or push:
/ System-mode preemption requires an empty supervisor stack.
/ IRET restores Normal mode using {identifier, FCW, PCseg, PCoff}; the
/ hardware restores the banked Normal stack. Preserve callee-saved r6/r7.

	.globl	sysspin_
	.shri

sysspin_:
	pushl	(rr14), rr6
	ld	r4, rr14(8)		/ n: 4 bytes of return address plus
					/   the 4 just pushed
	ldl	rr6, $back		/ where the IRET below comes back to
	ld	r5, $62			/ fn 62: set system mode
	sc	2

/ ---- SEGMENTED SYSTEM MODE.  r0-r13 came through the gate untouched
/ (scret restores all fourteen); rr14 is the system stack, at its top.
	ldctl	r2, FCW			/ the proof, and a privilege trap if
					/   function 62 did not take effect
1:	ld	r3, $0			/ 65536 decrements
2:	dec	r3, $1
	jr	nz, 2b
	dec	r4, $1
	jr	nz, 1b

/ ---- back to Normal mode.
	push	(rr14), r7		/ PC offset
	push	(rr14), r6		/ PC segment word (0xSS00)
	ld	r0, $0x9000		/ segmented, Normal, VIE
	push	(rr14), r0
	push	(rr14), r0		/ junk identifier word
	iret

back:
	popl	rr6, (rr14)
	ld	r1, r2			/ WORD result: the System-mode FCW
	ret
