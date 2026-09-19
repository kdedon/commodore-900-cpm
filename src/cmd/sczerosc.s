/ Copyright (c) 2026 Kevin Dedon.
/ SPDX-License-Identifier: MIT

/ sczerosc.s -- sczero(): execute SC #0 (7F00), DDT.Z8K's breakpoint
/ instruction, and return if anything ever resumes past it.

	.globl	sczero_
	.shri

sczero_:
	sc	0
	ret
