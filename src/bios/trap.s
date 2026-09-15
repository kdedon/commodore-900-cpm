
	.globl	tepa_, tprv_, tseg_, tnmi_, tnvi_, tvi_
	.globl	faultcom_, faultpanic_
	.globl	xvec_			/ 48-entry vector table (bios900.c)
	.globl	panic_			/ frame printer (bios900.c)

	.shri

/ EPU trap.  Every stock DRI binary's runtime startup executes Z8070
/ extended instructions (fldctl/fldil FP-state init, startup.8kn); the M20
/ handled them with the FPE emulator, which this port excludes (plan D1).
/ Policy: a genuine EPA opcode (high byte 0E/0F/4E/4F/8E/8F) with no
/ recorded FPE vector (slot 1) is skipped -- the hardware pushes the PC
/ past the whole instruction, so a plain dismiss resumes after it and FP
/ initialization becomes a no-op.  Anything else (a recorded vector, or a
/ non-EPA identifier, e.g. a truly unknown opcode) takes the normal
/ vector-or-panic path.
tepa_:
	sub	r15, $28
	ldm	(rr14), r0, $14
	ld	r4, $1			/ EPUTRAP
	ld	r0, rr14(28)		/ identifier = trapped opcode word
	and	r0, $0xFE00		/ EPA shapes: 0E/0F, 4E/4F, 8E/8F
	cp	r0, $0x0E00
	jr	eq, 1f
	cp	r0, $0x4E00
	jr	eq, 1f
	cp	r0, $0x8E00
	jr	nz, faultcom_
1:	ldl	rr2, xvec_+4		/ FPE vector recorded (fn 22 slot 1)?
	testl	rr2
	jr	nz, faultcom_		/   yes: dispatch it
	ldm	r0, (rr14), $14		/   no: skip the FP instruction
	add	r15, $28
	iret

tprv_:
	sub	r15, $28
	ldm	(rr14), r0, $14
	ld	r4, $8			/ PITRAP
	jr	faultcom_

tseg_:
	sub	r15, $28
	ldm	(rr14), r0, $14
	sinb	rl0, 0x02fc		/ read the Z8010 violation type and
	soutb	0x11fc, rl0		/   reset the violation latch
	ld	r4, $2			/ SEGTRAP
	jr	faultcom_

tnmi_:
	sub	r15, $28
	ldm	(rr14), r0, $14
	ld	r4, $0			/ NMITRAP
	jr	faultcom_

tnvi_:
	sub	r15, $28
	ldm	(rr14), r0, $14
	ld	r4, $6			/ C900: NVI (unassigned in the M20 set)
	jr	faultcom_

tvi_:
	sub	r15, $28
	ldm	(rr14), r0, $14
	ld	r4, $7			/ C900: VI (unassigned in the M20 set)
	jr	faultcom_

/ Common fault path.  r4 = trap-vector number (0..47), frame as above.
faultcom_:
	ld	r5, r4
	sll	r5, $2
	ldl	rr2, xvec_(r5)		/ recorded handler?
	testl	rr2
	jr	z, faultpanic_
	call	(rr2)			/ yes: segmented subroutine call;
	ldm	r0, (rr14), $14		/   its ret resumes the faulter
	add	r15, $28
	iret

faultpanic_:
	ld	r0, rr14(34)		/ PC offset (post-instruction)
	sub	r0, $2			/ -> address of the instruction itself
	ld	r1, rr14(32)		/ PC segment word
	ld	r2, rr14(30)		/ caller FCW
	ld	r3, rr14(28)		/ identifier = instruction word
	push	(rr14), r0		/ panic_(vec, id, fcw, pcseg, pcoff)
	push	(rr14), r1
	push	(rr14), r2
	push	(rr14), r3
	push	(rr14), r4
	call	panic_
	/ panic_ warm-boots a Normal-mode faulter (no return); a
	/ System-mode fault falls back here: stop dead.
fhang:
	halt
	jr	fhang
