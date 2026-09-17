/ Copyright (c) 2026 Kevin Dedon.
/ SPDX-License-Identifier: MIT

/ procasm.s -- the two halves of a context switch that C cannot write.
/
/ src/bdos/proc.c does everything else: it moves pages, stvars, the RSX
/ fence and the descriptors.  What is left here is the register file and
/ the two control registers, and both are one instruction each that the
/ compiler has no way to emit.
/
/ Convention (matches crt.s and glue.s): a segmented call pushes a 4-byte
/ return address, so the first argument is at rr14(4); a LONG/pointer
/ argument is two words.  r6..r12 are callee-saved; nothing here needs
/ them.
/
/ struct pframe (src/bdos/proc.h) is the SC gate's own frame layout with
/ the two banked stack registers appended:
/    0..27  r0-r13   28 id   30 FCW   32 PCseg   34 PCoff
/      36  NSPSEG   38 NSPOFF

	.globl	presume_, pnspget_, pcopark_, pcoresume_
	.globl	pgone_		/ src/bdos/proc.c
	.globl	sysstk_		/   and the stack top it publishes

	.shri

/ pnspget(pf)  LONG pf;  -- the save half's last two words.
/
/ A non-segmented Normal-mode program's r14/r15 are not in the register
/ file the gate saved: the hardware banks them in NSPSEG/NSPOFF, which
/ xfer_ (src/bios/glue.s) loaded at launch and which nothing has touched
/ since.  LDCTL is a System-mode instruction and this runs inside the SC
/ trap, so it is legal exactly where it is used and nowhere else.
pnspget_:
	ldl	rr2, rr14(4)		/ far pointer to the pframe
	ldctl	r0, NSPSEG
	ld	rr2(36), r0
	ldctl	r0, NSPOFF
	ld	rr2(38), r0
	ret

/ presume(pf)  LONG pf;  -- the restore half.  Never returns.
/
/ This IS xfer_ (src/bios/glue.s) with two differences and no third:
/ it restores fourteen saved registers instead of a launch context's
/ zeros, and it takes the FCW and the identifier from the frame the
/ process was trapped with rather than manufacturing them.  The system
/ stack reset stays -- and it is the reason a switch can only be taken
/ at the gate's IRET point.  Every C frame below the current SP belongs
/ to the process being parked, and there is exactly one moment in this
/ system when there are none: the instant the gate is about to return to
/ user code.  proc.c's banner is the argument; this instruction is where
/ the argument is spent.
/
/ The masking of VIE/NVIE that xfer_ does is NOT repeated here.  It was
/ already applied when the frame was built (proc.c pcreate) or when the
/ process was launched, and a saved FCW is the caller's own -- a program
/ that reached System mode through BDOS function 62 has to come back to
/ it, and re-masking a live FCW would silently change a running
/ program's interrupt state at a scheduling point.
/ THE `di VI' IS LOAD-BEARING AND C7 PUT IT THERE.  ttick_ (src/bios/
/ trap.s) now takes a switch from a SYSTEM-mode frame when the supervisor
/ stack is empty, and the four instructions after the stack reset below
/ are System-mode code running at exactly `sysstk'.  pgone() reaches here
/ from the SC gate as well as from the tick, and the gate's FCW has VIE
/ SET -- so without this, a tick landing on `ld r0, rr2(34)' would call
/ pdisp(), which copies the interrupted frame into pd[pcur].pd_f, which
/ is THE VERY STRUCTURE rr2 is pointing at.  The resumed process would
/ then IRET back into the middle of presume_ forever.  `make
/ verify-concdir' found it on the first suite run and looked like a
/ machine that had gone quiet.  The IRET at the end restores the resumed
/ process's own FCW, so nothing has to turn interrupts back on.
presume_:
	di	VI
	ldl	rr2, rr14(4)		/ far pointer to the pframe
	ld	r0, rr2(36)
	ldctl	NSPSEG, r0
	ld	r0, rr2(38)
	ldctl	NSPOFF, r0
	ld	r14, $0x3f00		/ the system stack, back to its top --
	ld	r15, sysstk_		/   THIS PROCESS'S top.  It was the
					/   literal 0xFC00, which is still what
					/   sysstk holds for process 0 and for
					/   every system with one process;
					/   proc.c sets it per switch because a
					/   process parked inside a BDOS call
					/   has frames on a stack of its own
					/   (proc.h PSTKOF)
	ld	r0, rr2(34)		/ PC offset
	push	(rr14), r0
	ld	r0, rr2(32)		/ PC segment word (0xSS00)
	push	(rr14), r0
	ld	r0, rr2(30)		/ FCW
	push	(rr14), r0
	ld	r0, rr2(28)		/ identifier word
	push	(rr14), r0
	ldm	r0, (rr2), $14		/ r0-r13 (LDM latches the address
					/   before r2/r3 are overwritten)
	iret				/ -> the other process, mid-BDOS-return

/ pcopark(slotp)  LONG slotp;  -- PARK THE CALLING PROCESS MID-CALL, which
/ is the whole of what S4 adds to the switch.  Never returns to its caller
/ here and now; it returns to its caller LATER, when pcoresume_ gives this
/ process the machine back, and to the C code in between it looks like an
/ ordinary function call that took a very long time.
/
/ presume_ above can only restart a process whose entire supervisor state
/ is a 36-byte frame -- true at the gate's IRET and nowhere else.  A
/ process blocked on the console is somewhere else entirely: it has C
/ frames, a return address and callee-saved registers, and they are on the
/ system stack.  So this saves what the ABI says a call must preserve
/ (r6-r13; r0-r5 are caller-saved and the caller has already dealt with
/ them), hands the stack pointer to the descriptor through slotp, and calls
/ pgone_ to give the machine away.  The frames stay exactly where they
/ are, untouched, because the process that runs next runs on its own
/ stack (proc.h PSTKOF) -- that is what the per-process stack is FOR.
pcopark_:
	ldl	rr2, rr14(4)		/ far pointer to the WORD save slot
	push	(rr14), r13
	pushl	(rr14), rr12
	pushl	(rr14), rr10
	pushl	(rr14), rr8
	pushl	(rr14), rr6
	ld	r0, r15			/ the parked stack pointer: it addresses
	ld	rr2(0), r0		/   the saved rr6, and pcoresume_ pops
					/   from exactly here
	call	pgone_			/ give the machine away; no return.  The
					/   return address this pushes lands
					/   BELOW the saved SP, on stack nothing
					/   reads again
	halt				/ pgone_ does not return

/ pcoresume(pf, sp)  LONG pf; WORD sp;  -- the other half.  Restores the
/ user stack pointer this process was banked with, moves to its supervisor
/ stack, and returns FROM ITS pcopark_ into the C it was parked in.
/
/ NSPSEG/NSPOFF are two control registers and there is one pair on the
/ machine, so they belong to whoever is running: they were saved into pf
/ by pnspget_ at the park and they have to come back here, or the resumed
/ process would return to user code on another process's stack.
pcoresume_:
	ldl	rr2, rr14(4)		/ far pointer to the pframe
	ld	r4, rr14(8)		/ the parked supervisor SP
	ld	r0, rr2(36)
	ldctl	NSPSEG, r0
	ld	r0, rr2(38)
	ldctl	NSPOFF, r0
	ld	r14, $0x3f00
	ld	r15, r4
	popl	rr6, (rr14)
	popl	rr8, (rr14)
	popl	rr10, (rr14)
	popl	rr12, (rr14)
	pop	r13, (rr14)
	ret				/ -> back into the parked BDOS call
