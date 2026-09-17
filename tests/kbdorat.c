/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/*
 * kbdorat.c -- COHERENT's C900 keyboard table (rec/kbtab.c, unmodified) for
 * the host oracle.  It is a separate object from kbdoracle.c because
 * kbtab.h has no include guard and kb.c and kbtab.c both include it.
 */

#include "kbtab.c"

/* The table's extent, as the current driver bounds it (hrtty/kb.c:299). */
int oranktab() { return ((int)(sizeof ktab / sizeof ktab[0])); }
