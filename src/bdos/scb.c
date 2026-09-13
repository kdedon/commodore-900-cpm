
#include "stdio.h"		/* Standard I/O declarations */

#include "bdosdef.h"		/* Type and structure declarations for BDOS */

#include "biosdef.h"		/* cpy_in / cpy_out			*/

#include "scb.h"		/* SCB offsets				*/

EXTERN XADDR	tpa_ht;		/* TPA upper boundary (bdosmain.c)	*/


/*  The image.  Byte-for-byte the v3 layout, including its initial values:
    outdelim '$', multcnt 1, the 0,0FFh,0FFh,0FFh drive search chain and
    the five 0FFh stamp bytes are all as resbdos.asm ships them.  The
    long-message flag is 80h, which is the banked build's value and the
    one we always want (this port always builds the banked-class BDOS).  */

MLOCAL UBYTE scbimg[SCBLEN] =
{
    0,				/* 00 hashl				*/
    0, 0, 0, 0,			/* 01 hash				*/
    SCB_VER_VALUE,		/* 05 version				*/
    0, 0, 0, 0,			/* 06 util$flgs				*/
    0, 0,			/* 0a dspl$flgs				*/
    0, 0,			/* 0c (reserved)			*/
    0, 0,			/* 0e clp$flgs				*/
    0, 0,			/* 10 clp$errcde			*/
    0,				/* 12 ccp$comlen			*/
    0,				/* 13 ccp$curdrv			*/
    0,				/* 14 ccp$curusr			*/
    0, 0,			/* 15 ccp$conbuff			*/
    0, 0,			/* 17 ccp$flgs				*/
    0,				/* 19 (reserved)			*/
    SCB_WIDTH_VALUE,		/* 1a conwidth				*/
    0,				/* 1b column				*/
    0,				/* 1c conpage				*/
    0,				/* 1d conline				*/
    0, 0,			/* 1e conbuffadd			*/
    0, 0,			/* 20 conbufflen			*/
    0, 0,			/* 22 conin$rflg  (@CIVEC)		*/
    0, 0,			/* 24 conout$rflg (@COVEC)		*/
    0, 0,			/* 26 auxin$rflg  (@AIVEC)		*/
    0, 0,			/* 28 auxout$rflg (@AOVEC)		*/
    0, 0,			/* 2a lstout$rflg (@LOVEC)		*/
    0,				/* 2c page$mode				*/
    0,				/* 2d pm$default			*/
    0,				/* 2e ctlh$act				*/
    0,				/* 2f rubout$act			*/
    0,				/* 30 type$ahead			*/
    0, 0,			/* 31 contran				*/
    0, 0,			/* 33 conmode				*/
    0, 0,			/* 35 @BNKBF				*/
    '$',			/* 37 outdelim				*/
    0,				/* 38 listcp				*/
    0,				/* 39 qflag				*/
    0, 0,			/* 3a scbadd				*/
    0x80, 0,			/* 3c dmaad = 0080h			*/
    0,				/* 3e @CRDSK				*/
    0, 0,			/* 3f info				*/
    0,				/* 41 resel				*/
    0,				/* 42 relog				*/
    0,				/* 43 @FX				*/
    0,				/* 44 @USRCD				*/
    0, 0,			/* 45 dcnt				*/
    0, 0,			/* 47 searcha				*/
    0,				/* 49 searchl				*/
    1,				/* 4a @MLTIO				*/
    0,				/* 4b @ERMDE				*/
    0, 0xff, 0xff, 0xff,	/* 4c searchchain			*/
    0,				/* 50 temp$drive			*/
    0,				/* 51 @ERDSK				*/
    0, 0,			/* 52 (reserved)			*/
    0,				/* 54 @MEDIA				*/
    0, 0,			/* 55 (reserved)			*/
    SCB_BFLGS_VALUE,		/* 57 @BFLGS				*/
    0xff, 0xff, 0xff, 0xff, 0xff, /* 58 @DATE/@HOUR/@MIN/@SEC		*/
    0, 0,			/* 5d commonbase			*/
    0, 0, 0,			/* 5f ?ERJMP				*/
    0, 0			/* 62 @MXTPA				*/
};



UBYTE *scbstampa()
{
    return( &scbimg[SCB_DATE] );
}



scbccpflg(bits)

REG UBYTE bits;
{
    scbimg[SCB_CCPFLGS] |= bits;
}


/*  word access to the image: low byte first, as on the 8080  */

MLOCAL UWORD scbword(off)

REG UWORD off;
{
    return( UBWORD(scbimg[off]) | (UBWORD(scbimg[off+1]) << 8) );
}


MLOCAL scbputw(off, val)

REG UWORD off;
REG UWORD val;
{
    scbimg[off]   = (UBYTE)val;
    scbimg[off+1] = (UBYTE)(val >> 8);
}


/****************************************************
**
** scbsync() -- refresh the mirror fields from the
**		BDOS state they mirror.
**
****************************************************/

MLOCAL scbsync(info)

REG UWORD info;			/* the parameter word of this call	*/
{
    BSETUP

    scbputw(SCB_CRDMA, (UWORD)(GBL.dmaadr & 0xffffL));
    scbimg[SCB_CRDSK] = GBL.curdsk;
    scbputw(SCB_VINFO, info);
    scbimg[SCB_FX] = GBL.curfx;
    scbimg[SCB_USRCD] = GBL.user;
    scbputw(SCB_DCNT, GBL.srchpos);
    scbputw(SCB_SEARCHA, (UWORD)(GBL.srchp & 0xffffL));
    /*  @MXTPA: v3 publishes the first address ABOVE the TPA (the BDOS
	entry, which on an 8080 is inside the same 64K and so always
	fits in the word).  Our TPA can end exactly on a segment
	boundary -- it does, being one whole 64K segment -- where that
	form is not representable and would read as 0.  The field
	therefore carries the HIGHEST offset the TPA contains.  A
	program that wants the layout of its own load reads its base
	page, which is where CP/M-8000 has always published it.  */
    scbputw(SCB_MXTPA, (UWORD)((tpa_ht - 1L) & 0xffffL));

    scbimg[SCB_COLUMN] = (UBYTE)GBL.column;
    /*	the four console-paging bytes.  They were image-only until the
	BDOS grew a pager (conbdos.c pagelf); they are mirrors now, so a
	program that reads page$mode sees what the driver is really
	doing and one that writes it changes what the driver does.  */
    scbimg[SCB_CONPAGE] = GBL.conpage;
    scbimg[SCB_CONLINE] = GBL.conline;
    scbimg[SCB_PAGEMODE] = GBL.pagemode;
    scbimg[SCB_PMDEFAULT] = GBL.pmdefault;
    scbputw(SCB_CONMODE, GBL.conmode);
    scbimg[SCB_OUTDELIM] = GBL.delim;
    scbimg[SCB_MLTIO] = GBL.multcnt;
    scbimg[SCB_ERMDE] = GBL.errmode;
    scbputw(SCB_ERRCDE, GBL.retcode);
}



MLOCAL scbpost()
{
    REG UWORD n;
    BSETUP

    GBL.column = UBWORD(scbimg[SCB_COLUMN]);
    /*	All four paging bytes are writable.  CP/M 3 documents @CONPAGE
	and page$mode as read/write (ref/cpm3/scb.asm) and @CONLINE as
	the driver's own, but v3's func49 has no read-only check at all
	(bdos30.asm:4716-4725) and a program that wants to start a fresh
	page by zeroing the line count is doing something reasonable, so
	the value is honoured rather than silently discarded.  */
    GBL.conpage = scbimg[SCB_CONPAGE];
    GBL.conline = scbimg[SCB_CONLINE];
    GBL.pagemode = scbimg[SCB_PAGEMODE];
    GBL.pmdefault = scbimg[SCB_PMDEFAULT];
    GBL.conmode = scbword(SCB_CONMODE);
    GBL.delim = scbimg[SCB_OUTDELIM];
    GBL.errmode = scbimg[SCB_ERMDE];
    GBL.retcode = scbword(SCB_ERRCDE);

    /*  the multi-sector count is the one mirror whose out-of-range
	values would misbehave rather than merely be wrong (multio
	loops on it), so it is clamped to what function 44 accepts and
	the clamped value is stored back, leaving image and state in
	agreement  */
    n = UBWORD(scbimg[SCB_MLTIO]);
    if (n == 0) n = 1;
    if (n > 128) n = 128;
    scbimg[SCB_MLTIO] = (UBYTE)n;
    GBL.multcnt = (UBYTE)n;
}



UWORD scb_fn(pbp, info)

XADDR	  pbp;			/* address of the caller's parameter block */
REG UWORD info;			/* the parameter word, for @VINFO	*/
{
    UBYTE     pb[SCBPBLEN];
    REG UWORD off;
    REG UWORD set;

    cpy_in(pbp, pb, SCBPBLEN);
    off = UBWORD(pb[SCBPB_OFF]);
    if (off >= SCBMAX)
	return(0xffff);		/* v3 returns with the result register
				   untouched; an explicit refusal is more
				   use than a garbage word */

    scbsync(info);

    set = UBWORD(pb[SCBPB_SET]);
    if (set == SCBSET_BYTE)
    {
	scbimg[off] = pb[SCBPB_VALUE];
	scbpost();
	return(0);
    }
    if (set == SCBSET_WORD)
    {
	scbimg[off]   = pb[SCBPB_VALUE];
	scbimg[off+1] = pb[SCBPB_VALUE+1];
	scbpost();
	return(0);
    }
    return( scbword(off) );
}
