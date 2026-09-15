

#include "stdio.h"

#include "bdosdef.h"

#include "biosdef.h"


#define   ctrla  0x01
#define   ctrlb  0x02
#define   ctrlc  0x03
#define   ctrle  0x05
#define   ctrlf  0x06
#define   ctrlg  0x07
#define   ctrlk  0x0b
#define   ctrlp  0x10
#define   ctrlq  0x11
#define   ctrlr  0x12
#define   ctrls  0x13
#define   ctrlu  0x15
#define   ctrlw  0x17
#define   ctrlx  0x18

#define   cr      0x0d
#define   lf      0x0a
#define   tab     0x09
#define   rub     0x7f
#define   bs      0x08
#define   space   0x20

  
EXTERN	warmboot();		/* External function definition */
EXTERN UBYTE cpy_bi();		/* copy byte in from user space */
EXTERN LONG  map_adr();		/* map an address for the BDOS	*/


#define CHAINMAX 128		/* one record, as function 47 documents	*/

MLOCAL UBYTE chainbuf[CHAINMAX + 1];


/****************************************************/
/* take a copy of a function 47 command line	    */
/****************************************************/

XADDR setchain(src)
XADDR src;
{
    REG UWORD i;
    REG UWORD n;

    n = UBWORD(cpy_bi(src));
    if (n > CHAINMAX)
	n = CHAINMAX;
    chainbuf[0] = (UBYTE)n;
    for (i = 0; i < n; i++)
	chainbuf[i + 1] = cpy_bi(src + 1L + (long)i);
    return (map_adr((XADDR)chainbuf, 0));
}

/*  Function 10 editing state.  The BDOS is single-threaded, so the line
    being edited can keep its geometry in module data, which is where
    CP/M 3 keeps it too (conbdos.asm strtcol/savepos).  */

MLOCAL UWORD lastlen = 0;	/* length of the line ^W would give back	*/
MLOCAL UWORD stcol;		/* column the line starts in (v3: strtcol) */
MLOCAL UWORD oldend;		/* column it ended in before this edit	   */


/******************/
/* console status */
/******************/

BOOLEAN constat()
{
    REG UBYTE ch;
    BSETUP

    if (GBL.conmode & CM_CTLC)
    {		/* ^C-only status: anything else is held for the next
		   input call and reported as "nothing waiting" */
	if ( ! bconstat() ) return(FALSE);
	return( ch == ctrlc );
    }
}

/********************/
/* check for ctrl/s */
/* used internally  */
/********************/


#define CONBRK_POLL 8

MLOCAL UBYTE brkctr = 0;	/* characters emitted since the last poll */

conbrk()
{
    REG UBYTE ch;
    REG BOOLEAN stop;
    BSETUP

    if (GBL.conmode & CM_NOSTOP) { brkctr = 0; return; }
		/* stop-scroll disabled: output is not interruptible at all,
		   ^C included -- CP/M 3 leaves conbrk immediately too */
    if (++brkctr < CONBRK_POLL) return;
    brkctr = 0;
    stop = FALSE;
    if ( bconstat() ) do
    {
	if ( (ch = bconin()) == ctrlc && !(GBL.conmode & CM_NOTERM) )
	{
	    GBL.retcode = RC_CTLC;
	    warmboot(1);
	}
	if ( ch == ctrls ) stop = TRUE;
	else if (ch == ctrlq) stop = FALSE;
	else if (ch == ctrlp) GBL.lstecho = !GBL.lstecho;
    } while (stop);
}


/******************/
/* console output */
/* used internally*/
/******************/

conout(ch)
REG UBYTE ch;
{
    BSETUP

    conbrk();			/* check for control-s break */
    bconout(ch);		/* output character to console */
    if (GBL.lstecho && !(GBL.conmode & CM_RAW)) blstout(ch);
				/* if ctrl-p on, echo to list dev */
    if (ch >= ' ') GBL.column++;	/* keep track of screen column */
    else if (ch == cr) GBL.column = 0;
    else if (ch == bs) GBL.column--;
}


/*************************************/
/* console output with tab expansion */
/*************************************/

cookdout(ch, ctlout)
REG UBYTE ch;		/* character to output */
BOOLEAN   ctlout;	/* output ^<char> for control chars? */
{
    BSETUP

    if (ch == tab && !(ctlout == FALSE && (GBL.conmode & CM_RAW)))
	do			/* expand tabs */
	    conout( ' ' );
	while (GBL.column & 7);
		/* raw console mode suppresses tab expansion for program
		   output; echoed input (ctlout) always expands, which is
		   the FX==1 test CP/M 3's tabout makes */

    else
    {
	if ( ctlout && (ch < ' ') )
	{
            conout( '^' );
	    ch |= 0x40;
	}
    conout(ch);			/* output the character */
    }
}


/*****************/
/* console input */
/*****************/

UBYTE getch()		/* Get char from buffer or bios */
			/* For internal use only	*/
{
    REG UBYTE temp;
    BSETUP

}
    
UBYTE conin()		/* BDOS console input function */
{
    REG UBYTE ch;
    BSETUP

    conout( ch = getch() );
    if (ch == ctrlp) GBL.lstecho = !GBL.lstecho;
    return(ch);
}

/******************
* raw console i/o *
******************/

UBYTE rawconio(parm)	/* BDOS raw console I/O function */

REG UWORD parm;
{
    BSETUP

    if (parm == 0xff) return(getch());
    else if (parm == 0xfe) return(constat());
    else return(bconout(parm & 0xff));	/* add return to make lint happy */
}


/****************************************************/
/* print line up to delimiter($) with tab expansion */
/****************************************************/

prt_line(p)
REG UBYTE *p;
{
    BSETUP

}


/*******************************************************/
/* print a block of characters (functions 111 and 112)  */
/*						       */
/* The character control block is {address, length}.    */
/* C900 deviation: the address is a 32-bit XADDR, since */
/* that is what a pointer is here -- the 8080 form is   */
/* two bytes.  Length stays a word.		       */
/*******************************************************/

prt_blk(ccbp, toconsole)

XADDR	ccbp;			/* address of the caller's control block */
BOOLEAN	toconsole;		/* console (fcn 111) or list (fcn 112)	 */
{
    struct
    {
	XADDR	cbaddr;		/* address of the characters	*/
	UWORD	cblen;		/* number of characters		*/
    } ccb;
    REG UWORD i;

    cpy_in(ccbp, &ccb, sizeof ccb);
    {
    }
}


/**********************************************/
/* read line with editing and bounds checking */
/**********************************************/

/* Two subroutines first */

newline()			/* new physical line, indented to the start */
{
    REG UWORD i;
    BSETUP

    conout(cr);
    conout(lf);
    for (i = stcol; i != 0; i--) conout(' ');
}



#define	RECALL	128		/* longest line ^W can give back: the
				   CCP's own buffer size, and no CP/M
				   command line is longer */

MLOCAL UBYTE lastlin[RECALL];	/* the previous line, for ^W recall */
MLOCAL savelin(p, len)		/* keep the line for the next ^W */

REG struct conbuf *p;
REG UWORD len;
{
    REG UWORD i;

    if (len == 0) return;	/* an empty line does not replace it */
    for (i = 0; i < len && i < RECALL; i++) lastlin[i] = p->cbuf[i];
    lastlen = i;
}


MLOCAL UWORD colof(p, n)
/*  the column the console reaches after echoing n characters: a tab
    steps to the next multiple of 8, a control character prints as two  */

REG struct conbuf *p;
REG UWORD n;
{
    REG UWORD col;
    REG UWORD i;
    REG UBYTE ch;

    col = stcol;
    for (i = 0; i < n; i++)
    {
	ch = p->cbuf[i];
	if (ch == tab) col += 8 - (col & 7);
	else if (ch < ' ') col += 2;
	else col += 1;
    }
    return(col);
}


MLOCAL backto(col)		/* back up to a column without erasing */

REG UWORD col;
{
    BSETUP

    while (GBL.column > col) conout(bs);
}


MLOCAL UWORD toend(p, cur)	/* echo out to the end of the line */

REG struct conbuf *p;
REG UWORD cur;
{
    while (cur < UBWORD(p->retlen)) cookdout(p->cbuf[cur++], TRUE);
    return(cur);
}


MLOCAL gotoch(p, n)		/* put the console back at character n */

REG struct conbuf *p;
UWORD n;
{
    backto( colof(p, n) );
}


MLOCAL delch(p, at)		/* take the character at `at' out of the line */

REG struct conbuf *p;
REG UWORD at;
{
    REG UWORD i;

    for (i = at; i + 1 < UBWORD(p->retlen); i++) p->cbuf[i] = p->cbuf[i+1];
    p->retlen -= 1;
}


MLOCAL repaint(p, from, cur)
/*  reprint the line from `from', erase whatever the old line left
    beyond it, and leave the cursor at character `cur'.  The console
    must already be sitting at the column of `from'.  */

REG struct conbuf *p;
UWORD from;
UWORD cur;
{
    REG UWORD i;
    BSETUP

    for (i = from; i < UBWORD(p->retlen); i++) cookdout(p->cbuf[i], TRUE);
    i = 0;
    while (GBL.column < oldend)	/* blank out what the old line left */
    {
	conout(' ');
	i += 1;
    }
    while (i)			/* and come back over the blanks */
    {
	conout(bs);
	i -= 1;
    }
    gotoch(p, cur);
}


readline(p)			/* BDOS function 10 */
REG struct conbuf *p;

{
    REG UBYTE ch;
    REG UWORD i;
    REG UWORD j;
    REG UBYTE *q;
    UWORD cur;			/* cursor position within the line	*/
    UWORD len;			/* line length (mirrors p->retlen)	*/
    UWORD max;			/* caller's buffer size			*/

    BSETUP

    stcol = GBL.column;		/* set up starting column */
    if (GBL.chainp != XNULL)	/* chain to program code  */
    {
	i = UBWORD(cpy_bi(GBL.chainp++));	/* cpy in from user space */
	j = UBWORD(p->maxlen);
	if (j < i) i = j;		/* don't overflow console buffer! */
	p->retlen = (UBYTE)i;
	q = p->cbuf;
	while (i)
	{
	    cookdout( *q++ = cpy_bi(GBL.chainp++), TRUE);
	    i -= 1;
	}
	GBL.chainp = XNULL;
	return;
    }

    p->retlen = 0;		/* start out with empty buffer */
    len = 0;
    cur = 0;
    max = UBWORD(p->maxlen);

    for (;;)			/* main loop for read console buffer */
    {
	ch = getch();
	oldend = colof(p, len);

	if ( ch == ctrlc && len == 0 && !(GBL.conmode & CM_NOTERM) )
	{
	    cookdout(ctrlc, TRUE);
	    GBL.retcode = RC_CTLC;
	    warmboot(1);
	}

	else if ( (ch == cr) || (ch == lf) )
	{				/* if cr or lf, exit */
	    conout(cr);
	    break;
	}

	else if ( ch == bs || ch == rub )	/* delete a character */
	{
	    if (cur)
	    {
		cur -= 1;
		if (ch == rub && GBL.echodel && cur + 1 == len)
		{	/* the echoing form: resend the deleted character and
			   leave it on the screen, which is what 2.2 does.
			   Only at the end of the line -- with the cursor
			   inside it, CP/M 3 falls back to erasing too */
		    conout( p->cbuf[cur] );
		    p->retlen = (UBYTE)(--len);
		}
		else
		{
		    delch(p, cur);
		    len -= 1;
		    gotoch(p, cur);
		    repaint(p, cur, cur);
		}
	    }
	}

	else if (ch == ctrlg)			/* delete under the cursor */
	{
	    if (cur < len)
	    {
		delch(p, cur);
		len -= 1;
		repaint(p, cur, cur);
	    }
	}

	else if (ch == ctrla)			/* cursor left */
	{
	    if (cur) gotoch(p, --cur);
	}

	else if (ch == ctrlf)			/* cursor right */
	{
	    if (cur < len) cookdout(p->cbuf[cur++], TRUE);
	}

	else if (ch == ctrlb)			/* start of line, or end */
	{
	    if (cur)
	    {
		cur = 0;
		backto(stcol);
	    }
	    else cur = toend(p, cur);
	}

	else if (ch == ctrlk)			/* delete to end of line */
	{
	    p->retlen = (UBYTE)(len = cur);
	    repaint(p, cur, cur);
	}

	else if (ch == ctrlw)			/* recall the last line */
	{
	    if (cur < len)		/* first walk out to the end */
		cur = toend(p, cur);
	    else if (len == 0)
	    {
		for (i = 0; i < lastlen && i < max; i++)
		    cookdout( p->cbuf[i] = lastlin[i], TRUE );
		p->retlen = (UBYTE)(len = cur = i);
	    }
	}

	else if (ch == ctrlp) GBL.lstecho = !GBL.lstecho;
						/* control-p */

	else if (ch == ctrlx)			/* kill back to line start */
	{
	    if (cur)
	    {
		for (i = 0; i + cur < len; i++) p->cbuf[i] = p->cbuf[i+cur];
		p->retlen = (UBYTE)(len -= cur);
		cur = 0;
		backto(stcol);
		repaint(p, 0, 0);
	    }
	}

	else if (ch == ctrle) newline();	/* control-e */

	else if (ch == ctrlu)			/* control-u */
	{
	    savelin(p, len);		/* ^W can still get it back */
	    conout('#');
	    newline();
	    p->retlen = 0;
	    len = cur = 0;
	}

	else if (ch == ctrlr)			/* control-r */
	{
	    conout('#');
	    newline();
	    cur = toend(p, 0);
	}

	else if (len >= max)		/* buffer full: refuse and complain */
	    conout(0x07);

	else					/* normal character */
	{
	    for (i = len; i > cur; i--) p->cbuf[i] = p->cbuf[i-1];
	    p->cbuf[cur] = ch;
	    p->retlen = (UBYTE)(++len);
	    cur += 1;
	    repaint(p, cur - 1, cur);
	}
    }
    savelin(p, len);
}
