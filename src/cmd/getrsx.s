/ Copyright (c) 2026 Kevin Dedon.
/ SPDX-License-Identifier: MIT

/ ***************************************************************
/
/	getrsx.s -- GET.RSX, console input served from a file.
/
/	This is v3's GET.RSX (ref/cpm3/getrsx.asm) in the shape the
/	Z8001 RSX mechanism gives us: a prefix (src/bdos/rsxhdr.h),
/	entry from the SC #2 gate (src/bdos/bdosglue.s rsxenter) in
/	the caller's mode on the caller's stack with r5 = the BDOS
/	function and rr6 = the parameter, `sc 2' to pass a call down
/	the chain, `ret' to answer it with the result in r7.
/
/	WHAT IT INTERCEPTS.  v3's list, from getrsx.asm:194-214:
/	function 1 (console input), 6 (raw console I/O), 10 (read
/	console buffer) and 11 (console status).  Everything else --
/	and everything at all once the file is exhausted -- is passed
/	down untouched.
/
/	WHY THE CCP FEELS IT.  On the 8080 GET works on the command
/	line because location 5 is the RSX and the CCP calls it like
/	anything else.  Here the CCP is a TRANSIENT (src/ccp/ccprun.c)
/	and reaches the BDOS through the same `sc 2' as any program,
/	so it goes through the chain too -- which is what makes
/	`GET FILE cmds.txt' able to feed commands to A>.  src/bdos/rsx.c's
/	header records that; CPM3-UTILITIES.md used to deny it.
/
/	THE CALLER'S DMA.  Reading a record moves the DMA address, and
/	the program whose console call we are serving owns it.  So each
/	refill reads @CRDMA out of the SCB (offset 3Ch) with function
/	49, points the DMA at our own buffer, reads, and puts the
/	caller's address back with function 26 as TPASEG:offset -- every
/	transient and every module lives in the one TPA segment.  v3
/	instead intercepts function 26 and remembers what went past
/	(getrsx.asm:207-212); reading the SCB needs no such shadow and
/	cannot be wrong about a DMA that was set before we attached.
/
/	OUR OWN BDOS CALLS.  A module's `sc 2' is routed by the gate to
/	the module ABOVE it, never to itself, so the opens and reads
/	below cannot recurse into this code.  That is the property
/	verify-rsx2 proves.
/
/	FIXED ADDRESS, FIXED FIELDS.  There is no relocator
/	(src/bdos/rsxhdr.h note 3): GETORG here, the link address in
/	the Makefile and the `org' word in the prefix must agree, and
/	tools/mkrsx.py fails the build when they do not.  GET.Z8K
/	patches two things into the image before it attaches it, at
/	offsets it knows because they are fixed here, immediately after
/	the 32-byte prefix:
/
/	  20  gecho	nonzero = echo what is read to the console
/	  22  gfcb	the 36-byte FCB of the file to read
/
/ ***************************************************************

#ifndef	GETORG
#define	GETORG	0xE400
#endif

/ The TPA segment word, c900cfg.h TPASEG and the Makefile's UBASE.  A
/ module has no include path (the .s rule is a bare cpp), and every
/ address this module hands the BDOS is in that one segment.
#define	GSEG	0x3200

#define	GOFF(l)	[l-rsxbase+GETORG]

	.shri

	.globl	rsxbase

/ ***** the prefix (src/bdos/rsxhdr.h; ref/cpm3/getrsx.asm:124-137) *****

rsxbase:
	.word	0, 0, 0			/ 00 serial, filled in on attach
	.word	entry-rsxbase+GETORG	/ 06 entry
	.word	0x5253			/ 08 magic 'RS'
	.word	0			/ 0A next, filled in on attach
	.word	0			/ 0C prev, filled in on attach
gwarm:
	.byte	0, 0			/ 0E warm-boot flag, bank flag
	.byte	0x47, 0x45, 0x54, 0x20	/ 10 name: GET
	.byte	0x20, 0x20, 0x20, 0x20
	.byte	0, 0, 0, 0		/ 18 endchain, rsvd, patch, rsvd
	.word	GETORG			/ 1C org
	.word	rsxend-rsxbase		/ 1E len

/ ***** the two fields GET.Z8K patches, at fixed offsets *****

gecho:	.byte	1			/ 20 echo the file to the console
	.byte	0			/ 21
gfcb:	.blkb	36			/ 22 the file to read

/ ***** the intercept *****

entry:
	sub	r15, $16
	ldm	(rr14), r0, $8		/ r0..r7 at rr14(0..14)
	ld	r0, geof
	test	r0
	jr	nz, down		/ spent: this module is a no-op now
	cp	r5, $1
	jr	eq, dofn1
	cp	r5, $6
	jr	eq, dofn6
	cp	r5, $10
	jr	eq, dofn10
	cp	r5, $11
	jr	eq, dofn11
	cp	r5, $60
	jr	eq, dofn60

/ pass the call down the chain: the gate sends it to the module above
/ this one, or to the BDOS when there is none
down:
	ldm	r0, (rr14), $8
	add	r15, $16
	sc	2
	ret

/ answer the call with r1 as the result
retv:
	ld	rr14(14), r1
	ldm	r0, (rr14), $8
	add	r15, $16
	ret

/ ----- function 1: console input, echoed -----
dofn1:
	call	getch
	test	r0
	jr	z, down			/ end of file: the console again
	call	echo1
	jr	retv

/ ----- function 11: console status -----
/ There is a character while the file has one, and the top of `entry'
/ has already sent the spent case down.
dofn11:
	ld	r1, $0xFF
	jr	retv

/ ----- function 6: direct console I/O -----
/ 0FFh = read if ready, 0FDh = wait for a character, 0FEh = status;
/ anything else is console OUTPUT and is not ours.
dofn6:
	ld	r1, rr14(14)		/ the caller's rl7 subcode
	and	r1, $0xFF
	cp	r1, $0xFE
	jr	eq, dofn11
	cp	r1, $0xFF
	jr	eq, f6read
	cp	r1, $0xFD
	jr	ne, down
f6read:
	call	getch			/ raw: never echoed
	test	r0
	jr	z, down
	jr	retv

/ ----- function 10: read console buffer -----
/ rr6 -> {maxlen, retlen, chars...}.  A line ends at CR; a LF is
/ dropped, so a CR LF file yields one line per line.  The loop state is
/ in the module rather than on the stack because getch() below makes
/ BDOS calls of its own and may use every register.
dofn10:
	ldl	rr2, rr14(12)
	ldl	f10buf, rr2
	clr	r4
	ldb	rl4, (rr2)		/ the caller's buffer size
	ld	f10max, r4
	clr	r4
	ld	f10cnt, r4
f10lp:
	call	getch
	test	r0
	jr	z, f10end
	cp	r1, $0x0D
	jr	eq, f10cr
	cp	r1, $0x0A
	jr	eq, f10lp
	call	echo1
	ld	r4, f10cnt
	cp	r4, f10max
	jr	uge, f10lp		/ longer than the caller's buffer
	ldl	rr2, f10buf
	ld	r6, r2
	ld	r7, r3
	add	r7, r4
	add	r7, $2
	ldb	(rr6), rl1
	inc	r4, $1
	ld	f10cnt, r4
	jr	f10lp
f10cr:
	ld	r1, $0x0D		/ function 10 echoes the CR that
	call	echo1			/   ended the line (conbdos.c
f10end:					/   readline: conout(cr), break)
	ld	r4, f10cnt
	test	r4
	jr	nz, f10sto
	ld	r0, geof
	test	r0
	jr	nz, down		/ nothing left and nothing read:
f10sto:					/   let the console answer
	ldl	rr2, f10buf
	ld	r6, r2
	ld	r7, r3
	inc	r7, $1
	ldb	(rr6), rl4
	clr	r1
	jr	retv

/ ----- function 60: v3's GET sub-functions -----
/ 129 = stop (getrsx.asm gkillf), 130 = report the FCB (gfcbf).  The
/ numbers are v3's so that a program written against CP/M 3 finds them
/ where it expects.
dofn60:
	ldl	rr2, rr14(12)
	ldb	rl0, (rr2)
	cpb	rl0, $129
	jr	eq, gkill
	cpb	rl0, $130
	jr	eq, gfcbq
	jr	down
gkill:
	call	spent
	clr	r1
	jr	retv
gfcbq:
	ld	r1, $GOFF(gfcb)
	jr	retv

/ ***** the file *****

/ spent() -- nothing more will come from the file.  Every later call
/ goes straight down, and the module asks to be removed at the next warm
/ boot -- which is the exit of whatever program was reading it, exactly
/ as v3 removes GET.RSX when the file runs out (getrsx.asm's kill flag).
spent:
	ld	r0, $0xFFFF
	ld	geof, r0
	ldb	rl0, $0xFF
	ldb	gwarm, rl0
	ret

/ echo1() -- send r1 to the console if echoing is on, preserving r1.
echo1:
	ldb	rl0, gecho
	cpb	rl0, $0
	jr	eq, e1x
	push	(rr14), r1
	ld	r5, $2
	clr	r6
	ld	r7, r1
	sc	2
	pop	r1, (rr14)
e1x:
	ret

/ getch() -- r1 = the next character and r0 = 1, or r0 = 0 at the end of
/ the file.
/
/ The LF of a CR LF pair is not a character.  A console line ends at the
/ CR, and every reader in the tree stops there -- function 10 above, and
/ INITDIR's askchar() (src/cmd/initdir.c:108), which reads function 1
/ until a CR.  Leave the LF in the stream and it becomes the ANSWER to
/ the next question the file was supposed to answer.  So the pair is
/ folded here, once, where every one of the four intercepted functions
/ gets the benefit -- and a file written with bare LFs still works,
/ because only an LF that FOLLOWS a CR is dropped.
getch:
	call	gnext
	test	r0
	jr	z, gcx
	cp	r1, $0x0A
	jr	ne, gckeep
	ld	r0, glast
	cp	r0, $0x0D
	jr	ne, gckeep
	call	gnext
	test	r0
	jr	z, gcx
gckeep:
	ld	glast, r1
	ld	r0, $1
gcx:
	ret

/ gnext() -- one byte of the file, with no interpretation but the 1Ah
/ that ends any CP/M text file.
gnext:
	ld	r0, gpos
	cp	r0, gcnt
	jr	ult, gchave
	call	gfill
	test	r0
	jr	z, gceof
	ld	r0, gpos
gchave:
	ld	r2, $GSEG
	ld	r3, $GOFF(gbuf)
	add	r3, r0
	clr	r1
	ldb	rl1, (rr2)
	inc	r0, $1
	ld	gpos, r0
	cp	r1, $0x1A
	jr	eq, gceof
	ld	r0, $1
	ret
gceof:
	call	spent
	clr	r0
	ret

/ gfill() -- read the next record into gbuf.  r0 = 1 on success.
/ The caller's DMA address is read out of the SCB and put back
/ afterwards; the file is opened on the first refill, not by GET.Z8K,
/ so that no open FCB has to survive the program load in between.
gfill:
	ld	r5, $49			/ @CRDMA (scb.h SCB_CRDMA)
	ld	r6, $GSEG
	ld	r7, $GOFF(gpb)
	sc	2
	ld	gsave, r7
	ld	r5, $26
	ld	r6, $GSEG
	ld	r7, $GOFF(gbuf)
	sc	2
	ld	r0, gopen
	test	r0
	jr	nz, gfread
	ld	r5, $15			/ open
	ld	r6, $GSEG
	ld	r7, $GOFF(gfcb)
	sc	2
	cp	r7, $255
	jr	eq, gfbad
	ld	r0, $1
	ld	gopen, r0
gfread:
	ld	r5, $20			/ read sequential
	ld	r6, $GSEG
	ld	r7, $GOFF(gfcb)
	sc	2
	test	r7
	jr	nz, gfbad
	clr	r0
	ld	gpos, r0
	ld	r0, $128
	ld	gcnt, r0
	ld	r0, $1
	jr	gfdma
gfbad:
	clr	r0
gfdma:
	push	(rr14), r0
	ld	r5, $26			/ the caller's DMA, back again
	ld	r6, $GSEG
	ld	r7, gsave
	sc	2
	pop	r0, (rr14)
	ret

/ ***** state *****

gpb:	.byte	0x3C, 0, 0, 0		/ the function-49 parameter block
gsave:	.word	0			/ the caller's DMA offset
gopen:	.word	0			/ the file has been opened
geof:	.word	0			/ ... and has nothing more in it
gpos:	.word	0			/ next byte of gbuf
glast:	.word	0			/ the byte before it, for CR LF
gcnt:	.word	0			/ bytes of gbuf that are the file's
f10buf:	.long	0			/ function 10: the caller's buffer
f10max:	.word	0
f10cnt:	.word	0
gbuf:	.blkb	128
rsxend:
