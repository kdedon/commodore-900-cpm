/ Copyright (c) 2026 Kevin Dedon.
/ SPDX-License-Identifier: MIT

/ C900 trap handlers. Hardware pushes {id, FCW, PCseg, PCoff};
/ stubs save r0-r13 on the system stack before calling C or xvec handlers.
/ Frame offsets: r0-r13 0..27, id 28, FCW 30, PCseg 32, PCoff 34.
/ A program's recorded handler sees DRI's 40-byte frame instead; see
/ faultcom_.
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
	.globl	xvec_			/ per-process vector table (bios900.c)
	.globl	pgcur_			/   and the row: the running process
	.globl	panic_			/ frame printer (bios900.c)

XVNPROC	=	6			/ rows in xvec (bios900.c XVNPROC)

	.shri

/ EPU trap.  Every stock DRI binary's runtime startup executes Z8070
/ extended instructions (fldctl/fldil FP-state init, startup.8kn); the M20
/ handled them with its floating-point emulator, which this port excludes.
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
1:	call	xvget			/ FPE vector recorded (fn 22 slot 1)?
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
/
/ A recorded handler is a PROGRAM's (BIOS fn 22 -- DDT.Z8K registers
/ SC #0, vector 32, for its breakpoints), and it is written against DRI's
/ frame, not ours: biostrap.z8k's _trap saves the caller's normal r14/r15
/ between the registers and the hardware words, so a handler reads
/     r0-r13 0..27, nr14 28, nr15 30, id 32, FCW 34, PCseg 36, PCoff 38
/ (40 bytes; biosdefs.z8k scinst/scfcw/scseg/scpc).  So the 36-byte frame
/ is widened to that ONLY here, for the length of the call: the fourteen
/ register words move down four bytes, the hardware words stay where the
/ CPU put them, and NSPSEG/NSPOFF fill the gap.  Everything of ours --
/ the stubs above, bdosglue.s, ttick_, pdisp -- keeps the 36-byte frame.
/
/ The handler is called as a segmented subroutine in System mode, return
/ address on top of the frame.  It may rewrite any of it; DDT does (the
/ breakpoint's PC-2, and whatever registers its user edits) and switches
/ itself to Normal mode and back through BDOS fn 62 before its ret.  On
/ return every change is taken: nr14/nr15 back into NSPSEG/NSPOFF (as
/ _trap_ret's ldctl NSP), the registers shifted back up, and FCW and PC
/ were never moved, so the IRET takes them as they now stand.  Registers
/ on entry to the handler are NOT the caller's (DRI passed r2-r13 intact):
/ the frame is the interface, and it is all DDT reads.
faultcom_:
	call	xvget			/ rr2 = this process's vector r4
	testl	rr2
	jr	z, faultpanic_
	sub	r15, $4			/ widen: r0-r13 move down 4 bytes
	ldl	rr4, rr14		/   dst = the new SP
	ldl	rr6, rr14
	add	r7, $4			/   src = where the stub saved them
	ld	r0, $14			/   (ascending, dst below src: safe)
	ldir	@rr4, @rr6, r0
	ldctl	r0, NSPSEG		/ the gap: the caller's normal SP
	ld	rr14(28), r0
	ldctl	r0, NSPOFF
	ld	rr14(30), r0
	call	(rr2)			/ segmented subroutine call
	di	VI, NVI			/ the handler may have left them on
	ld	r0, rr14(28)		/ take its normal SP back
	ldctl	NSPSEG, r0
	ld	r0, rr14(30)
	ldctl	NSPOFF, r0
	ldl	rr4, rr14		/ narrow: r0-r13 move up 4 bytes
	add	r5, $26			/   src = last register word
	ldl	rr6, rr14
	add	r7, $30			/   dst = 4 bytes above it
	ld	r0, $14			/   (descending, dst above src: safe)
	lddr	@rr6, @rr4, r0
	add	r15, $4
	ldm	r0, (rr14), $14		/ its ret resumes the faulter,
	add	r15, $28		/   with whatever FCW and PC the
	iret				/   handler left in the frame

/ rr2 = the RUNNING PROCESS's recorded handler for vector r4 (0..47), or 0.
/ Clobbers r0 and r1.  The table is per process (bios900.c xvec): row
/ pgcur, the BIOS's mirror of the running descriptor (pgalloc.c), so a
/ vector one program records is never seen by another program's fault.
xvget:
	subl	rr2, rr2
	ld	r1, pgcur_
	cp	r1, $XVNPROC
	jr	uge, 1f			/ no row: nothing recorded
	ld	r0, r1
	sll	r0, $5			/ row * 48 = row * 32 + row * 16
	sll	r1, $4
	add	r1, r0
	add	r1, r4
	sll	r1, $2			/ * sizeof(long)
	ldl	rr2, xvec_(r1)
1:	ret

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
/ A caller in either mode is preempted only with an empty supervisor
/ stack (interrupted SP == sysstk), ensuring there is no active BDOS or
/ fault frame and no partially updated shared state.
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
	ld	r0, r15			/ Switch only if this process's
	add	r0, $36			/   supervisor stack is EMPTY.  The
	ld	r1, sysstk_		/   frame sits at SP-36, so SP+36 is
	cp	r0, r1			/   the stack top when there is no
	jr	ne, 1f			/   BDOS activation.  Asked in BOTH
					/   modes: a program's fault handler
					/   (faultcom_) may drop to Normal
					/   mode -- DDT's does -- with its
					/   trap frame still on this stack,
					/   and pdisp would save only the
					/   tick's frame and lose that one.
	ld	r2, r14			/ frame XADDR: high word = 0x3F00
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
