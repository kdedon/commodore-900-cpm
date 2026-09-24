/ Copyright (c) 2026 Kevin Dedon.
/ SPDX-License-Identifier: MIT

/ Linked into every module.  cc2 relocates each frame reference against SS,
/ and module code runs on the resident system's stack, segment 0x3F.
	.globl	SS
SS	=	0x3f3f
