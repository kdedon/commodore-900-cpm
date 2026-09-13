

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
EXTERN UWORD	ckpass();	/* chk$password: may this caller do it?	*/
EXTERN UWORD	wr_xfcb();	/* fn 103: write or update a file's XFCB */
EXTERN UWORD	set_dfltpw();	/* fn 106: the default password		*/
EXTERN UWORD	mk_xfcb();	/* write a password XFCB for a new file	*/
EXTERN		del_xfcb();	/* erase a name's password XFCBs	*/
EXTERN		ren_xfcb();	/* carry them across a rename		*/
EXTERN UWORD	pwmode;		/* the mode of the XFCB that refused	*/
EXTERN		upd_stamp();	/* write a file's update stamp		*/
EXTERN UBYTE	*scbstampa();	/* address of the SCB @DATE group	*/
EXTERN		scbccpflg();	/* OR bits into the SCB ccp$flgs (fcn 47) */


/*  Declare "true" global variables; i.e., those which will pertain to the
    entire file system and thus will remain global even when this becomes
    a multi-tasking file system */

GLOBAL UBYTE	kbchar[CONBUFS];/* one byte of type-ahead PER CONSOLE,
				   indexed by concur (bdosdef.h).  bss-zero
				   is "nothing buffered" on every console,
				   so nothing has to initialise it	*/
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

/*  Function 103 belongs in both groups: it takes an FCB through
    tmp_sel() and it WRITES a directory entry.  Left out of them it was
    the one directory write with no read-only-drive check in front of
    it, and nothing deeper stops one -- dskutil.c calls error(4) on a
    read-only drive and then does the write anyway, which only the
    default error mode survives, because there error(4) aborts and never
    returns.  So in function 45 modes 0FEh and 0FFh function 103 wrote
    an XFCB onto a drive the program itself had marked read-only.	*/

#define fcbfunc(f) ( ((f) >= 15 && (f) <= 23) || (f) == 30 \
		     || ((f) >= 33 && (f) <= 36) || (f) == 40 \
		     || (f) == 99 || (f) == 100 || (f) == 102 \
		     || (f) == 103 )
#define wrtfunc(f) ( (f) == 16 || (f) == 19 || ((f) >= 21 && (f) <= 23) \
		     || (f) == 30 || (f) == 34 || (f) == 40 \
		     || (f) == 99 || (f) == 100 || (f) == 103 )


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
    REG UWORD newpw;		/* fn 22: f6', assign a password	*/
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
	xfcb_ro = 0;			/* and :2978, the line after	*/

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
		    if ( ckpass(temp.fptr, 0) )
			/*  bdos30.asm:4026-4057.  The three modes are not
			    three refusals: READ protection refuses the
			    open, WRITE protection lets it through and
			    makes the file read-only, and DELETE
			    protection -- the weakest -- does not concern
			    an open at all.  v3 tests the same two bits
			    of pw$mode in the same order (:4054-4057). */
		    {
			if (pwmode & XP_READ)
			{
			    seterr(7, UBWORD(GBL.curdsk));
			    rtnval = 0xff;
			    break;
			}
			if (pwmode & XP_WRITE) xfcb_ro = 0x80;
		    }
		    temp.fptr->extent = 0;
		    temp.fptr->s2 = 0;
		    rtnval = dirscan(openfile, temp.fptr, 0);
		    if ( rtnval == 255 && GBL.user != 0 && temp.reselect )
		    {		/* search$user0 (bdos30.asm:3940-3974) */
			temp.fptr->drvcode = 0;
			temp.fptr->extent = 0;
			temp.fptr->s2 = 0;
			if ( ckpass(temp.fptr, 0) )
			{   /*  the SECOND check, and it has to be a second
				one: an XFCB belongs to a user area (its
				entry type is 10h + the user number), so
				the check above probed the CALLER's area
				and found nothing there.  Without this a
				read-protected user-0 SYS file opened from
				every other user area with no password --
				the first P2 item of the first-release
				review.  Ordered before the scan so a
				refusal writes nothing, not even an access
				stamp, and read the same way as above:
				READ refuses, WRITE opens read-only,
				DELETE does not concern an open.	*/
			    if (pwmode & XP_READ)
			    {
				seterr(7, UBWORD(GBL.curdsk));
				rtnval = 0xff;
				break;
			    }
			    if (pwmode & XP_WRITE) xfcb_ro = 0x80;
			}
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
		    if ( ckpass(temp.fptr, 0) )
			/*  bdos30.asm:1638-1650.  ANY password protects a
			    file from erasure -- delete protection is the
			    weakest of the three and the other two include
			    it -- so unlike open there is no mode to look
			    at, only a yes or a no.  A wildcarded erase is
			    refused if any one of the files it names is
			    protected: ckpass() scans them all.	*/
		    {
			seterr(7, UBWORD(GBL.curdsk));
			rtnval = 0xff;
			break;
		    }
		    if ( UBWORD(temp.fptr->fname[5]) & 0x80 )
			del_xfcb(temp.fptr, 0);
			rtnval = 0;
			break;
		    }
		    rtnval = dirscan(delete, temp.fptr, 2);
		    if (rtnval != 255) del_xfcb(temp.fptr, 0);
			/* the XFCB goes with the file it named (:1690):
			   left behind it would hand its password to the
			   NEXT file created under that name	*/
		    break;

	  case 20:  tmp_sel(&temp);		/* read sequential */
		    rtnval = multio(temp.fptr, TRUE, 0);
		    break;

	  case 21:  tmp_sel(&temp);		/* write sequential */
		    if (hi_ext || xfcb_ro)
			/* a file reached through the user-0 fallback is
			   read-only, error 3 through set$aret -> 03FFh
			   plus the "Read/Only File" message
			   (bdos30.asm:2485-2494) -- and so is one opened
			   without the password its XFCB wanted for
			   writing, which is the same test one line above
			   at :2481	*/
		    {
			seterr(3, UBWORD(GBL.curdsk));
			break;
		    }
		    upd_stamp(temp.fptr);
		    rtnval = multio(temp.fptr, FALSE, 0);
		    break;

	  case 22:  tmp_sel(&temp);		/* create file */
		    newpw = (temp.fptr->fname[5]) & 0x80;
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
		    if (rtnval != 255 && newpw && mk_xfcb(temp.fptr))
			/* the XFCB would not fit.  v3 undoes the make it
			   just did and answers 0FFh (:4324-4329): a file
			   that was asked to be protected and is not must
			   not be left lying there unprotected	*/
		    {
			dirscan(delete, temp.fptr, 2);
			rtnval = 0xff;
		    }
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
		    if ( ckpass(temp.fptr, 0) )
			/* chk$password on the name being renamed
			   (bdos30.asm:1806-1807).  A rename is a delete
			   and a make of the name, so any password
			   protects it	*/
		    {
			seterr(7, UBWORD(GBL.curdsk));
			rtnval = 0xff;
			break;
		    }
		    rtnval = dirscan(rename, temp.fptr, 2);
		    if (rtnval != 255) ren_xfcb(temp.fptr);
			/* :1825 and :1855-1860: the new name's own XFCB
			   goes, and the old name's XFCB comes along	*/
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
		    if ( ckpass(temp.fptr, 0) )
			/* `indicators' checks the password before it
			   changes a single attribute (:1866-1868).  A
			   file's R/O, SYS and archive bits are as much
			   the owner's business as its contents	*/
		    {
			seterr(7, UBWORD(GBL.curdsk));
			rtnval = 0xff;
			break;
		    }
		    rtnval = dirscan(set_attr, temp.fptr, 2);
		    break;

	  case 31:  if (GBL.curdsk != GBL.dfltdsk) seldsk(GBL.dfltdsk);
		    cpy_out( (GBL.parmp), infop, sizeof *(GBL.parmp) );

	  case 32:  /* get/set user number.  E = 0FFh interrogates; anything
		       else is masked ani 0fh and SET, and the call
		       returns 0 (bdos30.asm:4458-4472)	*/
		    if ( (info & 0xff) == 0xff ) return(UBWORD(GBL.user));
		    GBL.user = (UBYTE)(info & 0x0f);
		    return(0);
		    /* break; */

	  case 33:  tmp_sel(&temp);		/* random read */
		    rtnval = multio(temp.fptr, TRUE, 1);
		    break;

	  case 34:  tmp_sel(&temp);		/* random write */
		    if (hi_ext || xfcb_ro)
			/* a file reached through the user-0 fallback is
			   read-only, error 3 through set$aret -> 03FFh
			   plus the "Read/Only File" message
			   (bdos30.asm:2485-2494) -- and so is one opened
			   without the password its XFCB wanted for
			   writing, which is the same test one line above
			   at :2481	*/
		    {
			seterr(3, UBWORD(GBL.curdsk));
			break;
		    }
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

	  /* Functions 38 and 39 are MP/M-only (get/set the process's own
	     descriptor address, and get/set the caller's process
	     priority).  Outside MP/M, v3's table entry for both is
	     func$ret -- the call succeeds and returns 0
	     (bdos30.asm:4578-4579) -- rather than an unimplemented-
	     function refusal.	*/
	  case 38:
	  case 39: return(0);
		    /* break; */

	  /* Function 41 (get/set outer environment) is likewise not a
	     CP/M 3 BDOS call outside MP/M; its table slot is
	     lret$eq$ff, so the answer is 00FFh, not the unimplemented-
	     function 0FFFFh (cpmbdos1.asm:322).	*/
	  case 41: return(0x00ff);
		    /* break; */

	  case 40:  tmp_sel(&temp);		/* write random with 0 fill */
		    if (hi_ext || xfcb_ro)
			/* a file reached through the user-0 fallback is
			   read-only, error 3 through set$aret -> 03FFh
			   plus the "Read/Only File" message
			   (bdos30.asm:2485-2494) -- and so is one opened
			   without the password its XFCB wanted for
			   writing, which is the same test one line above
			   at :2481	*/
		    {
			seterr(3, UBWORD(GBL.curdsk));
			break;
		    }
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

	  case 47:  if ( (info & 0xff) == 0xff )
			/* E = 0FFh additionally asks for the current
			   environment: set bit 40h of ccp$flgs
			   (bdos30.asm:4665-4670)	*/
			scbccpflg(0x40);
		    GBL.chainp = setchain(GBL.dmaadr);
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
		    if ( ckpass(temp.fptr, 0) )
		    {	/*  the same rule as erase (case 19): truncation
			    destroys records, delete protection is the
			    weakest of the three modes and the other two
			    include it, so ANY password protects a file
			    from function 99.  It had none, which meant a
			    protected file could be truncated by anyone. */
			seterr(7, UBWORD(GBL.curdsk));
			rtnval = 0xff;
			break;
		    }
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

	  case 103: tmp_sel(&temp);		/* write file XFCB	*/
		    rtnval = wr_xfcb(temp.fptr);
		    break;

		  /*  Fn 106's eight bytes are at the PARAMETER address,
		      not the DMA -- the one call in this group that does
		      not go through the DMA (bdos30.asm:5066-5076,
		      set.plm:1014).  It is what lets a program open a
		      password-protected file without carrying the
		      password itself, which is the only reason function
		      103 is usable from the console at all.	*/
	  case 106: rtnval = set_dfltpw(infop);	/* set default password	*/
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

	  /*  v3's function table does not stop at the highest number it
	      implements: cpmbdos1.asm:255-262 fills 51-97 and 113-127
	      with lret$eq$ff (00FFh) and, at :348-349, everything from
	      128 up with func$ret (0) -- the CP/M-8000 MP/M/XDOS range,
	      which answers "not present" rather than "bad function
	      number" so a program can probe it.  Only what is truly
	      outside the v3 table (fn 27, structural, G5) keeps the
	      0FFFFh bad-function-number answer.		*/
	  default:  if (func >= 128) return(0);
		    if ( (func >= 51 && func <= 97) ||
			 (func >= 113 && func <= 127) ) return(0x00ff);
		    return(-1);			/* bad function number */
		    /* break; */

	};					/* end of switch statement */

	if (temp.reselect){          /* if reselected disk, restore it now */
		temp.fptr->drvcode = temp.tempdisk;
		temp.fptr->fname[7] |= hi_ext;
		temp.fptr->fname[6] |= xfcb_ro;
			/* v3's goback: fcb(8) = fcb(8) | high$ext
			   (bdos30.asm:5124-5131) and fcb(7) = fcb(7) |
			   xfcb$read$only (:5112-5113) -- the user-0 flag
			   and the password-read-only flag both go home
			   with the caller's FCB	*/
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
	    xfcb_ro = (fcbp->fname[6]) & 0x80;	/* :2989-2990	*/
	    fcbp->fname[6] &= 0x7f;
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

