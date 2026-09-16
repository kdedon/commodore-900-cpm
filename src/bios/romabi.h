/*
 * romabi.h -- stock C900 boot-ROM entry points the CP/M BIOS reuses.
 * Trimmed from coherent/boot/src/romabi.h (kboot), which documents the
 * full surface; confirmed against ~/git/C900/firmware/bios_disassembly.txt
 * and firmware/rom_source/.
 *
 * The ROM is MWC-Coherent-C for the Z8001 -- same stack-arg / R1-return
 * convention as our cc -- so a segmented indirect call through a far
 * function pointer (seg 0 : offset) reaches a routine and it RETs back.
 * The ROM stays mapped at seg 0 (code) / seg 1 (its RAM data) after the
 * kboot handoff.  All these require the C stack in seg 0x3F (VKERN).
 */
#ifndef ROMABI_H
#define ROMABI_H

/*
 * Console dispatchers: putchar/puts route to the console the ROM selected
 * at boot -- serial SCC, LR video (BvidCHR), or HR video (AvidCHR) -- per
 * the flags con_alt (01:17ff) and con_hires (01:1800).  putchar expands
 * '\n' to CR+LF.  getchar BLOCKS AND ECHOES: never use it for BIOS
 * CONIN/CONST -- use the keyboard primitives below (or polled SCC when the
 * serial console is selected).
 */
#define ROM_PUTCHAR	0x0fc2
#define ROM_GETCHAR	0x104a
#define ROM_PUTS	0x0900

/*
 * Local keyboard (Z8036 CIO #1, port A = scancode).  The ROM does NOT
 * initialize it at reset: its kbd_init (0x3ed4) runs lazily inside the
 * ROM's own getchar(), behind a once-only flag at 01:041c, and CP/M never
 * calls getchar (docs/run/D5.md in c900oses).  The BIOS therefore does not
 * use these ROM calls; kbd900.h replaces them.  kbd_poll is NON-blocking:
 * raw scancode or 0.  kbd_decode maps a raw scancode to ASCII (0 for
 * key-up/modifier-only events; bit7 of the raw code = key-up), tracking
 * shift/ctrl/alt/caps/num in seg-1 state.
 * See ~/git/C900/firmware/rom_source/keyboard_re.c.
 */
#define ROM_KBDPOLL	0x3f1e
#define ROM_KBDDECODE	0x3f62

/* Polled WD disk (hard-disk sector READ only -- the ROM has no write). */
#define ROM_WDGO	0x111e
#define ROM_WDREAD	0x11a6

/* Block helpers. */
#define ROM_LDIRB	0x0274
#define ROM_PCLEAR	0x0286

/*
 * Segment-1 RAM cells, as CPU far pointers.  A Z8001 far pointer encodes
 * the segment in the HIGH BYTE ((seg<<24)|off), NOT the ROM's laddr
 * (seg<<16)|off form.
 */
#define ROMV_CONALT	0x010017ffL	/* char: LR video console selected */
#define ROMV_CONHIRES	0x01001800L	/* char: HR video console selected */
#define ROMV_BUF	0x01001240L	/* char[512]: ROM sector buffer */
/*
 * The video drivers' saved cursor, RR12 parked between calls: row in the
 * high word and column in the low word for the 6845 character driver
 * BvidCHR, a segmented framebuffer pointer for the bitmap driver AvidCHR
 * (~/git/C900/firmware/rom_source/display_re.c:52-59).  src/crsr.c writes
 * it to position the cursor.
 */
#define ROMV_SCRSTATE	0x01000614L	/* long: video cursor state */

/* Callable aliases (MWC convention; see header comment). */
#define putchar(c)	((int (*)())ROM_PUTCHAR)((int)(c))
#define puts(s)		((int (*)())ROM_PUTS)((char *)(s))
#define kbd_poll()	((int (*)())ROM_KBDPOLL)()
#define kbd_decode(sc)	((int (*)())ROM_KBDDECODE)((int)(sc))
#define wdgo(cmd)	((int (*)())ROM_WDGO)((char *)(cmd))
#define wdread(u,blk,buf) ((int (*)())ROM_WDREAD)((int)(u), \
					(unsigned long)(blk),(unsigned long)(buf))
#define ldirb(src,dst,n) ((int (*)())ROM_LDIRB)((unsigned long)(src),(unsigned long)(dst),(unsigned)(n))
#define pclear(segoff,n) ((int (*)())ROM_PCLEAR)((unsigned long)(segoff),(unsigned)(n))

#endif /* ROMABI_H */
