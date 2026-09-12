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
	for (i = 1; i < argc; i++) {
		putdec(i);
	}
	cputs("code+data+bss = ");
	putdec((unsigned) (_base->codelen + _base->datalen + _base->bsslen));
	cputs(" bytes\r\n");
	return (0);
}
