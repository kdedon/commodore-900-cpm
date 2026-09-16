/ CPM.SYS entry -- runs where kboot's launch stub left us:
/   text  seg 0x30 -> phys 0x080000, attr 0x03 (read/execute)
/   data  seg 0x31 -> phys 0x080000+roundup(text,1K), attr 0x02, bss zeroed
/   ROM   seg 0 (code) + seg 1 (its RAM cells) still mapped and callable
/   mode  segmented system mode; nothing else mapped
/
/ The WD controller's command block (phys 0x080000) and DMA sector buffer
/ (phys 0x080400) sit UNDER our first 0x600 text bytes; disk DMA bypasses
/ the MMU, so the controller writes those physical bytes no matter how the
/ segment is mapped.  The pad below keeps real code/data out of that range
/ (same idiom as the Coherent kernel's cmdblk_ pad in md.s).

#include "c900cfg.h"

	.globl	start
	.globl	scentry_		/ SC trap gate (sys/bdosglue.s)
	.globl	tepa_, tprv_, tseg_, tnmi_, tnvi_, tvi_	/ fault stubs (trap.s)

/ The segment every compiled frame reference is relocated against: cc2 emits
/ each frame address's segment byte as a relocation adding SS, so the objects
/ linked into CPM.SYS have to say which segment the resident system's stack is
/ in.  Both bytes carry it, as the kernel's md.s spells it.
	.globl	SS
SS	=	0x3f3f

	.shri

cmdblk_:
	ldar	rr2, start		/ offset-0 safety: jump to the real entry
	jp	(rr2)
	.blkb	[3*512]+cmdblk_-.	/ pad text to 0x600: WD cmd block, ECC,
					/ sector buffer live underneath

/ Program Status Area.  Lands at text offset 0x600 (right after the pad),
/ which is 256-aligned as the PSAP requires.  Z8001 entries are 4 words:
/ reserved, FCW, PC-segment, PC-offset; `.long 0xC000' emits the reserved
/ word (0) plus the handler FCW (segmented system, all interrupts off) in
/ one directive.  Layout per the Z8000 CPU Technical Manual Fig 7-2 (same
/ idiom as the Coherent kernel's md.s psa).
psa:
	.word	0, 0, 0, 0		/ +0: reserved
	.long	0xC000			/ +8: extended-instruction (EPU) trap
	.long	tepa_
	.long	0xC000			/ +16: privileged-instruction trap
	.long	tprv_
	.long	scentry_
	.long	0xC000			/ +32: segment trap
	.long	tseg_
	.long	0xC000			/ +40: non-maskable interrupt
	.long	tnmi_
	.long	0xC000			/ +48: non-vectored interrupt
	.long	tnvi_
	.long	0xC000			/ +56: FCW for all vectored interrupts
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_

start:
	/ ===== PSAP: route traps to our PSA (before anything can fault) =====
	ldar	rr0, psa
	ldctl	PSAPSEG, r0
	ldctl	PSAPOFF, r1

	/ ===== MMU: TPA + buffers + C stack =====
	/ seg TPASEG -> phys 0x0A0000, attr 0x00 (normal AND system mode,
	/ read/write/execute): the 64K TPA that BIOS fn 18 (GMRTA) advertises.
	/ Loaded programs run in it non-segmented Normal mode, so it must NOT
	/ carry the SYS attribute; every other mapped segment keeps SYS, which
	/ fences the user program in.  (kboot's text staging used this
	/ physical page; it is dead once we are running.)
	ldb	rl0, $TPASEG
	soutb	0x01fc, rl0
	ldb	rl0, $TPAPHYSPAGE
	soutb	0x0ffc, rl0
	ldb	rl0, $0x00
	soutb	0x0ffc, rl0
	ldb	rl0, $0xff
	soutb	0x0ffc, rl0
	ldb	rl0, $0x00
	soutb	0x0ffc, rl0

	/ seg 0x33 -> phys 0x0C0000, attr 0x02 (SYS r/w): disk/deblock buffers
	/ (the kboot launch stub at 0x0C0000 is dead once we are here).
	ldb	rl0, $0x33
	soutb	0x01fc, rl0
	ldb	rl0, $0x0c
	soutb	0x0ffc, rl0
	ldb	rl0, $0x00
	soutb	0x0ffc, rl0
	ldb	rl0, $0xff
	soutb	0x0ffc, rl0
	ldb	rl0, $0x02
	soutb	0x0ffc, rl0

	/ seg SPLITMSEG -> phys 0x0F0000, attr 0x02 (SYS r/w, and SYS
	/ execute -- the attribute byte's only access bits are SYS and
	/ read-only): the relocated split-I/D module.  cmain copies the
	/ linked image here out of our own data segment.  The limit is
	/ SPLITMLIM, not the usual 0xff, so the segment stops below the
	/ ROM's segment-1 data cells at the top of a 512 KB machine's RAM
	/ (see c900cfg.h).
	ldb	rl0, $SPLITMSEG
	soutb	0x01fc, rl0
	ldb	rl0, $SPLITMPHYSPAGE
	soutb	0x0ffc, rl0
	ldb	rl0, $0x00
	soutb	0x0ffc, rl0
	ldb	rl0, $SPLITMLIM
	soutb	0x0ffc, rl0
	ldb	rl0, $0x02
	soutb	0x0ffc, rl0

	/ seg 0x3F -> phys 0x0D0000, attr 0x02: the C stack.  Frame/local refs
	/ carry the segment SS, defined above as 0x3F -- the same segment the
	/ ROM's own routines frame in, which is what makes calling back into
	/ the ROM safe.
	ldb	rl0, $0x3f
	soutb	0x01fc, rl0
	ldb	rl0, $0x0d
	soutb	0x0ffc, rl0
	ldb	rl0, $0x00
	soutb	0x0ffc, rl0
	ldb	rl0, $0xff
	soutb	0x0ffc, rl0
	ldb	rl0, $0x02
	soutb	0x0ffc, rl0
	ld	r14, $0x3f00		/ SP segment = seg 0x3F (<<8 form)
	ld	r15, $0xfc00		/ SP offset (top; grows down)
	sub	r13, r13		/ clear frame pointer
	call	cmain_
hang:
	halt
	jr	hang

/ mapseg_(seg, basepage, attr) -- program one Z8010 MMU descriptor from C.
/ basepage = phys>>8 (e.g. 0x0800 for phys 0x080000); attr: 0x02 = SYS r/w,
/ 0x03 = SYS read/execute.
	.globl	mapseg_
mapseg_:
	ld	r0, rr14(4)		/ seg (low byte)
	ld	r1, rr14(6)		/ base page (hi:lo)
	ld	r2, rr14(8)		/ attr (low byte)
	soutb	0x01fc, rl0		/ select descriptor
	soutb	0x0ffc, rh1		/ base hi
	soutb	0x0ffc, rl1		/ base lo
	ldb	rl3, $0xff
	soutb	0x0ffc, rl3		/ limit = 0xff (64K)
	soutb	0x0ffc, rl2		/ attr
	ret
