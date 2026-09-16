/ C900 trap handlers. Hardware pushes {id, FCW, PCseg, PCoff};
/ stubs save r0-r13 on the system stack before calling C or xvec handlers.
/ Frame offsets: r0-r13 0..27, id 28, FCW 30, PCseg 32, PCoff 34.
/ Vector numbering follows the M20: NMI 0, EPU 1, SEG 2, PRV 8;
/ C900 uses 6/7 for NVI/VI. Unhandled Normal-mode faults warm boot;
/ unhandled System-mode faults halt.

	.globl	tepa_, tprv_, tseg_, tnmi_, tnvi_, tvi_
	.globl	faultcom_, faultpanic_
	.globl	ttick_, tvidsm_, tickget_, tickei_
	.globl	sccrxi_			/ SCC receive interrupt stub (C8)
	.globl	sccrxdrn_		/   and its body (bios/bios900.c)
	.globl	tickcnt_		/ the tick counter (bios/tick900.c)
	.globl	pquant_			/ ticks left in this process's slice
	.globl	psched_			/ nonzero when more than one process
	.globl	pdisp_			/   is live; the dispatcher (proc.c)
	.globl	sysstk_			/ the running process's supervisor
					/   stack TOP (proc.c; proc.h PSTKOF)
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
	/ Report PC-2 from the saved resume address. For multiword opcodes
	/ this may name an operand; the identifier remains the first word.
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

/ 100 Hz CIO #1 CT3 ISR, vector 0. Clear level-triggered IP/IUS first.
/ The 36-byte saved frame matches struct pframe for pdisp().
/ Spend one quantum tick; clamp an exhausted quantum until a safe switch.
/ Normal-mode callers are safe. System-mode callers are preempted only
/ with an empty supervisor stack (interrupted SP == sysstk), ensuring
/ there is no active BDOS frame or partially updated shared state.
/ Entry FCW disables interrupts, making scheduler state changes atomic.
ttick_:
	sub	r15, $28
	ldm	(rr14), r0, $14		/ r0-r13 under the hardware frame
	ldb	rl0, $0x24		/ clear IP and IUS; gate stays open
	outb	0x0019, rl0		/   CT3 command and status
	ldl	rr0, tickcnt_
	addl	rr0, $1
	ldl	tickcnt_, rr0
	ld	r0, psched_		/ more than one process live?
	test	r0
	jr	z, 1f
	ld	r0, pquant_		/ THE QUANTUM.  A slice is pd_quant
	dec	r0, $1			/   ticks long (proc.h PQBASE), and
	ld	pquant_, r0		/   only the tick that spends the
	jr	gt, 1f			/   last of it asks for a dispatch.
	ld	r0, $0			/ clamp while a BDOS call blocks preemption
	ld	pquant_, r0
	ld	r0, rr14(30)		/ the interrupted FCW
	bit	r0, $14			/ S/N: clear = Normal mode, the TPA,
	jr	z, 2f			/   nothing of ours on this stack
	ld	r0, r15			/ System mode: switch only if this
	add	r0, $36			/   process's supervisor stack is
	ld	r1, sysstk_		/   EMPTY.  The frame sits at SP-36,
	cp	r0, r1			/   so SP+36 is the stack top when
	jr	ne, 1f			/   there is no BDOS activation
2:	ld	r2, r14			/ frame XADDR: high word = 0x3F00
	ld	r3, r15			/   (seg << 8), low word = offset
	pushl	(rr14), rr2
	call	pdisp_			/ returns only if this process is
	add	r15, $4			/   still the right one to run
1:	ldm	r0, (rr14), $14
	add	r15, $28
	iret

/ PDMAC disk-completion interrupt, vector 0x80. The polled WD path
/ handles transfer completion; this stub only restores the CPU frame.
tvidsm_:
	iret

/ All armed SCC receive vectors share this stub. Save r0-r13 for C;
/ sccrxdrn drains each channel's data register, clearing receive level.
/ Entry FCW disables interrupts, preventing nesting.
sccrxi_:
	sub	r15, $28
	ldm	(rr14), r0, $14		/ r0-r13 under the hardware frame
	call	sccrxdrn_
	ldm	r0, (rr14), $14
	add	r15, $28
	iret

/ long tickget() -- the tick, read in ONE instruction.  A C `long' load
/ is two word loads and ttick_ can land between them; LDL cannot be split,
/ so this is the only sanctioned reader of tickcnt_.
tickget_:
	ldl	rr0, tickcnt_
	ret

/ Enable VI after tick initialization. The SC PSA entry and xfer_
/ preserve VIE for system-call handling and user-program preemption.
tickei_:
	ei	VI
	ret
