/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/*
 * gen-z80dectab.c -- emit the base-map table src/shim/z80deca.s reads.
 *
 * The reference decoder is the oracle: every field of every record is
 * whatever z80dec.c answers for that opcode, so renumbering Z_* in
 * z80.h means re-running this and pasting the output back between the
 * `btab:' label and the end of the table.
 *
 *	cc -std=gnu89 -w -DHOSTCC -o /tmp/gen-z80dectab \
 *		tools/gen-z80dectab.c src/shim/z80dec.c
 *	/tmp/gen-z80dectab
 *
 * The four prefix bytes get an all-zero record: a zero length is what
 * marks them, and the cleared fields are what the prefix paths start
 * from.  The last column says whether the opcode names the byte at
 * (HL), which under DD/FD becomes (IX+d) and grows a displacement --
 * read off the reference decoder as the length a DD prefix adds.
 */

#include <stdio.h>
#include <string.h>

#include "../src/shim/z80.h"

static char mem[8];

static int dec(struct z80in *in, int pfx, int op)
{
	memset(mem, 0, sizeof mem);
	if (pfx)
		mem[0] = pfx, mem[1] = op;
	else
		mem[0] = op;
	return (z80dec(mem, 0, in));
}

int main()
{
	struct z80in in, ix;
	int op, u;

	for (op = 0; op < 256; op++) {
		if (op == 0xcb || op == 0xdd || op == 0xed || op == 0xfd) {
			printf("\tE(0,0,0,0,0,0)\t/ %02X\n", op);
			continue;
		}
		dec(&in, 0, op);
		dec(&ix, 0xdd, op);
		u = ix.len - 1 - in.len;
		if (u != 0 && u != 1) {
			fprintf(stderr, "opcode %02X: dd adds %d\n", op, u);
			return (1);
		}
		printf("\tE(%d,%d,%d,%d,%d,%d)\t/ %02X\n",
			in.len, in.op, in.fl, in.x, in.y, u, op);
	}
	return (0);
}
