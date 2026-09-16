/ crt0.s -- runtime startup for MWC-built CP/M-8000 transient programs.
/
/ MUST be the first object on the link line (ld -R 0x32000000 -e start):
/ pgmld enters a segmented (0xEE01) program at the first text byte, and
/ go.c's initial "return address" (sstack.stwo) is pgldaddr+2, so text
/ offset 2 must hold a warm-boot stub.
/
/ Entry state (pgmld.c + go.c segmented path): segmented Normal mode,

	.globl	start
	.globl	_cstart_
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
	jr	eq, nobss
	sub	r1, r1
1:
	ldb	(rr4), rl1
	inc	r5, $1
	djnz	r0, 1b
nobss:
	pushl	(rr14), rr2	/ _cstart(basepage): saves _base, builds
	sub	r13, r13	/   argc/argv from the tail, calls main
	call	_cstart_
	ldk	r5, $0		/ main returned: warm boot
	sc	2
