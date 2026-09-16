#ifndef	RSXORG
#define	RSXORG	0xF000
#endif

/ The name and the function-60 sub-function are overridable for the same
/ reason: a chain of three needs a module the call has to travel PAST to
/ reach, and two copies answering the same sub-function cannot show that.
/ user/ucrsx3.s is this module under a name of its own, claiming a
/ sub-function of its own, so that 0C8h below has to cross it.
#ifndef	RSXSUB
#define	RSXSUB	0xC8
#endif
#ifndef	RSXNAM1
#define	RSXNAM1	0x55, 0x43, 0x41, 0x53	/ UCAS
#define	RSXNAM2	0x45, 0x20, 0x20, 0x20	/ E
#endif

	.shri

	.globl	rsxbase

/ ***** the prefix (sys/rsxhdr.h; ref/cpm3/getrsx.asm:124-137) *****

rsxbase:
	.word	0, 0, 0			/ 00 serial, filled in on attach
	.word	entry-rsxbase+RSXORG	/ 06 entry
	.word	0x5253			/ 08 magic 'RS'
	.word	0			/ 0A next, filled in on attach
	.word	0			/ 0C prev, filled in on attach
	.byte	0, 0			/ 0E warm-boot flag, bank flag
	.byte	RSXNAM1			/ 10 name
	.byte	RSXNAM2
	.byte	0, 0, 0, 0		/ 18 endchain, rsvd, patch, rsvd
	.word	RSXORG			/ 1C org
	.word	rsxend-rsxbase		/ 1E len

/ ***** the intercept *****

entry:
	push	(rr14), r0
	push	(rr14), r1
	pushl	(rr14), rr2
	ld	r0, ncalls		/ count every call we are shown
	inc	r0, $1
	ld	ncalls, r0
	cp	r5, $2
	jr	eq, upcase
	cp	r5, $60
	jr	eq, fn60

down:					/ pass it down the chain
	popl	rr2, (rr14)
	pop	r1, (rr14)
	pop	r0, (rr14)
	sc	2
	ret

upcase:					/ fn 2: the character is in rl7
	cpb	rl7, $0x61
	jr	ult, down
	cpb	rl7, $0x7A
	jr	ugt, down
	andb	rl7, $0xDF
	jr	down

fn60:					/ rr6 -> the RSX parameter block
	ldl	rr2, rr6
	ldb	rl0, (rr2)		/ sub-function number
	cpb	rl0, $RSXSUB		/ 0C8h by default: the call count
	jr	ne, down
	popl	rr2, (rr14)
	pop	r1, (rr14)
	pop	r0, (rr14)
	ld	r7, ncalls
	ret

ncalls:
	.word	0
rsxend:
