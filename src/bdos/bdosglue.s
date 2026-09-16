/ ***************************************************************
/ 
/ 	CP/M-Z8K Basic Disk Operating System interface module
/ 		For "C" version of CP/M-Z8K
/ 
/ 	Copyright (c) 1982 Digital Research, Inc.
/ 
/ 	Version 0.2 -- September 22, 1982
/ 	Z8000 version -- 821014
/ 
/ 	C900 translation (M3) of ref/may83/bdos/bdosif.z8k:
/ 	  - Zilog asz8k syntax -> as-z8001 (`;' -> `/' comments,
/ 	    .sect -> .shri, .equ -> `=', #imm -> $imm, @Rn -> (RRn))
/ 	  - symbol convention: Zilog prefix `_foo' -> our SUFFIX `foo_'
/ 	  - D3: the BDOS->BIOS gate is a DIRECT `call bios_' instead of
/ 	    the SC #3 trap; bios1_..bios6_ re-marshal their C stack args
/ 	    into a plain C call  bios_(func, parm1, parm2)  with
/ 	    (WORD func, LONG parm1, LONG parm2).
/ 	  - our C ABI (MWC cc2, VKERN): args pushed right-to-left,
/ 	    caller pops; 4-byte segmented return address, so the first
/ 	    arg is at rr14(4); WORD returns in r1, LONG/pointer in rr0
/ 	    (a LONG's low word lands in r1, so word-callers just work);
/ 	    r6..r12 are callee-saved -- these helpers use only r0-r5.
/ 
/ ***************************************************************

#include "c900cfg.h"

	.shri

/ ***************************************************
/ 
/   Globals
/ 
/ ***************************************************

	.globl	bios1_		/ 6 BIOS entry points from BDOS
	.globl	bios2_
	.globl	bios3_
	.globl	bios4_
	.globl	bios5_
#ifdef BOOT_TRACE
	.globl	bt_sc_			/ opt-in cold-boot marker (cmain.c)
#endif
	.globl	bios6_
	.globl	traphnd_	/ the SC trap gate (PSA offset 24 -> here)
	.globl	scentry_	/   (same entry; traphnd_ kept for bdosmisc.c)
	.globl	swap_		/ byte swapper
	.globl	udiv_		/ unsigned divide routine

/ ***************************************************
/ 
/  Externals and Constants
/ 
/ ***************************************************

	.globl	xbdos_		/ BDOS entry point in bdosmain (D3: was __bdos)
	.globl	bios_		/ C BIOS dispatcher (integration provides)
	.globl	faultcom_	/ trap.s: vector-or-panic path for other SCs
	.globl	faultpanic_
	.globl	map_adr_	/ BIOS side (SC #1 gate targets)
	.globl	mem_cpy_
	.globl	xfer_
	.globl	spemu_		/ split-I/D shim: SC #255 emulator (splitsc.c)
	.globl	spusp_		/   user r15/r14 mirrors around spemu_
	.globl	spufp_
	.globl	spflag_		/   loaded-program-is-split (splitld.c)
	.globl	rsxhead_	/ RSX chain head and fence, TPA offsets
	.globl	rsxtop_		/   (sys/rsx.c)
	.globl	psched_		/ nonzero when more than one process is
	.globl	pdisp_		/   live; the dispatcher (sys/proc.c)

/  The following were put in so that all BDOS modules were
/  referenced, so they could be put in a library

	.globl	constat_	/ references conbdos.o
	.globl	dirscan_	/ references dskutil.o
	.globl	create_		/ references fileio.o
	.globl	bdosrw_		/ references bdosrw.o

biosf	=	50
setsupf	=	62

/ ***************************************************
/
/  System Call Trap Gate
/
/  Entered directly from the PSA SC entry (crt.s psa+24) in segmented
/  System mode, FCW 0xC000 (all interrupts off), on the system stack
/  (seg 0x3F, primed by xfer_).  The CPU has pushed the 4-word frame
/  {identifier = 0x7F00|n, caller FCW, caller PCseg, caller PCoff};
/  all caller registers r0-r13 arrive live; a nonseg caller's r14/r15
/  are parked in NSPSEG/NSPOFF and need no saving here.
/
/  Handled system calls:
/    SC #2  BDOS:  r5 = function, rr6 = LONG parameter, result -> r7
/           (syscall.z8k _bdos / DRI startup.8kn ___BDOS contract)
/           fn 62 = set caller FCW to system+segmented (setsup)
/           fn 50 = direct BIOS call through a 5-word parameter block
/    SC #3  BIOS:  r3 = function, rr4 = P1, rr6 = P2, result -> rr6
/           (syscall.z8k _bios contract)
/    SC #254 the RSX return trampoline (rsxback): issued by the two
/           bytes rsxgon plants on a non-segmented caller's stack, and
/           by nothing else.  A program that issues one by hand unwinds
/           itself with rubbish, exactly as one that jumps into its own
/           stack does; it used to reach the panic path instead.
/    other  a handler recorded via BIOS fn 22 (vector 32+n), else panic
/
/  A non-segmented Normal caller's pointer parameters carry a
/  meaningless high word (nonseg C zero-extends 16-bit pointers); the
/  gate substitutes the caller's PC-segment word, exactly as the
/  original bdosif.z8k/biostrap.z8k did.
/
/  Frame layout after the prologue (offsets from rr14):
/    +0..27  r0-r13   +28 identifier   +30 FCW   +32 PCseg   +34 PCoff
/  Result slots: saved r7 = +14, saved rr6 = +12.
/
/ ***************************************************

scentry_:
traphnd_:
	sub	r15, $28
	ldm	(rr14), r0, $14		/ save caller r0-r13
#ifdef BOOT_TRACE
	call	bt_sc_			/ marker <T>, once: the FIRST SC trap
					/   reached the gate.  Placed after the
					/   register save, so clobbering r0-r13
					/   costs nothing; r0 is reloaded below.
#endif
	ld	r0, rr14(28)		/ identifier = 0x7F00 | SC number
	cp	r0, $0x7FFF		/ split-I/D data-access trap: by far
	jr	eq, splitgate		/   the hottest SC -- test it first
	cp	r0, $0x7F02
	jr	eq, bdosgate
	cp	r0, $0x7F03
	jr	eq, biosgate
	cp	r0, $0x7F01
	jr	eq, memgate
	cp	r0, $0x7FFE		/ RSX return trampoline (rsxgon); it
	jp	eq, rsxback		/   is here and not higher up because
					/   only a non-segmented caller with a
					/   module in the chain ever issues it
	/ any other SC: trap-vector number 32+n (M20 numbering), then the
	/ recorded-vector-or-panic path shared with the fault stubs
	ld	r4, r0
	and	r4, $0x00FF
	add	r4, $32
	cp	r4, $48
	jr	uge, 1f
	jp	faultcom_
1:	jp	faultpanic_

/ SC #1: memory-management gate (syscall.z8k MEM_SC/XFER_SC; DDT lives on
/ it).  Register contract: rr6 = address/src, an XADDR from a segmented
/ caller and a zero-extended 16-bit pointer from a non-segmented one:
/   rr2 != 0                ->  mem_cpy(src=rr6, dst=rr4, len=rr2)
/   rr2 == 0, rr4 == -2     ->  xfer(rr6)  (context switch; never returns)
/   rr2 == 0 otherwise      ->  rr6 = map_adr(rr6, space=r5)
memgate:
/
/	A NON-SEGMENTED Normal caller's pointer parameters carry a
/	meaningless ZERO high word, because non-segmented C zero-extends a
/	16-bit pointer.  Substitute the caller's PC segment, exactly as
/	bdosgate does above.
/
/	This gate used to claim that no such fixup applied to it, on the
/	grounds that its caller "always passes a full 32-bit XADDR".
/	DDT.Z8K is a non-segmented caller and does not: its xfer context
/	pointer arrived as 0x0000EB6C, which names SEGMENT 0 -- ROM, which
/	reads 0xFFFF -- so xfer_ launched a context of 0xFFFF words and,
/	because that context's FCW word had the System bit set, did so in
/	System mode, on top of every supervisor stack in segment 0x3F.
/
/	Only a ZERO high word is replaced, so the xfer selector in rr4
/	(0xFFFFFFFE) is never touched; rr2 is a LENGTH and is left alone.
/
	ld	r0, rr14(30)		/ caller's FCW
	bit	r0, $15
	jr	nz, memdisp		/   segmented: the pointers are XADDRs
	bit	r0, $14
	jr	nz, memdisp		/   system: ditto
	ld	r0, rr14(32)		/ caller's PC segment word (0xSS00)
	test	r6
	jr	nz, memfixd
	ld	r6, r0			/   rr6: address / src / context block
memfixd:
	test	r4
	jr	nz, memdisp
	ld	r4, r0			/   rr4: dst of the copy form
memdisp:
	testl	rr2
	jr	nz, memcpy
	cpl	rr4, $0xFFFFFFFE
	jr	eq, memxfer
	push	(rr14), r5		/ space
	pushl	(rr14), rr6		/ address
	call	map_adr_
	add	r15, $6
	ldl	rr14(12), rr0		/ result -> caller rr6
	jr	scret
memcpy:
	pushl	(rr14), rr2		/ len
	pushl	(rr14), rr4		/ dst
	pushl	(rr14), rr6		/ src
	call	mem_cpy_
	add	r15, $12
	jr	scret
memxfer:
	pushl	(rr14), rr6		/ context far pointer
	call	xfer_			/ resets the system stack; no return

/ SC #255: split-I/D data-access trap (Option 6 shim).  spfast_
/ (splitfast.s) emulates the dominant load/store forms in assembly
/ straight off this frame and resumes through spret_; every other form
/ comes back here (spslow_) for spemu_ (splitsc.c), which looks the PC
/ up in the side table, emulates the one data access against the right
/ bank, and advances the saved PC.  The user's r14/r15 are banked in
/ NSPSEG/NSPOFF; they are mirrored through spufp_/spusp_ so the C
/ emulator can read and write them (LDM, pushes, pointer autoincrement).
/ A nonzero return means the PC was not in the side table (a genuine
/ SC #255 -- no such caller exists) or an unemulated form: panic loudly,
/ never continue silently.
	.globl	spfast_
	.globl	spslow_
splitgate:
	jp	spfast_
spslow_:
	ldctl	r1, NSPSEG
	ld	spufp_, r1
	ldctl	r1, NSPOFF
	ld	spusp_, r1
	ld	r2, r14			/ frame XADDR: high word = 0x3F00
	ld	r3, r15			/   (seg << 8), low word = offset
	pushl	(rr14), rr2
	call	spemu_
	add	r15, $4
	ld	r0, spusp_
	ldctl	NSPOFF, r0
	ld	r0, spufp_
	ldctl	NSPSEG, r0
	test	r1			/ WORD result: 0 = emulated
	jp	z, scret
	ld	r4, $287		/ vector 32 + 255 for the panic print
	jp	faultpanic_

/ SC #2: the BDOS gate
bdosgate:
	cp	r5, $setsupf	/ fn 62: set system mode
	jr	eq, setsup
	cp	r5, $biosf	/ fn 50: call bios direct
	jr	eq, bioscall
	ld	r0, rsxhead_	/ resident system extensions (sys/rsx.c)
	test	r0
	jr	nz, rsxenter
notrsx:
/
/	If caller was a non-segmented user program,
/	get the parameter's segment from his program counter
/
	ldl	rr2, rr6		/ LONG parameter
	ld	r0, rr14(30)		/ caller's FCW
	bit	r0, $15
	jr	nz, callC		/   segmented
	bit	r0, $14
	jr	nz, callC		/   system
	ld	r2, rr14(32)		/   user nonseg: seg from PC
/	Split-I/D caller: a D-space pointer maps to the data bank unless
/	it reaches the stack region (>= user SP), which lives in the TPA.
	ld	r0, spflag_
	test	r0
	jr	z, callC
	ldctl	r0, NSPOFF		/ user SP at call time
	cp	r3, r0
	jr	uge, callC		/ stack/base page: keep the PC seg
	ld	r2, $[SPLITDSEG*256]	/ static/heap: the data bank
/
/	Call C main routine: xbdos_(cmd, (word)param, (addr)param)
/
callC:
	pushl	(rr14), rr2	/ xaddr param.
	push	(rr14), r7	/ word  param (low word of rr6)
	push	(rr14), r5	/ command
	call	xbdos_
	add	r15, $8
/
/	Return result in caller's r7 (our ABI: WORD result in r1)
/
	ld	rr14(14), r1
/
/	THE DISPATCH POINT (src/bdos/proc.c).  The BDOS has returned, its
/	answer is in the caller's saved r7, and the frame above rr14 is the
/	whole of this process's supervisor state -- there is no C frame
/	left, no lock held and nothing below the SP that matters.  That is
/	the one instant in this system at which another process can be
/	given the machine, and it is why `psched' is tested HERE and not at
/	scret: spret_ (the split-I/D fast path) and the RSX and BIOS
/	returns join below, and none of them is a BDOS call boundary.
/
/	`psched' is zero unless two or more processes are live, which is
/	every system this port has shipped, so the ordinary cost is one
/	load, one test and one taken branch.  pdisp_ returns only when the
/	running process is still the right one; otherwise it never comes
/	back, because presume_ (procasm.s) resets this stack and IRETs into
/	the other process's own frame.
/
	ld	r0, psched_
	test	r0
	jr	z, scret
	ld	r2, r14			/ frame XADDR: high word = 0x3F00
	ld	r3, r15			/   (seg << 8), low word = offset
	pushl	(rr14), rr2
	call	pdisp_
	add	r15, $4
	.globl	spret_
spret_:
scret:
	ldm	r0, (rr14), $14	/ restore r0-r13 (incl. the result)
	add	r15, $28
	iret			/ pop id, reload caller FCW + PC

/
/ Resident System Extensions: hand this call to the chain instead of to
/ the BDOS (sys/rsx.c has the whole design; rsxhdr.h has the prefix).
/
/ The 8080 does this with a jump: location 0005h points at the newest
/ module, so a program's `call 5' lands in the RSX with the return
/ address already on the caller's stack, and the module returns to the
/ caller when it is done (ref/cpm3/loader3.asm:276-307).  There is no
/ location 5 here, so the gate manufactures the same situation: it
/ pushes the caller's own return address onto the caller's stack and
/ IRETs to the module's entry instead of to the instruction after the
/ `sc 2'.  The module runs in the caller's mode on the caller's stack,
/ sees the caller's r5/rr6 untouched, and its final `ret' goes straight
/ back to the caller with the result in r7 -- the same register the
/ gate would have returned it in.
/
/ Two callers are passed straight to the BDOS:
/   - a System-mode caller: that is the resident CCP, which reaches the
/     BDOS by C call anyway (src/seam.c:17) and only arrives here
/     through the fn-50/62 services above;
/   - a SPLIT-I/D caller.  Its data references are trapped and rewritten
/     (Option 6), which an RSX's would not be: the module arrives after
/     the load and is never scanned, so its own data references would
/     resolve to the TPA rather than to the D bank.  8080 CP/M 3 has no
/     split I/D to be faithful to.  DEVIATIONS.md #7 records this one as
/     structural, and it still is.
/
/ THAT SECOND TEST USED TO BE FCW BIT 15, and bit 15 is the SEGMENTED
/ bit, not the split bit.  Non-segmented covers two containers, not one:
/ 0xEE0B (split I/D) and 0xEE03 (non-segmented, combined I/D --
/ x.out.h:21,23).  Only the first has the trapped-data-reference problem;
/ an 0xEE03 program's data is in the TPA, in the same segment the module
/ occupies.  So the bit test excluded DDT.Z8K, SDB.Z8K and every other
/ stock combined-I/D binary for a reason that does not apply to them, and
/ what it should have asked is what the LOADER knows: `spflag'
/ (splitld.c:30, set at pgmld.c:331, per-process and saved across a swap
/ at proc.c:351,364) is exactly "the loaded program is split I/D".
/
/ Letting an 0xEE03 caller in costs one thing the bit test hid.  The gate
/ hands the module control by IRET with the CALLER'S FCW, so the module
/ ran in the caller's mode -- and every module is assembled segmented
/ (`.shri': rr14 as a stack pointer, segmented register indirect, 4-byte
/ `ret' frames), because it must also serve the CCP, which is segmented,
/ and one image cannot be both.  So a non-segmented caller's module call
/ is entered in SEGMENTED mode and has to come back out of it, and `ret'
/ cannot change mode.  rsxgon below builds a return trampoline on the
/ caller's own stack -- in the TPA, the segment the module lives in --
/ whose one instruction is `sc 254', and rsxback restores the caller's
/ mode, r14, r6 and PC from the words beneath it.  The cost is one extra
/ trap per intercepted call, and only for a non-segmented caller.
/
/ A third caller -- one already inside the RSX area -- is a module
/ passing the call down, and that is NOT the same thing as a call for
/ the BDOS.  On the 8080 the module jumps to its own `next' field, which
/ holds the module above it and holds the BDOS only in the last module
/ of the chain (dirlbl.asm:43-45 `lhld NEXTa ! pchl'; loader3.asm:
/ 294-300 `fixchain1' is what puts the module above into that field).
/ The gate does that here, once, instead of once per module: it finds
/ the module the return address lies in and dispatches through THAT
/ module's `next' field, so a two-module chain passes down through both
/ and only the last one reaches the BDOS.  v3's
/ own version of this test is a module comparing the return address page
/ with its own (getrsx.asm:198-202); made in the gate it also spares
/ every module the code and cannot be got wrong per module.
/
rsxenter:
	ld	r1, rr14(30)		/ caller's FCW
	bit	r1, $14
	jr	nz, notrsx		/ system caller
	ld	r1, spflag_
	test	r1
	jr	nz, notrsx		/ split-I/D program (see above)
	ld	r2, rr14(32)		/ caller's PC segment word.  The
	ld	r1, r2			/   hardware pushes it in the LONG
	and	r1, $0x7F00		/   form -- bit 15 set, segment in
	cp	r1, $[TPASEG*256]	/   14..8 -- which is also the form
	jr	ne, notrsx		/   `ret' wants back, so the raw word
					/   is what gets pushed below and only
					/   the comparison masks it
	ld	r3, rr14(34)		/ caller's PC offset
	ld	r1, rsxtop_
	cp	r3, r1
	jr	ult, rsxgo		/ an ordinary transient: the head module
/
/ The caller is a module: this call goes to the module ABOVE it, or to
/ the BDOS if there is none.  r0 walks the chain from the head looking
/ for the module the return address lies in; r10/r11 address that
/ module's prefix, whose `next' (0Ah) is the answer and whose `len'
/ (1Eh) bounds it.  Nothing but r0/r1/r3/rr10 may be touched before the
/ walk commits, because falling out of it into notrsx has to leave the
/ BDOS call exactly as it arrived (r5 = function, rr6 = parameter).
/
	dec	r3, $1			/ the last byte of the `sc 2': that
					/   byte is inside the module even
					/   when the instruction after it is
					/   the first byte of the next one
	ld	r10, $[TPASEG*256]
/ Only the TOP of each module is tested, and that is exact rather than
/ half a test: the walk runs from the lowest module upwards and is only
/ entered when the return address is at or above rsxtop_, which is the
/ lowest module's base, so the first module whose top is above the
/ return address is the module the return address is in.
rsxwalk:
	ld	r11, r0			/ rr10 -> this module's prefix
	ld	r1, rr10(30)		/ its length ...
	add	r1, r0			/   ... so this is its top
	cp	r3, r1
	jr	ult, rsxup		/ the caller is this module
	ld	r0, rr10(10)		/ no: try the module above it
	test	r0
	jr	nz, rsxwalk
	jr	notrsx			/ inside no module at all: the BDOS
rsxup:
	ld	r0, rr10(10)		/ the module above the caller
	test	r0
	jr	z, notrsx		/ the last module: on to the BDOS
	inc	r3, $1			/ the return address again
rsxgo:
	ld	r1, rr14(30)		/ caller's FCW
	bit	r1, $15
	jr	z, rsxgon		/ non-segmented: the trampoline below
	ldctl	r1, NSPOFF		/ push the return address on the
	sub	r1, $4			/   caller's own stack, segment
	ldctl	NSPOFF, r1		/   first: a segmented `ret' frame
	ldctl	r4, NSPSEG
	ld	r5, r1
	ld	(rr4), r2
	ld	rr4(2), r3
	ld	r4, $[TPASEG*256]	/ resume at r0's entry (prefix offset
	ld	r5, r0			/   6, rsxhdr.h): the head module for a
					/   transient, the next one along for a
					/   module passing the call down
	ld	r3, rr4(6)
	ld	rr14(34), r3
	jr	scret

/
/ A non-segmented (0xEE03) caller.  Four things differ from the path
/ above, and each of them is the same fact seen from a different side:
/ in non-segmented Normal execution the user's r14 is a DATUM, not a
/ segment, so the stack is addressed as TPASEG:NSPOFF; the module is
/ segmented code, so it is entered with the segmented bit set and needs
/ r14 to BE the TPA segment word while it runs; its parameter register
/ rr6 must be the segmented pointer the module reads, which for a
/ non-segmented caller is TPASEG:r7 (the same substitution notrsx makes
/ below); and its `ret' cannot put any of that back, so it returns into
/ a trampoline instead.
/
/ The frame built below the caller's stack pointer, low address first:
/
/	+0  TPASEG	the module's segmented `ret' address ...
/	+2  TSP+4	  ... which is the word after it
/	+4  sc 254	the one instruction: back into rsxback
/	+6  caller r14	  the four words rsxback puts back
/	+8  caller r6
/	+10 caller FCW
/	+12 caller PC
/
/ Fourteen bytes of the caller's stack, which has DEFSTACK (pgmld.c) of
/ headroom above the base page, plus whatever the module itself uses.
/ The trampoline is code on the stack because the stack is in the TPA --
/ the one segment every module and every transient shares -- so there is
/ nowhere else a Normal-mode `ret' could land that would not have to be
/ allocated and accounted for.
/
rsxgon:
	ldctl	r1, NSPOFF
	sub	r1, $14
	ldctl	NSPOFF, r1
	ld	r4, $[TPASEG*256]	/ NOT NSPSEG: see above
	ld	r5, r1
	ld	r2, $[TPASEG*256]
	ld	(rr4), r2		/ +0 return segment
	ld	r2, r1
	add	r2, $4
	ld	rr4(2), r2		/ +2 return offset: the `sc' at +4
	ld	r2, $0x7FFE
	ld	rr4(4), r2		/ +4 sc 254
	ldctl	r2, NSPSEG
	ld	rr4(6), r2		/ +6 the caller's own r14
	ld	r2, rr14(12)
	ld	rr4(8), r2		/ +8 the caller's own r6
	ld	r2, rr14(30)
	ld	rr4(10), r2		/ +10 the caller's own FCW
	ld	rr4(12), r3		/ +12 the caller's return offset
	or	r2, $0x8000		/ the module runs segmented
	ld	rr14(30), r2
	ld	r2, $[TPASEG*256]
	ldctl	NSPSEG, r2		/ ... on a segmented stack pointer
	ld	rr14(12), r2		/ ... with a segmented rr6
	ld	r4, r2			/ resume at r0's entry, as above
	ld	r5, r0
	ld	r3, rr4(6)
	ld	rr14(34), r3
	jr	scret

/
/ SC #254: the return trampoline above, reached by the module's `ret'.
/ The module's exit registers are in the frame -- r7 is the result it
/ answers with -- and NSPOFF is TSP+4, because the `ret' popped the four
/ bytes at +0.  Everything the caller owned and this path borrowed comes
/ back from the words at +6..+12, and the caller resumes at the
/ instruction after its own `sc 2' in its own mode, having seen nothing
/ but a BDOS call that answered.
/
rsxback:
	ldctl	r1, NSPOFF		/ = TSP+4
	ld	r4, $[TPASEG*256]
	ld	r5, r1
	ld	r2, rr4(2)		/ +6 the caller's r14
	ldctl	NSPSEG, r2
	ld	r2, rr4(4)		/ +8 the caller's r6
	ld	rr14(12), r2
	ld	r2, rr4(6)		/ +10 the caller's FCW
	ld	rr14(30), r2
	ld	r2, rr4(8)		/ +12 the caller's PC offset
	ld	rr14(34), r2
	ld	r2, $[TPASEG*256]
	ld	rr14(32), r2		/ its segment: the gate checked it
	add	r1, $10			/ drop the trampoline
	ldctl	NSPOFF, r1
	jp	scret

/
/ direct BIOS call function (BDOS fn 50): rr6 points at a 5-word block
/ {code, P1seg, P1off, P2seg, P2off} in caller memory; marshal the call
/ and return the LONG result in the caller's rr6.
/
/ The call goes to bioscl_ (sys/iosys.c), not to bios_: this is a gate
/ for USER programs, and which BIOS functions a user program may reach
/ is policy, written down in one place.  bioscl_ answers 0FFFFFFFFh for
/ a code it refuses.  The system's own BDOS->BIOS traffic is direct-
/ linked (D3) and the SC #3 gate below is the raw one for stock DRI
/ binaries, so neither of those pays for this.
/
bioscall:
	ldl	rr2, rr6		/ block address
	ld	r0, rr14(30)		/ caller's FCW
	bit	r0, $15
	jr	nz, callBios		/   segmented
	bit	r0, $14
	jr	nz, callBios		/   system
	ld	r2, rr14(32)		/   user nonseg: block seg from PC
	ld	r0, spflag_		/ split caller: block below the user
	test	r0			/   SP lives in the data bank
	jr	z, 2f
	ldctl	r0, NSPOFF
	cp	r3, r0
	jr	uge, 2f
	ld	r2, $[SPLITDSEG*256]
2:	ldm	r3, (rr2), $5		/ code, P1seg, P1off, P2seg, P2off
	ld	r4, rr14(32)		/   P1 seg := caller PC seg
	ld	r6, rr14(32)		/   P2 seg := caller PC seg
	ld	r0, spflag_		/ split caller: map P1/P2 by their
	test	r0			/   offsets vs the user SP too
	jr	z, dobios
	ldctl	r0, NSPOFF
	cp	r5, r0
	jr	uge, 3f
	ld	r4, $[SPLITDSEG*256]
3:	cp	r7, r0
	jr	uge, dobios
	ld	r6, $[SPLITDSEG*256]
	jr	dobios
callBios:
	ldm	r3, (rr2), $5		/ get parameters
dobios:
	pushl	(rr14), rr6		/ P2
	pushl	(rr14), rr4		/ P1
	push	(rr14), r3		/ code
	call	bioscl_
	add	r15, $10
	ldl	rr14(12), rr0		/ LONG result -> caller rr6
	jr	scret

/ SC #3: the BIOS gate -- stock binaries' _bios traps this even though
/ the system's own BDOS->BIOS traffic is direct-linked (D3)
biosgate:
	ld	r0, rr14(30)		/ caller's FCW
	bit	r0, $15
	jr	nz, 1f			/   segmented
	bit	r0, $14
	jr	nz, 1f			/   system
	ld	r4, rr14(32)		/   user nonseg: P1/P2 seg from PC
	ld	r6, rr14(32)
	ld	r0, spflag_		/ split caller: map P1/P2 by their
	test	r0			/   offsets vs the user SP
	jr	z, 1f
	ldctl	r0, NSPOFF
	cp	r5, r0
	jr	uge, 4f
	ld	r4, $[SPLITDSEG*256]
4:	cp	r7, r0
	jr	uge, 1f
	ld	r6, $[SPLITDSEG*256]
1:	pushl	(rr14), rr6		/ P2
	pushl	(rr14), rr4		/ P1
	push	(rr14), r3		/ code
	call	bios_
	add	r15, $10
	ldl	rr14(12), rr0		/ LONG result -> caller rr6
	jr	scret

/
/ Set supervisor mode procedure -- VERY DANGEROUS
/
/	The caller's saved FCW is set to SYSTEM, SEGMENTED; IRET arms it.
/	Interrupt status will be that at the time of the call.
/
setsup:
	ld	r0, rr14(30)
	set	r0, $14		/ set system
	set	r0, $15		/     and segmented
	ld	rr14(30), r0	/   in the saved caller FCW
	jr	scret

/ ***************************************************
/ 
/  BIOS Interface Routines		== LIVE M3 CODE ==
/ 
/   Note - there are 6 BIOS entry points from the BDOS,
/ 	labelled BIOS1 - BIOS6, depending on the
/ 	parameters passed.
/ 
/   D3: each re-marshals its stack arguments and makes a DIRECT
/   call  bios_(func, parm1, parm2)   -- func WORD, parms LONG --
/   instead of the original register-loaded SC #3.  Word parms are
/   zero-extended to LONG; missing parms are passed as 0L.  The
/   bios_ return value comes back in rr0/r1 per the C ABI and is
/   simply left there for our C callers.
/ 
/ ***************************************************

bios5_:
/ For BIOS functions sectran and set exception vector
/ (funct, word, long)	stack offsets 4, 6, 8
	ld	r2, rr14(4)	/ function number
	ld	r3, rr14(6)	/ 1st param (word)
	ldl	rr4, rr14(8)	/ 2nd param (long)
	pushl	(rr14), rr4	/ parm2
	sub	r0, r0
	ld	r1, r3
	pushl	(rr14), rr0	/ parm1 = (long)word
	push	(rr14), r2	/ func
	call	bios_
	add	r15, $10
	ret

bios4_:
/ For BIOS function seldsk
/ (func, word, word)	stack offsets 4, 6, 8
	ld	r2, rr14(4)	/ function number
	ld	r3, rr14(6)	/ 1st param (word)
	ld	r4, rr14(8)	/ 2nd param (word)
	sub	r0, r0
	ld	r1, r4
	pushl	(rr14), rr0	/ parm2 = (long)word
	ld	r1, r3
	pushl	(rr14), rr0	/ parm1 = (long)word
	push	(rr14), r2	/ func
	call	bios_
	add	r15, $10
	ret

bios3_:
/ For BIOS function set dma
/ (func, long)		stack offsets 4, 6
	ld	r2, rr14(4)	/ function number
	ldl	rr0, rr14(6)	/ 1st param (long)
	subl	rr4, rr4
	pushl	(rr14), rr4	/ parm2 = 0L
	pushl	(rr14), rr0	/ parm1
	push	(rr14), r2	/ func
	call	bios_
	add	r15, $10
	ret

bios2_:
/ For all BIOS functions with a word parameter
/ (func, word)		stack offsets 4, 6
	ld	r2, rr14(4)	/ function number
	ld	r3, rr14(6)	/ 1st param (word)
	subl	rr0, rr0
	pushl	(rr14), rr0	/ parm2 = 0L
	ld	r1, r3
	pushl	(rr14), rr0	/ parm1 = (long)word
	push	(rr14), r2	/ func
	call	bios_
	add	r15, $10
	ret

bios6_:
bios1_:
/ For all BIOS functions that have no parameter
/ other than function number	stack offset 4
	ld	r2, rr14(4)	/ function number
	subl	rr0, rr0
	pushl	(rr14), rr0	/ parm2 = 0L
	pushl	(rr14), rr0	/ parm1 = 0L
	push	(rr14), r2	/ func
	call	bios_
	add	r15, $10
	ret

/ ***************************************************
/ 
/   Utility Subroutines
/ 
/   swap(word)		swap bytes of a word
/ 
/   uword udiv((long)   dividend,
/ 	       (uword)  divisor,
/ 	       (uword *)rem    )
/ 
/   C900: stack offsets rebased for the 4-byte segmented return
/   address (args at +4); registers moved off r6/r7 (callee-saved
/   here, and r1/rr0 are the return registers now).
/ 
/ ***************************************************

swap_:
	ld	r1, rr14(4)
	exb	rh1, rl1
	ret

udiv_:
	ldl	rr2, rr14(4)	/ long dividend (low half of quad rq0)
	subl	rr0, rr0	/   as unsigned quad
	ld	r5, rr14(8)	/ word divisor
	clr	r4		/   as unsigned long
	divl	rq0, rr4	/ quotient -> rr2, remainder -> rr0
	ldl	rr4, rr14(10)	/ -> remainder result
	ld	(rr4), r1	/ store remainder (low word of rr0)
	ld	r1, r3		/ return quotient (word, in r1)
	ret
