/*
 * libcpm.c - thin C wrappers over the SC #2 BDOS gate (__bdos, bdossc.s).
 * Pointers pass as longs: a far pointer's value IS the XADDR the BDOS
 * wants.
 */

#include "cpm.h"

int conout(c)
int c;
{
	return (__bdos(BDOS_CONOUT, (long) c));
}

int conin()
{
	return (__bdos(BDOS_CONIN, 0L));
}

VOID cputs(s)
register char *s;
{
		conout(*s++);
}

/* print a '$'-terminated string via BDOS fn 9 */
VOID printstr(s)
char *s;
{
	__bdos(BDOS_PRINTSTR, (long) s);
}

VOID putdec(n)
unsigned n;
{
	if (n >= 10)
		putdec(n / 10);
	conout('0' + n % 10);
}

VOID setdma(p)
char *p;
{
	__bdos(BDOS_SETDMA, (long) p);
}

/*
 * Parse "NAME", "NAME.EXT" or "D:NAME.EXT" (already uppercased by the
 * CCP) into a fresh FCB.
 */
VOID mkfcb(name, f)
register char *name;
struct fcb *f;
{
	register char	*p;
	register int	i;

	p = (char *) f;
	for (i = 0; i < sizeof (struct fcb); i++)
		*p++ = 0;
	if (name[0] != 0 && name[1] == ':') {
		f->drvcode = name[0] - 'A' + 1;
		name += 2;
	}
	for (i = 0; i < 8; i++)
		f->fname[i] = ' ';
	for (i = 0; i < 3; i++)
		f->ftype[i] = ' ';
	for (i = 0; i < 8 && *name != 0 && *name != '.'; i++)
		f->fname[i] = *name++;
	while (*name != 0 && *name != '.')
		name++;
	if (*name == '.')
		name++;
	for (i = 0; i < 3 && *name != 0; i++)
		f->ftype[i] = *name++;
}
