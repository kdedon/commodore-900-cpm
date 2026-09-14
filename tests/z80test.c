		 * unset SCB field is.
		 *
		 * page$mode (0x2c) IS THE ONE TO WATCH, and the comment
		 * above is loose about it: the byte is inverted in CP/M 3,
		 * so the zero this falls through to means paging is ON.
		 * That is deliberate and it is what the BDOS on the machine
		 * ships too (pm$default = PM_ON, src/bdos/bdosmisc.c) --
		 * DRI's DUMP.COM reads this byte
		 * (ref/cpm3/dump.asm:251,374-380,429) and takes a different
		 * path on it, so the two runs would diverge on any other
		 * answer and verify-z80's triple would not match.  Do not
