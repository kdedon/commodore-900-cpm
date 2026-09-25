/ Copyright (c) 2026 Kevin Dedon.
/ SPDX-License-Identifier: MIT

/ bootent(n), the boot module's first bytes: cpm.sys links before the
/ module and calls it only here.  0 = biosinit(), 1 = bdosinit().
	.globl	biosinit_, bdosinit_
	.shri

	ld	r0, rr14(4)
	test	r0
	jp	z, biosinit_
	jp	bdosinit_
