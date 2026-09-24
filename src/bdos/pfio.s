/ Copyright (c) 2026 Kevin Dedon.
/ SPDX-License-Identifier: MIT

/ The parse module's copies of the resident cpy_bi and map_adr.  map_adr
/ handles space 0 only, the one parsefn.c's cpy_in/cpy_out pass.

	.globl	cpy_bi_, map_adr_
	.shri

/ UBYTE cpy_bi(addr)  XADDR addr;
cpy_bi_:
	ldl	rr2, rr14(4)
	ldb	rl1, @rr2
	subb	rh1, rh1
	ret

/ long map_adr(adr, 0): the address without the segment word's bit 15.
map_adr_:
	ldl	rr0, rr14(4)
	res	r0, $15
	ret
