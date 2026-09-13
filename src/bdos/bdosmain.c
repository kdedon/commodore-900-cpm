

#include "stdio.h"		/* Standard I/O declarations */

#include "bdosdef.h"		/* Type and structure declarations for BDOS */

#include "biosdef.h"		/* Declarations of BIOS functions */

#include "pktio.h"		/* Packet I/O definitions (drive probe) */
#include "boottrace.h"		/* opt-in cold-boot markers (src/bios) */

/*  Declare EXTERN functions */

EXTERN XADDR	setchain();	/* copy a function 47 command line */
EXTERN		warmboot();	/* Warm Boot function 		*/
EXTERN BOOLEAN	constat();	/* Console status		*/
EXTERN UBYTE	conin();	/* Console Input function	*/
EXTERN 		cookdout();	/* Cooked console output routine */
EXTERN UBYTE	rawconio();	/* Raw console I/O		*/
EXTERN		prt_line();	/* Print line until delimiter	*/
EXTERN		cookdrun();	/* Print a run of characters	*/
EXTERN		readline();	/* Buffered console read	*/
EXTERN		seldsk();	/* Select disk			*/
EXTERN BOOLEAN	openfile();	/* Open File			*/
EXTERN UWORD	close_fi();	/* Close File			*/
EXTERN UWORD	search();	/* Search first and next fcns	*/
EXTERN UWORD	dirscan();	/* General directory scanning routine */
EXTERN UWORD	bdosrw();	/* Sequential and Random disk read/write */
EXTERN UWORD	multio();	/* Multi-sector shell around bdosrw	*/
EXTERN BOOLEAN	create();	/* Create file			*/
EXTERN BOOLEAN	delete();	/* Delete file			*/
EXTERN BOOLEAN	rename();	/* Rename file			*/
EXTERN BOOLEAN	set_attr();	/* Set file attributes		*/
EXTERN		getsize();	/* Get File Size		*/
EXTERN		setran();	/* Set Random Record		*/
EXTERN		free_sp();	/* Get Disk Free Space		*/
EXTERN UWORD	flushit();	/* Flush Buffers		*/
EXTERN UWORD	pgmld();	/* Program Load			*/
EXTERN UWORD	setexc();	/* Set Exception Vector		*/
EXTERN		set_tpa();	/* Get/Set TPA Limits		*/
EXTERN		move();		/* general purpose byte mover	*/
EXTERN UWORD	do_phio();	/* physical I/O			*/
EXTERN UBYTE	cpy_bi();	/* copy one byte in from user space */
EXTERN		seterr();	/* record a disk error for the program */
EXTERN		prt_blk();	/* print a character block (fcns 111/112) */
EXTERN UBYTE	serial[];	/* system serial number (fcn 107)	*/
EXTERN UWORD	scb_fn();	/* get/set system control block (fcn 49) */
EXTERN UWORD	parsefn();	/* parse filename (fcn 152)		*/
EXTERN UWORD	rsxfn();	/* call resident system extension (60)	*/
EXTERN UWORD	bdostime();	/* read/set the clock through @DATE	*/
EXTERN UWORD	set_label();	/* set directory label	    (fcn 100)	*/
EXTERN UWORD	get_label();	/* return dir label data    (fcn 101)	*/
EXTERN UWORD	rd_stamps();	/* read file date stamps    (fcn 102)	*/
EXTERN UWORD	trunf();	/* truncate file	    (fcn 99)	*/
EXTERN UWORD	fexists();	/* file$exists: is the name taken?	*/
EXTERN UWORD	ckwild();	/* check$wild: is there a `?' in it?	*/
EXTERN		upd_stamp();	/* write a file's update stamp		*/
EXTERN UBYTE	*scbstampa();	/* address of the SCB @DATE group	*/


/*  Declare "true" global variables; i.e., those which will pertain to the
    entire file system and thus will remain global even when this becomes
    a multi-tasking file system */

GLOBAL UWORD	log_dsk = 0;	/* 16-bit vector of logged in drives */
GLOBAL UWORD	ro_dsk = 0;	/* 16-bit vector of read-only drives */
GLOBAL UWORD	crit_dsk = 0;	/* 16-bit vector of drives in "critical"
				   state.  Used to control dir checksums */
GLOBAL XADDR	tpa_lp;		/* TPA lower boundary (permanent)	*/
GLOBAL XADDR	tpa_lt;		/* TPA lower boundary (temporary)	*/
GLOBAL XADDR	tpa_hp;		/* TPA upper boundary (permanent)	*/
GLOBAL XADDR	tpa_ht;		/* TPA upper boundary (temporary)	*/

/*  Declare the "state variables".  These are globals for the single-thread
    version of the file system, but are put in a structure so they can be
    based, with a pointer coming from the calling process		*/

GLOBAL struct stvars gbls;

struct tempstr
{
      UBYTE	  tempdisk;
      BOOLEAN	  reselect;
      struct fcb *fptr;
      XADDR	  fxptr;		/* xaddr of caller's FCB	*/
      struct fcb  tempfcb;		/* added for memory management	*/
					/* because caller's fcb may not	*/
					/* be directly accessible	*/
};


MLOCAL BOOLEAN drv_ok(dsk)	/* can this drive be selected? */

REG UWORD dsk;
{
    struct iopb selpkt;
    BSETUP

    if (dsk > 15) return(FALSE);
    selpkt.iofcn = sel_info;
    selpkt.devnum = (UBYTE)dsk;
    selpkt.ioflags = 1;		/* probe as an already-logged drive, so a
				   BIOS that initializes media on a first
				   select does not do it for a question */
    do_phio(&selpkt);		/* the BIOS answers NULL for no drive */
    return( selpkt.infop != (struct dph *)NULL );
}


/*  the functions that take an FCB, and the subset of those that write */

#define fcbfunc(f) ( ((f) >= 15 && (f) <= 23) || (f) == 30 \
		     || ((f) >= 33 && (f) <= 36) || (f) == 40 \
#define wrtfunc(f) ( (f) == 16 || (f) == 19 || ((f) >= 21 && (f) <= 23) \
		     || (f) == 30 || (f) == 34 || (f) == 40 \


#define keephi(f) ( (f) == 16 || (f) == 20 || (f) == 21 \
		    || (f) == 33 || (f) == 34 || (f) == 40 )

MLOCAL UBYTE hi_ext;		/* v3's high$ext, for the call in progress */



UWORD xbdos(func,info,infop)	/* C900: renamed from _bdos -- `bdos' is the
				   user-facing shim provided at integration
				   (D3); the trap handler and the shim both
				   call xbdos */
REG WORD func;		/* BDOS function number */
REG UWORD info;		/* parameter as word */
REG XADDR infop;	/* parameter as (segmented) pointer */
{
    REG UWORD rtnval;
    REG UWORD dsk;
    LOCAL struct tempstr temp;
    BSETUP

	BTRACE1("<a>");			/* FIRST xbdos() body entry */
	temp.reselect = FALSE;
	temp.fxptr = infop;
 	rtnval = 0;
	GBL.curfx = (UBYTE)func;	/* named in the fcn 45 message */
	GBL.curfcb = (struct fcb *)NULL;
	GBL.errcode = 0;
	hi_ext = 0;			/* v3's reselectx (bdos30.asm:2975);
					   keephi() functions put it back  */

	if (GBL.errmode && fcbfunc(func))
	{			/* the drive this FCB names */
	    dsk = UBWORD(cpy_bi(func == 18 ? GBL.srchp : infop));
	    dsk = (dsk == '?' || dsk == 0) ? UBWORD(GBL.dfltdsk) : dsk - 1;
	    if ( ! drv_ok(dsk) )
	    {
		seterr(4, dsk);		/* invalid drive */
		return(0x04ff);
	    }
	    if ( wrtfunc(func) && (ro_dsk & (1 << dsk)) )
	    {
		seterr(2, dsk);		/* read-only disk */
		return(0x02ff);
	    }
	}

	switch (func)	/* switch on function number */
	{
	  case 0:   warmboot(0);		/* warm boot function */
		    /* break; */

	  case 1:   return((UWORD)conin());	/* console input function */
		    /* break; */

	  case 2:   cookdout((UBYTE)info,FALSE);/* "cooked" console output */
		    break;

	  case 3:   return((UWORD)brdr());	/* get reader from bios */
		    /* break; */

	  case 4:   bpun((UBYTE)info);		/* punch output to bios */
		    break;

	  case 5:   blstout((UBYTE)info);	/* list output from bios */
		    break;

	  case 6:   return((UWORD)rawconio(info)); /* raw console I/O */
		    /* break; */

	  case 7:   return(bgetiob());		/* get i/o byte */
		    /* break; */

	  case 8:   bsetiob(info);		/* set i/o byte function */
		    break;

	  case 9:   uprt_line(infop);		/* print line function */
		    break;

	  case 10:  ureadline(infop);		/* read buffered con input */
		    break;

	  case 11:  return((UWORD)constat());	/* console status */
		    /* break; */

	  case 12:  return(VERSION);		/* return version number */
		    /* break; */

	  case 13:  log_dsk = 0;		/* reset disk system */
		    ro_dsk  = 0;
		    crit_dsk= 0;
		    GBL.curdsk = 0xff;
		    GBL.dfltdsk = 0;
		    BTRACE1("<b>");	/* FIRST fn 13 body completion */
		    break;

	  case 14:  if (GBL.errmode && ! drv_ok(info & 0xff))
		    {				/* select disk */
			seterr(4, info & 0xff);
			return(0x04ff);
		    }
		    seldsk((UBYTE)info);
		    GBL.dfltdsk = (UBYTE)info;
		    break;

	  case 15:  tmp_sel(&temp);		/* open file */
		    if ( ckwild(temp.fptr, 0) )	/* check$wild (:3924)	*/
		    {
			rtnval = 0xff;
			break;
		    }
		    temp.fptr->extent = 0;
		    temp.fptr->s2 = 0;
		    rtnval = dirscan(openfile, temp.fptr, 0);
		    if ( rtnval == 255 && GBL.user != 0 && temp.reselect )
		    {		/* search$user0 (bdos30.asm:3940-3974) */
			temp.fptr->drvcode = 0;
			temp.fptr->extent = 0;
			temp.fptr->s2 = 0;
			rtnval = dirscan(openfile, temp.fptr, 0);
			if (rtnval != 255)
			{	/* only a SYS file is shared out of user 0
				   (bdos30.asm:4006-4015: t2' bit 7 clear
				   turns the successful open back into a
				   failure) */
			    if ( (temp.fptr->ftype[1]) & 0x80 )
				hi_ext = 0x80;
			    else
				rtnval = 255;
			}
		    }
		    break;

	  case 16:  tmp_sel(&temp);		/* close file */
		    rtnval = close_fi(temp.fptr);
		    break;

	  case 17:  GBL.srchp = infop;		/* search first */
		    tmp_sel(&temp);
		    rtnval = search(temp.fptr, 0, &temp);
		    break;

	  case 18:  infop = GBL.srchp;		/* search next */
		    temp.fxptr = infop;
		    tmp_sel(&temp);
		    rtnval = search(temp.fptr, 1, &temp);
		    break;

	  case 19:  tmp_sel(&temp);		/* delete file */
		    rtnval = dirscan(delete, temp.fptr, 2);
		    break;

	  case 20:  tmp_sel(&temp);		/* read sequential */
		    rtnval = multio(temp.fptr, TRUE, 0);
		    break;

	  case 21:  tmp_sel(&temp);		/* write sequential */
			/* a file reached through the user-0 fallback is
		    upd_stamp(temp.fptr);
		    rtnval = multio(temp.fptr, FALSE, 0);
		    break;

	  case 22:  tmp_sel(&temp);		/* create file */
		    temp.fptr->extent = 0;
		    temp.fptr->s1 = 0;
		    temp.fptr->s2 = 0;
		    temp.fptr->rcdcnt = 0;
			/* Zero extent, S1, S2, rcrdcnt. create zeros rest */
		    if ( ckwild(temp.fptr, 0)	/* check$wild (:4241)	*/
			 || fexists(temp.fptr, 0) )
			/* `call open' then the extent test (:4248-4258):
			   a make onto a name that is already there is
			   error 8, not a second directory entry	*/
		    {
			rtnval = 0xff;
			break;
		    }
		    rtnval = dirscan(create, temp.fptr, 8);
		    break;

	  case 23:  tmp_sel(&temp);		/* rename file */
		    if ( ckwild(temp.fptr, 1)	/* check$wild BOTH names
						   (:1803 and :1817)	*/
			 || fexists(temp.fptr, 16) )
			/* the search at :1820-1821: the new name must not
			   already exist	*/
		    {
			rtnval = 0xff;
			break;
		    }
		    rtnval = dirscan(rename, temp.fptr, 2);
		    break;

	  case 24:  return(log_dsk);		/* return login vector */
		    /* break; */

	  case 25:  return(UBWORD(GBL.dfltdsk)); /* return current disk */
		    /* break; */

	  case 26:  GBL.dmaadr = infop;		/* set dma address */
		    break;

	  /* No function 27 -- Get Allocation Vector */

	  case 28:  ro_dsk |= 1<<GBL.dfltdsk;	/* set disk read-only */
		    break;

	  case 29:  return(ro_dsk);		/* get read-only vector */
		    /* break; */

	  case 30:  tmp_sel(&temp);		/* set file attributes */
		    if ( ckwild(temp.fptr, 0) )	/* check$wild (:4445)	*/
		    {
			rtnval = 0xff;
			break;
		    }
		    rtnval = dirscan(set_attr, temp.fptr, 2);
		    break;

	  case 31:  if (GBL.curdsk != GBL.dfltdsk) seldsk(GBL.dfltdsk);
		    cpy_out( (GBL.parmp), infop, sizeof *(GBL.parmp) );

		    /* break; */

	  case 33:  tmp_sel(&temp);		/* random read */
		    rtnval = multio(temp.fptr, TRUE, 1);
		    break;

	  case 34:  tmp_sel(&temp);		/* random write */
			/* a file reached through the user-0 fallback is
		    upd_stamp(temp.fptr);
		    rtnval = multio(temp.fptr, FALSE, 1);
		    break;

	  case 35:  tmp_sel(&temp);		/* get file size */
		    getsize(temp.fptr);
		    break;

	  case 36:  tmp_sel(&temp);		/* set random record */
		    setran(temp.fptr);
		    break;

	  case 37:  info = ~info;		/* reset drive */
		    log_dsk &= info;
		    ro_dsk  &= info;
		    crit_dsk&= info;
		    break;

	  case 40:  tmp_sel(&temp);		/* write random with 0 fill */
			/* a file reached through the user-0 fallback is
		    upd_stamp(temp.fptr);
		    rtnval = multio(temp.fptr, FALSE, 2);
		    break;

	  /* Record lock and unlock.  CP/M 3 outside MP/M defines both as
	     func$ret (bdos30.asm:4594-4596) -- the call succeeds and does
	     nothing, because there is nothing to lock against on a
	     single-tasking system.  Programs written for MP/M therefore
	     run here unchanged, which is the whole point of the pair. */
	  case 42:				/* lock record	 */
	  case 43:  break;			/* unlock record */

	  case 44:  rtnval = info & 0xff;	/* set multi-sector count */
		    if (rtnval == 0 || rtnval > 128) return(0xff);
		    GBL.multcnt = (UBYTE)rtnval;
		    rtnval = 0;
		    break;

	  case 45:  GBL.errmode = (UBYTE)info;	/* set BDOS error mode */
		    break;

	  case 46:  if (GBL.errmode && ! drv_ok(info & 0xff))
		    {				/* get disk free space */
			seterr(4, info & 0xff);
			return(0x04ff);
		    }
		    free_sp(info);
		    break;

					/* chain to program: the line is
					   COPIED, not pointed at -- see
					   setchain() in conbdos.c */
		    warmboot(0);		/* terminate calling program */
		    /* break; */

	  case 48:  rtnval = flushit();		/* flush buffers */
		    break;

		  /* Get/set the system control block.  The image is not
		     addressable from a program here (the BDOS data segment
		     is SYS-only), so this function is the whole of the
		     SCB interface -- see sys/scb.c. */
	  case 49:  return(scb_fn(infop, info));
		    /* break; */

	  case 59:  rtnval = pgmld(infop);	/* program load */
		    break;

		    break;

	  case 61:  return(setexc(infop));	/* set exception vector */
		    /* break; */

	  case 63:  set_tpa(infop);		/* get/set TPA limits */
		    break;

		  /*  Truncate file: shorten it to the record its random
		      record field names.  The whole of CP/M 3's batch
		      handling rests on this -- its CCP consumes a submit
		      file by reading the last record and truncating it
		      away (ref/cpm3/ccp3.asm:517-537) -- and nothing else
		      in this BDOS can shrink a file: close() writes an FCB
		      back only when it describes a LARGER one.	*/
	  case 99:  tmp_sel(&temp);		/* truncate file	*/
		    rtnval = trunf(temp.fptr);
		    break;

	  case 98:  rtnval = flushit();		/* free blocks */
		    log_dsk = 0;	/* drop every allocation vector: the
					   next select rebuilds it from the
					   directory, which is what returns
					   blocks allocated to files nobody
					   closed */
		    GBL.curdsk = 0xff;
		    break;


	  case 100: tmp_sel(&temp);		/* set directory label	*/
		    rtnval = set_label(temp.fptr);
		    break;

	  case 101: return(get_label(info & 0xff));
		    /* break; */

	  case 102: tmp_sel(&temp);		/* read file date stamps */
		    rtnval = rd_stamps(temp.fptr);
		    break;


		  /*  Fn 104 stores the caller's four bytes in @DATE and
		      pushes them down to the clock, zeroing @SEC on the
		      way (ref/cpm3/bdos30.asm:5027-5031).  A machine with
		      no clock is not an error to the program: the SCB is
		      still updated, so the time it set is the time this
		      system stamps with.			*/
	  case 104: cpy_in(infop, scbstampa(), 4);
		    (scbstampa())[4] = 0;
		    bdostime(1);
		    break;

		  /*  Fn 105 refreshes @DATE from the clock, copies the
		      four bytes out and returns BCD seconds
		      (ref/cpm3/bdos30.asm:5039-5050).  When the clock
		      does not answer, @DATE keeps whatever function 104
		      last put there, which is v3's behaviour too.  */
	  case 105: bdostime(0);
		    cpy_out(scbstampa(), infop, 4L);
		    return( UBWORD((scbstampa())[4]) );
		    /* break; */

	  case 107: cpy_out(serial, infop, 6L);	/* return serial number */
		    break;

	  case 108: if (info == 0xffff)		/* get/set return code */
			return(GBL.retcode);
		    GBL.retcode = info;
		    break;

	  case 109: if (info == 0xffff)		/* get/set console mode */
			return(GBL.conmode);
		    GBL.conmode = info;
		    break;

	  case 110: if (info == 0xffff)		/* get/set fcn 9 delimiter */
			return(UBWORD(GBL.delim));
		    GBL.delim = (UBYTE)info;
		    break;

	  case 111:				/* print block to console */
	  case 112: prt_blk(infop, func == 111);/* print block to list	*/
		    break;

	  case 152: return(parsefn(infop));	/* parse filename	*/
		    /* break; */

		    /* break; */

	};					/* end of switch statement */

	if (temp.reselect){          /* if reselected disk, restore it now */
		temp.fptr->drvcode = temp.tempdisk;
		temp.fptr->fname[7] |= hi_ext;
			/* v3's goback: fcb(8) = fcb(8) | high$ext
		cpy_out(temp.fptr, infop, sizeof *temp.fptr);
	}

	if (GBL.errcode)	/* a disk error the program asked to handle */
		return( (UBWORD(GBL.errcode) << 8) | 0xff );

	return(rtnval);			/* return the BDOS return value */
}					/* end xbdos */



MLOCAL UWORD wildname(p)	/* chk$wild (bdos30.asm:1775-1778) */

REG UBYTE *p;			/* the 11 name-and-type bytes	*/
{
    REG WORD i;

    for (i = 0; i < 11; i++)
	if ( (UBWORD(p[i]) & 0x7f) == '?' ) return(TRUE);
    return(FALSE);
}


UWORD ckwild(fcbp, both)

REG struct fcb *fcbp;
REG WORD both;			/* also check the name at FCB+16 */
{
    BSETUP

    if ( wildname(&(fcbp->fname[0]))
	 || (both && wildname((UBYTE *)&(fcbp->dskmap.small[1]))) )
    {
	seterr(9, UBWORD(GBL.curdsk));	/* set$aret, bdos30.asm:1774 */
	return(TRUE);
    }
    return(FALSE);
}


/*****************************************************
**
** tmp_sel(temptr) -- temporarily select disk
**		      pointed to by temptr->fptr.
**
**	make local copy of FCB in caller's space.
**
*****************************************************/

tmp_sel(temptr)			/* temporarily select disk pointed to by fcb */
				/* also copy fcb into temp structure         */
REG struct tempstr *temptr;
{
    REG struct fcb *fcbp;
    REG UBYTE tmp_dsk;
    BSETUP

				/* get local copy of caller's FCB, */
				/* and point temptr->fptr at it    */

    cpy_in(temptr->fxptr, &temptr->tempfcb, sizeof(struct fcb));
    temptr->fptr = &temptr->tempfcb;

				/* get local copy of fcb pointer */
    fcbp = temptr->fptr;
    GBL.curfcb = fcbp;		/* named in the fcn 45 error message */

				/* select disk if necessary	 */
    tmp_dsk = fcbp->drvcode;
    if (tmp_dsk == '?') {	/* -- drive '?' for search	 */
	seldsk( GBL.dfltdsk);
    } else {			/* -- drive 0 or disk+1		 */
	temptr->tempdisk = tmp_dsk;
	seldsk( tmp_dsk ? tmp_dsk - 1 : GBL.dfltdsk );

	if ( keephi(GBL.curfx) )
	{			/* v3's reselect: lift the user-0 flag out
				   of the name and work without it	*/
	    hi_ext = (fcbp->fname[7]) & 0x80;
	    fcbp->fname[7] &= 0x7f;
	}
	fcbp->drvcode = hi_ext ? 0 : GBL.user;
	temptr->reselect = TRUE;
    }
}


/*****************************************************
**
** uprt_line(ptr) -- print line in user space
** ureadline(ptr) -- read line into user space
**
**	The pointer parameter is passed as a long,
**	since it may be in the user's memory space.
**
*****************************************************/

#define UPRTCHUNK 32	/* bytes pulled from user space per cpy_in() call */

uprt_line(ptr)
XADDR ptr;
{
	UBYTE	buf[UPRTCHUNK];
	REG UWORD i;
	BOOLEAN done;
	BSETUP

	/* Pull the string across in chunks with one cpy_in() apiece instead
	   of one mem_cpy() per byte -- glue.s's bdcall is a direct call now,
	   not a trap, but it is still a call, so this still cuts the count
	   by roughly UPRTCHUNK.  Function 9 strings run to GBL.delim with
	   no declared length limit, so the chunk size is arbitrary and we
	   just keep pulling chunks until the delimiter turns up in one.  */

	do
	{
	    cpy_in(ptr, buf, UPRTCHUNK);
	    ptr += UPRTCHUNK;
	    for (i = 0; i < UPRTCHUNK; i++)
		if (buf[i] == GBL.delim) break;
	    done = (i < UPRTCHUNK);
	    if (i != 0) cookdrun(buf, i);
	} while (!done);
}


ureadline(ptr)
XADDR ptr;
{
	char buf[258];

	cpy_in(ptr, buf, 1);			/* copy in user's buffer */

	readline(buf);				/* read line		 */

	cpy_out(buf, ptr, 2+(255&(int)buf[1]));	/* copy out result	 */
}

