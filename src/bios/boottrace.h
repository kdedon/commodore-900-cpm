/*
 * boottrace.h -- opt-in cold-boot progress markers.
 *
 * The cold path from crt.s to the BDOS sign-on banner has no output of
 * its own between "CP/M-8000(tm) for the Commodore 900" (cmain.c) and
 * prt_line(SYS_BANNER) (bdosmisc.c).  A machine that stops in that window
 * says nothing about where.  BTRACE() emits one short marker per step so
 * a single boot names the last step reached.
 *
 * The marker goes out through the BOOT ROM's own console dispatcher
 * (romabi.h ROM_PUTS, seg 0 offset 0x0900) -- the same routine cmain.c's
 * puts() used to print the line that IS reaching the screen.  It depends
 * on no part of the BIOS console layer, no BDOS, and no prt_line, so it
 * stays legible while the layer under test is the suspect.  Its only
 * requirement is the one every ROM call has: the C stack in seg 0x3F,
 * which ccpentry (glue.s) has already established.
 *
 * OFF unless BOOT_TRACE is defined, and nothing here is compiled into a
 * default build: `make cpmtrace' recurses with its own OBJDIR and DEFS
 * and writes build/cpmtrace.bin, leaving build/cpm.sys and
 * build/cpmonly.bin byte-identical to what `make all' produces.
 * Markers are listed in docs/run/D3.md.
 */
#ifndef BOOTTRACE_H
#define BOOTTRACE_H

#ifdef BOOT_TRACE
#define BTRACE(s)	((int (*)())0x0900)((char *)(s))
/*
 * BTRACE1: the same marker, but ONCE.  Sites on a path the running system
 * re-enters -- xbdos(), the SC trap gate -- would otherwise print a marker
 * per BDOS call and bury the cold-boot sequence (and change the timing of
 * the thing being measured).  Only the first arrival is the cold-boot fact.
 */
#define BTRACE1(s)	{ static char _btseen = 0; \
			  if (!_btseen) { _btseen = 1; BTRACE(s); } }
#else
#define BTRACE(s)
#define BTRACE1(s)
#endif

#endif /* BOOTTRACE_H */
