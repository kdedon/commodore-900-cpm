/ Copyright (c) 2026 Kevin Dedon.
/ SPDX-License-Identifier: MIT

/ ucrsxl.s -- UCASEL.RSX is linked below PROT to test the opposite chain order.
/ Fixed-address modules cannot be relocated at attachment.

#define	RSXORG	0xE000

#include "ucrsx.s"
