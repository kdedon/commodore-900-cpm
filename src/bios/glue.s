/ glue.s -- C-callable machine helpers for the CP/M-8000 BIOS.
/
/ Convention (matches crt.s): segmented call pushes a 4-byte return address,
/ so the first argument is at rr14(4); int args are one word, long/pointer
/ args two.  int returns in r1, long/pointer returns in rr0.

	.globl	inb_, outb_, outw_
	.globl	mem_cpy_, mem_clr_, xfer_
	.globl	ccpentry_
	.globl	ccprun_
#ifdef BOOT_TRACE
	.globl	bt4_			/ opt-in cold-boot marker (cmain.c)
#endif
	.shri

ccpentry_:
	ld	r14, $0x3f00		/ SP segment = 0x3F (<<8 form, VKERN)
	ld	r15, sysstk_		/ SP offset: THIS process's stack top,
					/   grows down (proc.h PSTKOF)
	sub	r13, r13		/ clear frame pointer
#ifdef BOOT_TRACE
	call	bt4_			/ marker <4>: stack reset (cmain.c)
#endif
	call	ccprun_
	jr	ccpentry_		/ ccprun does not return; if it ever
					/   did, start over rather than run on

/ int inb(port) -- read one byte from a standard-I/O port.
inb_:
	ld	r1, rr14(4)
	inb	rl1, (r1)
	subb	rh1, rh1
	ret

/ outb(port, val) -- write one byte to a standard-I/O port.
outb_:
	ld	r1, rr14(4)
	ld	r0, rr14(6)
	outb	(r1), rl0
	ret

/ outw(port, val) -- write one word to a standard-I/O port.
outw_:
	ld	r1, rr14(4)
	ld	r0, rr14(6)
	out	(r1), r0
	ret

/ mem_cpy(src, dst, len)  XADDR src, dst; long len;
/ Far-to-far byte copy.  An XADDR is the CPU register far-pointer form
/ (seg<<24)|offset, which is exactly what an rr address pair holds, so the
/ arguments load straight into the pairs.  ldirb increments offsets only:
/ a copy must not cross a segment end; callers keep len <= 64K-1 (the low
/ word of len is used; 0 copies nothing).
mem_cpy_:
	ldl	rr2, rr14(4)		/ src
	ldl	rr4, rr14(8)		/ dst
	ld	r0, rr14(14)		/ len, low word (r0: caller-scratch --
					/ r6..r12 are callee-saved and ldirb
					/ counts its counter register down)
	test	r0
	jr	eq, 1f
	ldirb	@rr4, @rr2, r0
1:
	ret

/ mem_clr(dst, len)  XADDR dst; long len;
/ Far zero fill: len bytes (even; low word of len used; 0 clears nothing).
/ Offsets increment only, so the region must not cross a segment end.
/ Clears the first word, then a word ldir whose read pointer trails the
/ write pointer by one word propagates the zero across the region.
mem_clr_:
	ldl	rr2, rr14(4)		/ dst = read pointer
	ld	r0, rr14(10)		/ len, low word, bytes
	srl	r0, $1			/ word count
	jr	z, 1f
	clr	r1
	ld	@rr2, r1		/ first word = 0
	sub	r0, $1
	jr	z, 1f			/ one word only
	ldl	rr4, rr2
	add	r5, $2			/ write pointer = dst + 2
	ldir	@rr4, @rr2, r0		/ propagate the zero forward
1:
	ret

xfer_:
	di	VI
	ldl	rr2, rr14(4)		/ far pointer to the context block
	ld	r0, rr2(28)		/ regs[14] -> user r14
	ldctl	NSPSEG, r0
	ld	r0, rr2(30)		/ regs[15] -> user initial SP
	ldctl	NSPOFF, r0
	ld	r0, rr2(38)		/ PC offset
	push	(rr14), r0
	ld	r0, rr2(36)		/ PC segment word (XADDR high = 0xSS00)
	push	(rr14), r0
	and	r0, $0xB7FF		/ NVI is unsupported; clear NVIE -- and
					/   clear FCW_SN (0x4000) as well, so a
					/   launch is ALWAYS into Normal mode,
					/   which is this routine's documented
					/   contract (see the IRET below).  A
					/   context whose FCW word carried the
					/   System bit used to be launched in
					/   System mode, and segment 0x3F -- every
					/   process's supervisor stack -- is
					/   mapped there: one bad context handed
					/   in by a user program then wrote over
					/   all of them.  Every legitimate
					/   launcher already passes SN clear (the
					/   CCP's own launch context carries FCW
					/   0x9800), so this costs them nothing.
	push	(rr14), r0
	push	(rr14), r0		/ junk identifier word
	ldm	r0, (rr2), $14		/ user r0-r13 (LDM latches the address
					/   before r2/r3 are overwritten)
	iret				/ -> nonseg Normal mode in the TPA
