/ Copyright (c) 2026 Kevin Dedon.
/ SPDX-License-Identifier: MIT

/ prsx.s -- PROT.RSX refuses deletes and counts successful/failed opens after
/ passing them down the chain. Function 60/201 reports the three counts
/ as hexadecimal nibbles. Entry uses the UCASE.RSX calling convention.

#ifndef	PROTORG
#define	PROTORG	0xE800
#endif

/ The bank flag (prefix offset 0Fh).  GENCOM reads it and, when it is
/ non-zero, forces the module's warm-boot flag to 0FFh so the module
/ cannot outlive the program it was bound to (gencom.plm:1186-1189).
/ user/prsxn.s is this module with it set, which is the only way a
/ GENCOM'd module becomes temporary -- GENCOM has no option for it.
#ifndef	NBANKF
#define	NBANKF	0
#endif

	.shri

	.globl	protbase

/ ***** the prefix (sys/rsxhdr.h; ref/cpm3/getrsx.asm:124-137) *****

protbase:
	.word	0, 0, 0			/ 00 serial, filled in on attach
	.word	entry-protbase+PROTORG	/ 06 entry
	.word	0x5253			/ 08 magic 'RS'
	.word	0			/ 0A next, filled in on attach
	.word	0			/ 0C prev, filled in on attach
	.byte	0, NBANKF		/ 0E warm-boot flag, bank flag
	.byte	0x50, 0x52, 0x4F, 0x54	/ 10 name: PROT
	.byte	0x20, 0x20, 0x20, 0x20
	.byte	0, 0, 0, 0		/ 18 endchain, rsvd, patch, rsvd
	.word	PROTORG			/ 1C org
	.word	protend-protbase	/ 1E len

/ ***** the intercept *****

entry:
	push	(rr14), r0
	push	(rr14), r1
	pushl	(rr14), rr2
	cp	r5, $19			/ delete file: ours, and it stops here
	jr	eq, refuse
	cp	r5, $15			/ open file: down first, then look
	jr	eq, openit
	cp	r5, $60
	jr	eq, fn60

down:					/ pass it down the chain
	popl	rr2, (rr14)
	pop	r1, (rr14)
	pop	r0, (rr14)
	sc	2
	ret

refuse:					/ the BDOS never sees this call
	ld	r0, ndel
	inc	r0, $1
	ld	ndel, r0
	popl	rr2, (rr14)
	pop	r1, (rr14)
	pop	r0, (rr14)
	ld	r7, $255		/ 0FFh: what a failed open/delete says
	ret

openit:					/ post-processing: the call goes down
	popl	rr2, (rr14)		/   and comes back here with the
	pop	r1, (rr14)		/   BDOS's own answer in r7
	pop	r0, (rr14)
	sc	2
	push	(rr14), r0
	cp	r7, $255
	jr	eq, ofail
	ld	r0, nopen
	inc	r0, $1
	ld	nopen, r0
	pop	r0, (rr14)
	ret
ofail:
	ld	r0, nfail
	inc	r0, $1
	ld	nfail, r0
	pop	r0, (rr14)
	ret

fn60:					/ rr6 -> the RSX parameter block
	ldl	rr2, rr6
	ldb	rl0, (rr2)		/ sub-function number
	cpb	rl0, $0xC9		/ 201: report what each path did
	jr	ne, down
	popl	rr2, (rr14)
	pop	r1, (rr14)
	pop	r0, (rr14)
	ld	r7, nopen		/ 0x0<opens><failures><deletes>
	add	r7, r7
	add	r7, r7
	add	r7, r7
	add	r7, r7
	add	r7, nfail
	add	r7, r7
	add	r7, r7
	add	r7, r7
	add	r7, r7
	add	r7, ndel
	ret

nopen:
	.word	0
nfail:
	.word	0
ndel:
	.word	0
protend:
