/*
 * mhello.c - Exercise the segmented transient loader, BDOS string/character
 * output, command arguments, and base page.
 */

#include "cpm.h"

int main(argc, argv)
int argc;
char *argv[];
{
	register int	i;

	printstr("MWC hello from the Coherent Z8001 pipeline (0xEE01)$");
	cputs("\r\n");
	/*
	 * The argument echo goes out through fn 2 (conputs, libcpm.c), not
	 * fn 111, so that the "both fn 9 and fn 2" above stays true and so
	 * that verify-rsx has an UNRELATED program whose console output a
	 * resident module can fold.  An RSX hooks the BDOS entry; fn 111 is
	 * served by prt_blk() inside the BDOS, below the chain, so a module
	 * never sees it.  The text is byte-for-byte what cputs() printed.
	 */
	for (i = 1; i < argc; i++) {
		conputs("  arg ");
		putdec(i);
		conputs(": ");
		conputs(argv[i]);
		conputs("\r\n");
	}
	cputs("code+data+bss = ");
	putdec((unsigned) (_base->codelen + _base->datalen + _base->bsslen));
	cputs(" bytes\r\n");
	return (0);
}
