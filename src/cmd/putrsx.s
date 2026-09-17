/ Portions Copyright (c) 2026 Kevin Dedon.

/ ***************************************************************
/
/	putrsx.s -- PUT.RSX, console output copied into a file.
/
/	The other half of getrsx.s, and everything said in that file's
/	header about the prefix, the SC #2 gate, `sc 2' as the
/	pass-down, the fixed link address and the caller's DMA applies
/	here unchanged.  What is different is the direction and the
/	list of functions.
/
/	WHAT IT INTERCEPTS.  v3's PUT.RSX takes function 2 and function
/	9 (ref/cpm3/putrsx.asm).  Ours must take function 111 as well,
/	and that is not a liberty: this tree's cputs() sends a whole
/	string through function 111 and only conputs() goes character
/	by character through function 2 (src/cmd/libcpm.c says why).
/	A module that claimed function 2 alone would see almost none of
/	the output on this system.
/
/	ECHO.  With echoing on -- v3's default -- the call is passed
/	down as well, so the text reaches the console too.  With it off
/	the module answers the call itself and the console stays quiet.
/
/	THE FILE.  PUT.Z8K creates it, empty, before it attaches this
/	module, so that `no directory space' is reported by a command
/	and not from inside an intercept.  The module opens it for
/	itself on the first record it has to write, for getrsx.s's
/	reason: no open FCB then has to survive the program load in
/	between.  Records are written as the 128-byte buffer fills; the
/	last, partial one is padded with 1Ah and the file is closed when
/	`PUT CONSOLE OUTPUT TO CONSOLE' calls sub-function 133.  Output
/	that has not filled a record when a session ends without that
/	command is lost -- v3 has the same hole, and its manual says to
/	end a PUT before turning the machine off.
/
/	FIXED FIELDS.  PUT.Z8K patches these before the attach:
/
/	  20  pecho	nonzero = the console sees the output too
/	  22  pfcb	the 36-byte FCB of the file to write
/
/ ***************************************************************

#ifndef	PUTORG
#define	PUTORG	0xF200
#endif

/ The TPA segment word: c900cfg.h TPASEG, the Makefile's UBASE.
#define	PSEG	0x3200

#define	POFF(l)	[l-putbase+PUTORG]

	.shri

	.globl	putbase

/ ***** the prefix (src/bdos/rsxhdr.h; ref/cpm3/getrsx.asm:124-137) *****

putbase:
	.word	0, 0, 0			/ 00 serial, filled in on attach
	.word	entry-putbase+PUTORG	/ 06 entry
	.word	0x5253			/ 08 magic 'RS'
	.word	0			/ 0A next, filled in on attach
	.word	0			/ 0C prev, filled in on attach
pwarm:
	.byte	0, 0			/ 0E warm-boot flag, bank flag
	.byte	0x50, 0x55, 0x54, 0x20	/ 10 name: PUT
	.byte	0x20, 0x20, 0x20, 0x20
	.byte	0, 0, 0, 0		/ 18 endchain, rsvd, patch, rsvd
	.word	PUTORG			/ 1C org
	.word	putend-putbase		/ 1E len

/ ***** the two fields PUT.Z8K patches, at fixed offsets *****

pecho:	.byte	1			/ 20 the console sees it too
	.byte	0			/ 21
pfcb:	.blkb	36			/ 22 the file to write

/ ***** the intercept *****

entry:
	sub	r15, $16
	ldm	(rr14), r0, $8		/ r0..r7 at rr14(0..14)
	ld	r0, pdead
	test	r0
	jr	nz, down		/ closed, or the disk said no
	cp	r5, $2
	jr	eq, dofn2
	cp	r5, $9
	jr	eq, dofn9
	cp	r5, $111
	jr	eq, dofn111
	cp	r5, $60
	jr	eq, dofn60

down:
	ldm	r0, (rr14), $8
	add	r15, $16
	sc	2
	ret

retv:
	ld	rr14(14), r1
	ldm	r0, (rr14), $8
	add	r15, $16
	ret

/ The call has been copied to the file.  Echoing decides whether the
/ console gets it as well, which is the whole of v3's [ECHO] option.
dpass:
	ldb	rl0, pecho
	cpb	rl0, $0
	jr	ne, down
	clr	r1
	jr	retv

/ ----- function 2: one character -----
dofn2:
	ld	r1, rr14(14)
	and	r1, $0xFF
	call	putch
	jr	dpass

/ ----- function 9: a string, up to the '$' -----
dofn9:
	ldl	rr2, rr14(12)
p9lp:
	clr	r1
	ldb	rl1, (rr2)
	cp	r1, $0x24
	jr	eq, dpass
	pushl	(rr14), rr2
	call	putch
	popl	rr2, (rr14)
	inc	r3, $1
	jr	p9lp

/ ----- function 111: a counted block (src/cmd/cpm.h struct ccb) -----
dofn111:
	ldl	rr2, rr14(12)
	ldl	rr4, (rr2)		/ cbaddr
	ldl	pblkp, rr4
	ld	r4, rr2(4)		/ cblen
	ld	pblkn, r4
p111lp:
	ld	r4, pblkn
	test	r4
	jr	z, dpass
	dec	r4, $1
	ld	pblkn, r4
	ldl	rr2, pblkp
	clr	r1
	ldb	rl1, (rr2)
	inc	r3, $1
	ldl	pblkp, rr2
	call	putch
	jr	p111lp

/ ----- function 60: v3's PUT sub-functions -----
/ 133 = stop and close (putrsx.asm pckillf), 134 = report the FCB.
dofn60:
	ldl	rr2, rr14(12)
	ldb	rl0, (rr2)
	cpb	rl0, $133
	jr	eq, pkill
	cpb	rl0, $134
	jr	eq, pfcbq
	jr	down
pkill:
	call	pflush
	ld	r5, $16			/ close
	ld	r6, $PSEG
	ld	r7, $POFF(pfcb)
	sc	2
	ld	r1, r7
	call	pstop
	jr	retv
pfcbq:
	ld	r1, $POFF(pfcb)
	jr	retv

/ ***** the file *****

/ pstop() -- write nothing more, and go at the next warm boot, which is
/ the exit of the PUT command that called this.
pstop:
	ld	r0, $0xFFFF
	ld	pdead, r0
	ldb	rl0, $0xFF
	ldb	pwarm, rl0
	ret

/ putch() -- add r1 to the buffer, writing a record when it is full.
putch:
	ld	r0, ppos
	ld	r2, $PSEG
	ld	r3, $POFF(pbuf)
	add	r3, r0
	ldb	(rr2), rl1
	inc	r0, $1
	ld	ppos, r0
	cp	r0, $128
	jr	ult, pcx
	call	pflush
pcx:
	ret

/ pflush() -- one record out.  A partial record is padded with 1Ah, as
/ every CP/M text file's last record is.
pflush:
	ld	r0, ppos
	test	r0
	jr	z, pfx
	ld	r2, $PSEG
	ld	r3, $POFF(pbuf)
	add	r3, r0
	ldb	rl1, $0x1A
ppad:
	cp	r0, $128
	jr	uge, pfdma
	ldb	(rr2), rl1
	inc	r3, $1
	inc	r0, $1
	jr	ppad
pfdma:
	ld	r5, $49			/ @CRDMA (scb.h SCB_CRDMA)
	ld	r6, $PSEG
	ld	r7, $POFF(ppb)
	sc	2
	ld	psave, r7
	ld	r5, $26
	ld	r6, $PSEG
	ld	r7, $POFF(pbuf)
	sc	2
	ld	r0, popen
	test	r0
	jr	nz, pfwrit
	ld	r5, $15			/ open the file PUT.Z8K made
	ld	r6, $PSEG
	ld	r7, $POFF(pfcb)
	sc	2
	cp	r7, $255
	jr	eq, pfbad
	ld	r0, $1
	ld	popen, r0
pfwrit:
	ld	r5, $21			/ write sequential
	ld	r6, $PSEG
	ld	r7, $POFF(pfcb)
	sc	2
	test	r7
	jr	z, pfok
pfbad:
	call	pstop			/ full, or read-only: stop quietly
					/   rather than fail the caller's
					/   console call
pfok:
	ld	r5, $26			/ the caller's DMA, back again
	ld	r6, $PSEG
	ld	r7, psave
	sc	2
	clr	r0
	ld	ppos, r0
pfx:
	ret

/ ***** state *****

ppb:	.byte	0x3C, 0, 0, 0		/ the function-49 parameter block
psave:	.word	0			/ the caller's DMA offset
popen:	.word	0			/ the file has been opened
pdead:	.word	0			/ closed: pass everything down
ppos:	.word	0			/ bytes of pbuf in use
pblkp:	.long	0			/ function 111: where we are in the
pblkn:	.word	0			/   caller's block, and what is left
pbuf:	.blkb	128
putend:
