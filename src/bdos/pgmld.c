
#include "stdio.h"		/* Standard declarations for BDOS, BIOS */

#include "bdosdef.h"		/* Type and structure declarations for BDOS */

#include "biosdef.h"		/* Declarations of BIOS functions 	*/

#include "basepage.h"		/* Base page structure			*/

#include "x.out.h"		/* structure of x.out (".Z8[KS] file")	*/

#include "c900cfg.h"		/* C900: TPA + split-I/D bank layout	*/

#include "rsxhdr.h"		/* C900: the RSX prefix (RSXHDRLEN)	*/

extern int spload();		/* C900: split-I/D scan-and-patch	*/
extern short spflag;		/*   (splitld.c) loaded-program-is-split*/
extern UWORD rsxres();		/* C900: bytes of TPA held by resident	*/
				/*   system extensions (rsx.c).  v3 does	*/
				/*   this by lowering the top of the TPA	*/
				/*   in @MXTPA and location 6 so the	*/
				/*   loader loads underneath resident	*/
				/*   modules (loader3.asm:302-314); the	*/
				/*   segment limits below are where that	*/
				/*   lowering has to land here.		*/
extern UWORD rsxchk();		/* C900: placement rules + chain entry	*/
extern UWORD rsxlink();		/*   (rsx.c), for the GENCOM path below	*/

#define SPLIT	0x4000		/* Separate I/D flag for LPB		*/
#define SEG	0x2000		/* Segmented code flag for TPA		*/
#define NSEG	16		/* Maximum number of x.out segments	*/
#define SEGLEN	0x10000		/* Length of a Z8000 segment		*/
				/* Address of basepage (near top of TPA)*/
#define BPLEN	(sizeof (struct b_page))
#define DEFSTACK 0x100		/* Default stack length			*/
#define NREGIONS 2		/* Number of regions in the MRT		*/


#define X_GC_MAGIC 0xEE05	/* GENCOM'd: modules, then the program	*/
#define GCHDRLEN   256		/* the GENCOM header: two records	*/

/* return values */

#define GOOD	0		/* good return value			*/
#define BADHDR	1		/* bad header 		 		*/
#define NOMEM	2		/* not enough memory 			*/
#define READERR	3		/* read error 				*/

#define MYDATA	0		/* Argument for map_adr			*/
#define TPAPROG	5		/* Argument for map_adr			*/
#define TPADATA	4		/* Argument for map_adr			*/
				/* Get actual code segment (as opposed	*/
				/*   to segment where it can be accessed*/
				/*   as data)				*/
#define TRUE_TPAPROG	(TPAPROG | 0x100)

struct lpb			/* Load Parameter Block			*/
	{
		XADDR	fcbaddr;/* Address of fcb of opened file	*/
		XADDR	pgldaddr;/* Low address of prog load area	*/
		XADDR	pgtop;	/* High address of prog load area, +1	*/
		XADDR	bpaddr;	/* Address of basepage; return value	*/
		XADDR	stackptr;/* Stack ptr of user; return value	*/
		short 	flags;	/* Loader control flags; return value	*/
	} mylpb;

struct ustack			/* User's initial stack - nonsegmented  */
	{
		short	two;	/* "Return address" (actually address   */
				/*  of warm boot call in user's startup)*/
		short	bpoffset;/* Pointer to basepage			*/
	};

struct sstack			/* User's initial stack - segmented     */
	{
		XADDR	stwo;	/* "Return address" (actually address   */
				/*  of warm boot call in user's startup)*/
		XADDR	sbpadr; /* Pointer to basepage			*/
	};


struct m_rt {			/* The Memory Region Table		*/
	int count;
	struct {
		XADDR	tpalow;
		XADDR	tpalen;
	} m_reg[NREGIONS];
};

#define SPREG	1		/* The MRT region for split I/D programs */
#define NSPREG	0		/* The MRT region for non-split programs */
#define SDREG	2		/* The MRT region for split I/D data     */
#define NSDREG	0		/* The MRT region for non-split data     */

#define READ	20		/* Read Sequential BDOS call		*/
#define SETDMA	26		/* Set DMA Address BDOS call		*/
extern UWORD	bdos();		/* To do I/O into myself (note this	*/
				/*   function does not map 2nd param -	*/
				/*   see mbdos macro below)		*/

static XADDR	textloc,	/* Physical locations of pgm sections.	*/
		dataloc,
		bssloc,
		stkloc;

static XADDR	textsiz,	/* Sizes of the various sections.	*/
		datasiz,
		bsssiz,
		stksiz;

static XADDR	rsvd;		/* TPA bytes held by resident RSXes	*/

static UWORD	split,		/* Tells if split I/D or not		*/
		seg;		/* Tells if segmented or not		*/

static char	*gp;		/* Buffer pointer for char input	*/
static char	*mydma;		/* Local address of read buffer		*/

struct x_hdr	x_hdr;		/* Object File Header structure		*/
struct x_sg	x_sg[NSEG];	/* Segment Header structure		*/

static XADDR	segsiz[NSEG];	/* Segment lengths 			*/
static XADDR	seglim[NSEG];	/* Segment length limits		*/
static XADDR	segloc[NSEG];	/* Segment base physical addresses	*/

static short	textseg,	/* Logical seg # of various segments	*/
		dataseg,
		bssseg,
		stkseg;


		/********************************/
		/*				*/
		/* Start of pgmld function	*/
		/*				*/
		/********************************/

UWORD pgmld(xlpbp)			/* Load a program from LPB info */
XADDR xlpbp;
{	register int	i,j;		/* Temporary counters etc.	*/
	struct m_rt	*mrp;		/* Pointer to a MRT structure	*/
	char		mybuf[SECLEN];	/* Local buffer for file reading*/

					/* get local LPB copy		*/
	cpy_in(xlpbp, &mylpb, (long) sizeof mylpb);

	mydma = mybuf;			/* Initialize addr for local DMA*/
	gp = &mybuf[SECLEN];		/* Point beyond end of buffer	*/

	mrp = (struct m_rt *) bgetseg();/* Get address of memory region */
					/*   table (note segment # lost)*/
	if (readhdr() == EOF)		/* Get x.out file header	*/
		return (READERR);	/* Read error on header		*/

	/* C900: a GENCOM'd file carries its modules ahead of the program,
	   and they are attached before it is placed, so the fence is
	   already down when the segment limits below are computed.  That
	   is v3's order: rsxf1 moves every RSX to high memory
	   (loader3.asm:216-231) and comfile moves the program afterwards
	   (:248-267).  x_nseg holds the module count here.  */

		if ((j = ldrsx((int) x_hdr.x_nseg)) != GOOD)
			return (j);
		if (readhdr() == EOF)	/* now the program's own header	*/
			return (READERR);
	}

	rsvd = (XADDR) rsxres();

	{
	  case X_NXN_MAGIC:		/* Non-seg, combined I & D	*/
		split = FALSE;
		seg = FALSE;
		break;

	  case X_SX_MAGIC:		/* Segmented - must be combined	*/
		/* C900: placement is by raw CPU segment number, so every
		   file segment must name the TPA segment the MRT
		   advertises -- checked after the segment headers are
		   read, below */
		split = FALSE;
		seg = SEG;
		break;

	  case X_NXI_MAGIC:		/* Non-seg, separate I & D	*/
		/* C900: the split-I/D loader shim (Option 6).  Code loads
		   into the TPA and runs natively; the D address space
		   loads into the SPLITDSEG bank; after loading, spload()
		   patches every data-referencing instruction to SC #255
		   for the trap handler.  The stack and base page live at
		   the top of the TPA (implicit CALL/PUSH traffic must be
		   native), which the SC handlers honor by routing
		   addresses at or above the user SP to the TPA. */
		split = SPLIT;
		seg = FALSE;
		break;

	  default:
		return (BADHDR);		/* Sorry, can't load it!	*/
	}

/* Set the user space segment number, from the low address in the	*/
/*  appropriate entry of the MRT.					*/
/* m_reg[SPREG] is the region used for split I/D programs in the MRT	*/
/* m_reg[NSPREG] is used for non-split.					*/
/* -1            is used for segmented					*/

/* NOTE -- the tpa limits passed in the LPB are ignored.  This is	*/
/*	   incorrect, but saves the caller from having to look at the	*/
/*	   load module to determine the magic number.			*/

	/* C900: the MRT advertises one region (the TPA); a split load's
	   code bank IS the TPA, so the recorded user segment comes from
	   the non-split region for split programs too (stock indexed
	   m_reg[SPREG], the empty second slot) */
	map_adr(seg ? -1L
		    : (mrp->m_reg[NSPREG].tpalow),
		0xffff);
	
	for (i = 0; i < x_hdr.x_nseg; i++) {	/* For each segment...	*/
		seglim[i] = SEGLEN - rsvd;	/* ...set max length	*/
		segsiz[i] = 0L;			/* ...and current size	*/
	}

	if (seg) {
		/* C900: a segmented program loads at its file segment
		   numbers as raw CPU segments; the only mapped user
		   segment is the TPA, so every x_sg_no must equal the
		   TPA segment of the MRT's non-split region */
		j = (int)(mrp->m_reg[NSPREG].tpalow >> 24) & 0x7f;
		for (i = 0; i < x_hdr.x_nseg; i++)
			if ((x_sg[i].x_sg_no & 0x7f) != j)
				return (BADHDR);
	}

					/* Set section base addresses   */

	textloc = dataloc = bssloc = stkloc = 0L;

					/* Zero section sizes		*/
	textsiz = datasiz = bsssiz = 0L;
	stksiz  = DEFSTACK;


	if (seg) {			/* Locate text & data segments	*/
					/* if segmented we know nothing	*/
		textseg = dataseg = bssseg = stkseg = 0;

		/*  C900: RESERVE THE BASE PAGE AND THE STACK HERE TOO.
		    The non-segmented arm below subtracts them from the
		    data segment's limit; this arm never did.  The only
		    reduction written for a segmented image is loadseg's
		    X_SG_STK case, and it never runs, because lout2cpm
		    emits exactly COD, DAT and BSS and no stack segment
		    (tc/tools/lout2cpm/lout2cpm.c).  So a segmented image
		    whose text+data+bss reached within 0x200 bytes of the
		    ceiling was loaded on top of the base page and the
		    stack that setaddr/setbase were about to write there,
		    and loadseg's overflow test never fired.  seglim is
		    what that test reads, so reserving it here is what
		    turns such an image into a clean NOMEM refusal.  */

		for (i = 0; i < x_hdr.x_nseg; i++)
			seglim[i] -= BPLEN + stksiz;
	} else {			/* if nonsegmented ...		*/
					/* assign segment numbers	*/
		textseg = 0;
		dataseg = (split) ? 1 : 0;
		stkseg = bssseg = dataseg;

					/* assign locations		*/
		segloc[textseg] = map_adr(0L, TPAPROG);
		if (split)
			segloc[dataseg] = SPLITDBASE;
			/* C900: the D space is its own bank, not a
			   map_adr TPA space */

					/* Assign limits		*/
		seglim[textseg] = SEGLEN - rsvd;
		seglim[dataseg] = (split ? (XADDR)SEGLEN
				  : mrp->m_reg[NSDREG].tpalen)
				  - rsvd - BPLEN - stksiz;
			/* C900: stock read m_reg[SDREG=2].tpalen for
			   split -- out of range with NREGIONS = 2; the
			   split data bank is simply a full segment */

					/* Assign stack location	*/
		stkloc = segloc[dataseg] + seglim[dataseg] + stksiz;
	}

	for (i = 0; i < x_hdr.x_nseg; i++)	/* For each segment...	*/
		if( (j = loadseg(i)) != GOOD)	/* ...load memory. If	*/
			return (j);		/* error return, pass	*/
						/* it back.		*/

	if (split) {
		/* C900 split I/D: the stack and base page go at the top
		   of the CODE bank (loadseg computed a data-bank stkloc);
		   then scan-and-patch the loaded text.  Text is capped
		   so the side table and its scratch share one segment,
		   which also guarantees stack headroom above the code. */
		if (textsiz > (XADDR)SPLITMAXT)
			return (NOMEM);
		stkloc = TPABASE + (SEGLEN - rsvd - BPLEN);
		spload((UWORD)textsiz);
	}
	spflag = (split != 0);

	setbase(setaddr(&mylpb));		/* Set addresses in LPB,*/
						/* Set up base page	*/
	cpy_out((XADDR) &mylpb, xlpbp, sizeof mylpb);
	return (GOOD);
}


/* Macro to call BDOS.  First parameter is passed unchanged, second	*/
/* is cast into an XADR, then mapped to caller data space.		*/

#define mbdos(func, param) (bdos((func), map_adr((XADDR) (param), MYDATA)))


/* Macro to read the next character from the input file (much faster	*/
/*   than having to make a function call for each byte)			*/

#define fgetch() ((gp<mydma+SECLEN) ? (int)*gp++&0xff : fillbuf())
			/* C900: was fillbuff() -- the function below is
			   fillbuf(); the original spelling never linked */


/* Routine to fill input buffer when fgetch macro detects it is empty	*/


int fillbuf()				/* Returns first char in buffer */
{					/*   or EOF if read fails	*/
					/* Set up address to read into	*/
	mbdos(SETDMA, mydma);
	if (bdos(READ, mylpb.fcbaddr) != 0)	/* Have BDOS do the read*/
		return (EOF);
	gp = mydma;			/* Initialize buffer pointer	*/
	return ((int)*gp++ & 0xff);	/* Return first character	*/
}

/* C900: transfer `l' bytes of the input file to `dest'.  This is
   loadseg's copy loop without its read-straight-to-target case, which
   needs the target to be a whole record and an RSX module is not.	*/

MLOCAL int rsxfer(dest, l)
XADDR dest;
register UWORD l;
{
	register UWORD	length;

	while (l) {
		if (gp >= mydma + SECLEN) {
			mbdos(SETDMA, mydma);
			if (fillbuf() == EOF)
				return (READERR);
			gp = mydma;	/* fillbuf consumed the first byte */
		}
		length = min(l, mydma + SECLEN - gp);
		cpy_out(gp, dest, length);
		gp += length;
		dest += length;
		l -= length;
	}
	return (GOOD);
}



MLOCAL int ldrsx(n)
register int n;
{
	UBYTE		pfx[RSXHDRLEN];
	register int	i, k;
	register UWORD	org, len;

	for (i = sizeof (struct x_hdr); i < GCHDRLEN; i++)
		if (fgetch() == EOF)	/* the descriptor table		*/
			return (READERR);

	while (--n >= 0) {
		for (i = 0; i < RSXHDRLEN; i++) {
			if ((k = fgetch()) == EOF)
				return (READERR);
			pfx[i] = (UBYTE) k;
		}
		/* UBYTE is a SIGNED char (stdio.h ALCYON), so every byte
		   of a word must be masked before shifting.	*/
		org = (UWORD)(((pfx[0x1c] & 0xff) << 8) | (pfx[0x1d] & 0xff));
		len = (UWORD)(((pfx[0x1e] & 0xff) << 8) | (pfx[0x1f] & 0xff));
		if (len < RSXHDRLEN)
			return (BADHDR);
		if (rsxchk(org, len) != 0)
			return (NOMEM);
		cpy_out(pfx, TPABASE + (long) org, (long) RSXHDRLEN);
		if ((k = rsxfer(TPABASE + (long) org + (long) RSXHDRLEN,
				(UWORD)(len - RSXHDRLEN))) != GOOD)
			return (k);
		for (i = (int)(len & (SECLEN - 1)); i != 0 && i < SECLEN; i++)
			if (fgetch() == EOF)	/* the record padding	*/
				return (READERR);
		if (rsxlink(org, len) != 0)
			return (BADHDR);
	}
	return (GOOD);
}


/* Routine to read the file header */

int readhdr()
{
	register int n, k;
	register char *p;

	p = (char *) &x_hdr;
	for (n = 0; n < sizeof (struct x_hdr); n++) {
		if( (k = fgetch()) == EOF)
			return (k);
		*p++ = (char) k;
	}
	return (GOOD);
}

/* Routine to read the header for  segment i */

int readxsg(i)
int i;
{
	register int n, k;
	register char *p;

	p = (char *) &x_sg[i];
	for(n = 0; n < sizeof (struct x_sg); n++) {
		if ( (k = fgetch()) == EOF)
			return (READERR);
		*p++ = (char) k;
	}
	return (GOOD);
}

/* Routine to load segment number i					*/
/* This assumes that the segments occur in load order in the file, and	*/
/* that all initialized data and, in the case of combined I/D programs, */
/* text segments, precede all bss segments.				*/

/* In the case of segmented programs, the stack segment must exist,	*/
/* and all segments are presumed to be of maximum length.		*/
/* Text, data, bss, and stack lengths are sum of lengths of all such	*/
/* segments, and so may be bigger than maximum segment length.		*/

int loadseg(i)
int i;
{
	register UWORD l, length;	/* Total, incremental length	*/
	register int	type;		/* Type of segment loaded	*/
	register short	lseg;		/* logical segment index	*/
	register XADDR	phystarg;	/* physical target load address	*/

	l = x_sg[i].x_sg_len;		/* number of bytes to load	*/
	type = x_sg[i].x_sg_typ;	/* Type of segment		*/

	lseg  = textseg;		/*   try putting in text space	*/

	if (split) {			/* If separate I/D, this may	*/
		switch (type)		/*   be a bad guess		*/
		{
		  case X_SG_CON:	/* Separate I/D: all data goes  */
		  case X_SG_DATA:	/*   in data space		*/
		  case X_SG_BSS:
		  case X_SG_STK:
			lseg  = dataseg;
		}
	}

	if (seg) {			/* If segmented, compute phys.	*/
					/* address of segment		*/

		/* search to see if seg was used already	*/
		/* if so, use the same logical segment index.	*/
		/* (if not, loop ends with lseg == i)		*/

		for (lseg = 0; 
		     x_sg[lseg].x_sg_no != x_sg[i].x_sg_no; 
		     lseg++) ;

		segloc[lseg] = ((long)x_sg[i].x_sg_no) << 24;
	}

	phystarg = segloc[lseg] + segsiz[lseg];	/* physical target addr	*/

	switch (type)			/* Now load data, if necessary	*/
					/* save physical address & size	*/
	{
	  case X_SG_BSS:		/* BSS gets cleared by runtime	*/
					/*   startup.                   */
		stkloc = (phystarg & 0xffff0000L) + SEGLEN - rsvd
							- BPLEN - stksiz;
					/*  ...in case no stack segment	*/
		if (bssloc == 0L) bssloc = phystarg;
		bsssiz += l;
		if ((segsiz[lseg] += l) >= seglim[lseg])
			return (NOMEM);
		return (GOOD);			/* Transfer no data	*/
	
	  case X_SG_STK:			/* Stack segment:	*/
		if (stkloc == 0L) { 		/* if segmented, we now	*/
						/* know where to put	*/
			seglim[lseg] -= BPLEN;	/* the base page	*/
			stkloc = segloc[lseg] + seglim[lseg];
		}

		stkseg = lseg;
		stksiz += l;			/*   adjust size and	*/
		seglim[lseg] -= l;		/*   memory limit	*/
		if (segsiz[lseg] >= seglim[lseg])
			return (NOMEM);
		return (GOOD);			/* Transfer no data	*/
	
	  case X_SG_COD:		/* Pure text segment		*/
	  case X_SG_MXU:		/* Dirty code/data  (better not)*/
	  case X_SG_MXP:		/* Clean code/data  (be sep I/D)*/
		if (textloc == 0L) textloc = phystarg;
		textsiz += l;
		break;

	  case X_SG_CON:		/* Constant (clean) data	*/
	  case X_SG_DAT:		/* Dirty data			*/
		stkloc = (phystarg & 0xffff0000L) + SEGLEN - rsvd
							- BPLEN - stksiz;
					/*  ...in case no stack or 	*/
					/*    bss segments		*/
		if (dataloc == 0L) dataloc = segloc[i];
		datasiz += l;
		break;
	}
						/* Check seg overflow	*/
	if ((segsiz[lseg] += l) >= seglim[lseg])
		return (NOMEM);
						/* load data from file	*/

	/* Following loop is optimized for load speed.  It knows*/
	/*   about three conditions for data transfer:		*/
	/* 1. Data in read buffer:				*/
	/*	Transfer data from read buffer to target	*/
	/* 2. Read buffer empty and more than 1 sector of data  */
	/*    remaining to load:				*/
	/*	Read data direct to target			*/
	/* 3. Read buffer empty and less than 1 sector of data	*/
	/*    remaining to load:				*/
	/*	Fill read buffer, then proceed as in 1 above	*/

	while (l)			/* Until all loaded	*/
	{				/* Data in disk buffer? */
		if (gp < mydma + SECLEN)
		{
			length = min(l, mydma + SECLEN - gp);
			cpy_out(gp, phystarg, length);
			gp += length;
		}
		else if (l < SECLEN)	/* Less than 1 sector	*/
		{			/*   remains to transfer*/
			length = 0;
			mbdos(SETDMA, mydma);
		}
		else			/* Read full sector	*/
		{			/*   into target space  */
			length = SECLEN;
			bdos(SETDMA, phystarg);
		}

		phystarg += length;
		l -= length;
	}

	return (GOOD);
}

/* Routine to set the addresses in the Load Parameter Block		*/
/* Unlike normal CP/M, the original load address is replaced on return	*/
/* by the actual starting address of the program (true Code-space addr)	*/

int setaddr(lpbp)
struct lpb *lpbp;
{
	register int space;

	space = (split) ? TPADATA : TPAPROG;
	lpbp->pgldaddr = (seg) ? textloc : map_adr(textloc, TRUE_TPAPROG);
	lpbp->bpaddr = stkloc;
	lpbp->stackptr = stkloc - (seg? sizeof (struct sstack)
					    : sizeof (struct ustack));
	lpbp->flags = split | seg;
	return (space);
}

/* Routine to set up the base page.  The parameter indicates whether
 * the data and bss should be mapped in code space or in data space.
 */

VOID setbase(space)
int space;
{
	struct b_page	bp;

	if (seg) {
		bp.lcode = textloc;
		bp.ltpa  = 0L;
	} else {
		bp.lcode = bp.ltpa = map_adr(textloc, TRUE_TPAPROG);
	}
				
	bp.htpa = mylpb.stackptr;	/* htpa is where the stack is	*/
	bp.codelen = textsiz;

	bp.ldata = dataloc;
	bp.datalen = datasiz;

	if (bssloc == 0L) bssloc = dataloc + datasiz;
	bp.lbss = bssloc;
	bp.bsslen = bsssiz;

	/*  C900: FREE MEMORY ENDS AT THE STACK POINTER, not at the segment
	    limit.  DRI defines this field as "Length of free memory after
	    bss" (pg/pgmac.tex:60) and puts the user stack at the highest
	    address of the TPA, with "the maximum size of the stack equal to
	    the address of the stack pointer minus the last address of the
	    program" (pg/pgm4f.tex:516-518).  The span a program may use
	    therefore runs from the end of bss up to its own initial SP,
	    which is what htpa already is (set above from mylpb.stackptr).

	    seglim is not that number.  On the segmented path it counted the
	    program's OWN base page and default stack -- 0x208 bytes an
	    0xEE01 program was told it could have and could not -- because
	    nothing reduced the limit for them (see the reservation added in
	    pgmld() above, which now refuses an image that would collide
	    with them; this reports what is left).

	    A SPLIT-I/D program keeps the old form and must: its bss is in
	    the D bank and its stack is in the code bank, so htpa and lbss
	    are offsets in DIFFERENT segments and subtracting one from the
	    other would be meaningless.  Its base page and stack are not in
	    the span being measured at all.  */

	bp.freelen = bp.lbss + bp.bsslen;	/* end of the image	*/
	if (split)
		bp.freelen = seglim[bssseg] - segsiz[bssseg];
	else if (bp.htpa > bp.freelen)
		bp.freelen = bp.htpa - bp.freelen;
	else
		bp.freelen = 0L;	/* no room between bss and the SP */

	cpy_out(&bp, map_adr((long) stkloc, space), sizeof bp);
}
