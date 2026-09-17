/*
 * Portions Copyright (c) 2026 Kevin Dedon.
 */
/* Parse [drive:]name[.type][;password] using CP/M 3 rules. The parameter
 * block contains two 32-bit addresses. Return the delimiter offset,
 * zero at NUL/CR, or FFFFh for an invalid name. Source: cpmbdos2.asm PARSE. */

#include "stdio.h"		/* Standard I/O declarations */

#include "bdosdef.h"		/* Type and structure declarations for BDOS */

#include "biosdef.h"		/* cpy_in / cpy_out			*/

EXTERN UBYTE	cpy_bi();	/* copy one byte in from user space	*/

#define TAB	'\011'
#define CR	'\015'

#define PFCBLEN	36		/* the parse FCB: 36 bytes, password and
				   all, exactly as v3 fills it		*/

/*  The parse parameter block.  */

struct pfcb
{
	XADDR	fname;		/* the name to parse			*/
	XADDR	fcbp;		/* where the FCB goes			*/
};

/*  Scan state.  v3 keeps this in registers; the file system is
    single-threaded, so module data is the same thing here.  */

MLOCAL XADDR	pos;		/* cursor in the caller's string	*/
MLOCAL UBYTE	*outp;		/* where the next character goes	*/
MLOCAL UWORD	room;		/* characters left in the field		*/
MLOCAL BOOLEAN	stars;		/* does '*' expand in this field?	*/
MLOCAL BOOLEAN	bad;		/* the name is invalid			*/


/*  the delimiter table, cpmbdos2.asm:549 -- note that the terminating
    zero of the table is itself matched, so a NUL is a delimiter  */

MLOCAL BYTE delims[] = { CR, TAB, ' ', '.', ',', ':', ';',
			 '[', ']', '=', '<', '>', '|', 0 };


MLOCAL BOOLEAN pdelim(c)

REG UWORD c;
{
    REG UWORD i;

    for (i = 0; i < sizeof delims; i++)
	if (c == UBWORD(delims[i])) return(TRUE);
    return(FALSE);
}


/*  the upper-casing half of v3's `delim': below 'a' the byte is passed
    through untouched, 'a'..'z' are folded with 5Fh, and anything above
    'z' is masked to seven bits  */

MLOCAL UWORD pupper(c)

REG UWORD c;
{
    if (c < 'a') return(c);
    if (c <= 'z') return(c & 0x5f);
    return(c & 0x7f);
}


/****************************************************
**
** gfc() -- v3's `gfc': take one character into the
**	    current field.  FALSE ends the field.
**
****************************************************/

MLOCAL BOOLEAN gfc()
{
    REG UWORD c;

    c = UBWORD(cpy_bi(pos));
    if (pdelim(c)) return(FALSE);	/* end of the name */
    c = pupper(c);
    pos++;
    if (c < ' ')			/* a control character is an error */
    {
	bad = TRUE;
	return(FALSE);
    }
    if (room == 0)			/* one character too many */
    {
	bad = TRUE;
	return(FALSE);
    }
    if (stars && c == '*')		/* match the rest of the field */
    {
	while (room != 0)
	{
	    *outp++ = '?';
	    room--;
	}
	return(TRUE);
    }
    *outp++ = (UBYTE)c;
    room--;
    return(TRUE);
}


/****************************************************
**
** parsefn(pfcbp) -- BDOS function 152.
**
**	[d:]name[.typ][;password]
**
****************************************************/

UWORD parsefn(pfcbp)

XADDR pfcbp;			/* address of the caller's parameter block */
{
    struct pfcb	pb;
    UBYTE	fcb[PFCBLEN];
    REG UWORD	i;
    REG UWORD	c;
    BOOLEAN	dotype;
    BOOLEAN	dopw;
    XADDR	stop;
    XADDR	q;

    cpy_in(pfcbp, &pb, sizeof pb);

    /*  parse0: drive byte zero, name and type blank, the four FCB
	control bytes zero, the password blank, everything after it
	zero  */
    fcb[0] = 0;
    for (i = 1; i <= 11; i++) fcb[i] = ' ';
    for (i = 12; i <= 15; i++) fcb[i] = 0;
    for (i = 16; i <= 23; i++) fcb[i] = ' ';
    for (i = 24; i < PFCBLEN; i++) fcb[i] = 0;

    bad = FALSE;
    dotype = FALSE;
    dopw = FALSE;

    /*  skps: skip leading blanks and tabs  */
    pos = pb.fname;
    while ((c = UBWORD(cpy_bi(pos))) == ' ' || c == TAB) pos++;

    /*  a ':' as the second character makes the first one a drive.
	(v3 reads that second byte even when the first is the string's
	NUL; we do not -- the only place this parser runs off the end
	of its input.)  */
    if (c != 0 && UBWORD(cpy_bi(pos+1)) == ':')
    {
	if (pdelim(c)) goto done;	/* parsedrv: delimiter, nothing to do */
	c = pupper(c);
	if (c < 'A' || c > 'A' + 15)
	{
	    bad = TRUE;
	    goto done;
	}
	fcb[0] = (UBYTE)(c - 'A' + 1);
	pos += 2;			/* past the letter and the ':' */
    }

    /*  parse$name.  A delimiter right here ends the whole parse, which
	is why a leading '.' yields an empty name AND an empty type.  */
    outp = &fcb[1];
    room = 8;
    stars = TRUE;
    if (! pdelim(UBWORD(cpy_bi(pos))))
	for (;;)
	{
	    c = UBWORD(cpy_bi(pos));
	    if (c == '.') { dotype = TRUE; break; }
	    if (c == ';') { dopw = TRUE; break; }
	    if (! gfc()) break;
	}

    if (dotype)
    {
	pos++;				/* past the dot */
	outp = &fcb[9];
	room = 3;
	stars = TRUE;
	for (;;)
	{
	    c = UBWORD(cpy_bi(pos));
	    if (c == ';') { dopw = TRUE; break; }
	    if (! gfc()) break;
	}
    }

    if (dopw)
    {
	pos++;				/* past the ';' */
	outp = &fcb[16];
	room = 8;
	stars = FALSE;			/* '*' is a password character */
	while (gfc()) ;
	fcb[26] = (UBYTE)(8 - room);	/* length of the password */
    }

done:
    /*  v3 parses in place, so the caller sees the padding and whatever
	was accepted even when the name turns out to be invalid  */
    cpy_out(fcb, pb.fcbp, (long)PFCBLEN);
    if (bad) return(0xffff);

    /*  parse$ok: skip trailing blanks and tabs and look at what stopped
	the scan.  A NUL or a carriage return returns zero; any other
	delimiter returns where it is; anything else (a non-delimiter
	after blanks) returns where the scan actually stopped.  */
    stop = pos;
    q = pos;
    while ((c = UBWORD(cpy_bi(q))) == ' ' || c == TAB) q++;
    if (pdelim(c))
    {
	if (c == 0 || c == CR) return(0);
	return( (UWORD)(q & 0xffffL) );
    }
    return( (UWORD)(stop & 0xffffL) );
}
