
/********************************************************
*							*
*		P-CP/M header file 			*
*    Copyright (c) 1982 by Digital Research, Inc.	*
*    Structure definitions for BDOS globals		*
*	and BDOS data structures			*
*							*
********************************************************/

/* gbls holds the running process's BDOS state. proc.c copies it at
   context switches; shared filesystem state is protected by LOCK/UNLOCK. */

#define snglthrd TRUE
/* Keep GBL as direct access to gbls; proc.c saves and restores it per process. */
			/* TRUE for single-thread environment
			undefined to create based structure for re-entrant model */
#ifdef snglthrd
#define GBL gbls 
				/* In single thread case, GBL just names
					the structure */
#define BSETUP  EXTERN struct stvars gbls;
				/* and BSETUP defines the extern structure */
#else

#define GBL (*statep)
				/* If multi-task, state vars are based */
#define BSETUP  EXTERN struct stvars gbls; \
		REG struct stvars *statep; \
	  statep = &gbls;
				/* set up pointer to state variables */
			/* This is intended as an example to show the intent */
#endif






/* The filesystem lock is recursive: do_phio() can acquire it under log_in().
   A competing owner yields; only the outermost UNLOCK releases the lock. */
#define LOCK    plock();
#define UNLOCK  punlock();
EXTERN VOID plock();
EXTERN VOID punlock();

/*  Console type-ahead belongs to the device, not the process producing
    output -- AND THERE IS ONE BUFFER PER DEVICE.  It was a single byte for
    the whole machine, which is a steal and not a race: conbrk() takes a
    character off the PRINTING process's console (bconstat/bconin pass
    concur, biosdef.h) and parks it here, and getch() hands the parked
    character to the first reader on ANY console, before it looks at the

    Indexed by concur.  NOT part of stvars: stvars is per-PROCESS, copied
    wholesale at a switch (proc.c), and type-ahead must survive a switch
    between two processes sharing one console and must NOT follow a
    process moved to another console.  CONBUFS is the ceiling the BIOS's
    own per-console tables use (src/bios/bios900.c CONMAX, and its own
    lookahead pend[] there is this table's mirror one layer down);
    bconcnt() says how many of them are configured, and proc.c refuses
    any console number at or above it.  */
#define CONBUFS 4
EXTERN UBYTE	kbchar[CONBUFS];

 
/* Function 12 encodes Portable CP/M as 0x20 and level 3.1 as 0x31.
   DRI utilities use this value to enable SCB, XFCB and multi-sector calls. */
#define robit 0			/* read-only bit in file type field of fcb */
#define arbit 2			/* archive bit in file type field of fcb   */
#define SECLEN 128		/* length of a CP/M sector		   */


/* CP/M 3 on-disk directory types and label modes. Source: xfcb.lit, dirlbl.asm. */

/* directory entry type byte (byte 0) */
#define DE_XFCB	  0x10		/* 0x10 + user: password XFCB		*/
#define DE_LABEL  0x20		/* directory label, at most one a drive	*/
#define DE_SFCB	  0x21		/* date/time stamps for 3 entries	*/
#define DE_EMPTY  0xe5		/* free slot				*/

/* directory label mode byte (entry byte 12) */
#define DL_PASSWD 0x80		/* passwords enabled on the drive	*/
#define DL_ACCESS 0x40		/* stamp files on open (access)		*/
#define DL_UPDATE 0x20		/* stamp files on write (update)	*/
#define DL_CREATE 0x10		/* stamp files on make (create)		*/
#define DL_STAMPS (DL_ACCESS|DL_UPDATE|DL_CREATE)
#define DL_EXISTS 0x01		/* the directory label exists		*/

/* the label's own two stamps live in its disk-map area */
#define DL_CRSTAMP 24		/* label created			*/
#define DL_UPSTAMP 28		/* label updated			*/

/* XFCBs carry the file name, mode and an eight-byte password stored
 * reversed and XORed with its byte sum. Read protection includes write
 * and delete; write includes delete. Source: ref/cpm3/xfcb.lit. */

#define XF_MODE	  12		/* the password mode byte		*/
#define XF_KEY	  13		/* checksum of the password = XOR key	*/
#define XF_PASS	  16		/* the eight password bytes, reversed	*/
#define PASSLEN	  8		/* a CP/M 3 password is eight bytes	*/

#define XP_READ	  0x80		/* password needed to open at all	*/
#define XP_WRITE  0x40		/* password needed to open for writing	*/
#define XP_DELETE 0x20		/* password needed to erase or rename	*/
#define XP_MODES  (XP_READ|XP_WRITE|XP_DELETE)

/* SFCB geometry inside a 128-byte directory record */
#define SF_MARK	  96		/* byte offset of the SFCB entry	*/
#define SF_SUBLEN 10		/* one sub-record per described entry	*/
#define SF_CREATE 0		/* sub-record: create-or-access stamp	*/
#define SF_UPDATE 4		/* sub-record: update stamp		*/
#define SF_PWMODE 8		/* sub-record: password mode		*/
#define STAMPLEN  4		/* date word + BCD hour + BCD minute	*/

/* fcb s2 bit 6: this open file's update stamp has already been written
   (ref/cpm3/bdos30.asm:2469-2471 set$filewf, tested at :3349)	*/
#define UPDSTAMPED 0x40


union smallbig
{
  UBYTE	small[16];	/* 16 block numbers of 1 byte		*/
  WORD	big[8];		/* or 8 block numbers of 1 word		*/
};

/* File Control Block definition */
struct fcb
{
	UBYTE	drvcode;	/* 0 = default drive, 1..16 are drives A..P */
	UBYTE	fname[8];	/* File name (ASCII)			*/
	UBYTE	ftype[3];	/* File type (ASCII)			*/
	UBYTE	extent;		/* Extent number (bits 0..4 used)	*/
	UBYTE	s1;		/* Reserved				*/
	UBYTE	s2;		/* Module field (bits 0..5), write flag (7) */
	UBYTE	rcdcnt;		/* Nmbr rcrds in last block, 0..128	*/
	union	smallbig dskmap;
	UBYTE	cur_rec;	/* current record field			*/
	UBYTE	ran0;		/* random record field (3 bytes)	*/
	UBYTE	ran1;
	UBYTE	ran2;
};


/* Declaration of directory entry	*/
struct dirent
{
	UBYTE	entry;		/* 0 - 15 for user numbers, E5 for empty */
				/* the rest are reserved		*/
	UBYTE	fname[8];	/* File name (ASCII)			*/
	UBYTE	ftype[3];	/* File type (ASCII)			*/
	UBYTE	extent;		/* Extent number (bits 0..4 used)	*/
	UBYTE	s1;		/* Reserved				*/
	UBYTE	s2;		/* Module field (bits 0..5), write flag (7) */
	UBYTE	rcdcnt;		/* Nmbr rcrds in last block, 0..128	*/
	union	smallbig dskmap;
};


/* Declaration of disk parameter tables		*/
struct dpb			/* disk parameter table		*/
{
	UWORD	spt;		/* sectors per track 		*/
	UBYTE	bsh;		/* block shift factor		*/
	UBYTE	blm;		/* block mask			*/
	UBYTE	exm;		/* extent mask			*/
	UBYTE	dpbdum;		/* dummy byte for fill		*/
	UWORD	dsm;		/* max disk size in blocks	*/
	UWORD	drm;		/* max directory entries	*/
	UWORD	dir_al;		/* initial allocation for dir	*/
	UWORD	cks;		/* number dir sectors to checksum */
	UWORD	trk_off;	/* track offset			*/
};

struct	dph			/* disk parameter header	*/
{
	UBYTE	*xlt;		/* pointer to sector translate table	*/
	UWORD	hiwater;	/* high water mark for this disk	*/
	UWORD	dum1;		/* dummy (unused)			*/
	UWORD	dum2;
	UBYTE	*dbufp;		/* pointer to 128 byte directory buffer	*/
	struct dpb *dpbp;	/* pointer to disk parameter block	*/
	UBYTE	*csv;		/* pointer to check vector		*/
	UBYTE	*alv;		/* pointer to allocation vector		*/
};


/*  Console paging (src/bdos/conbdos.c pagelf).  page$mode is a byte
    whose ZERO means paging is ON, which reads backwards until you see
    that v3 defines it that way (ref/cpm3/ccp3.asm:196) so that the
    cleared byte is the configured system's normal state.  PM_OFF is
    0FFh because that is the value v3's own utilities write into it
    (ref/cpm3/dump.asm:374-380).					*/

#define	PM_ON		0x00	/* pause at the foot of each page	*/
#define	PM_OFF		0xff	/* do not					*/

/*  Lines per page for a caller that wants the system's answer to "how
    long is a page" without inventing one.  24 is the fallback SDIR and
    SET already apply to a zero @CONPAGE (src/cmd/set.c:990), so there is
    one number and not two.  The BDOS's own pager does NOT use it as a
    fallback: a zero @CONPAGE turns that pager off outright (see
    src/bdos/conbdos.c pagelf), because page$mode has to keep v3's
    "paging is on" default for the utilities that read it.		*/

#define	PAGELEN		24

/* Declaration of structure containing "global" state variables */
struct stvars
{
	UBYTE	delim;		/* Delimiter for function 9		   */
	BOOLEAN	lstecho;	/* True if echoing console output to lst:  */
	BOOLEAN echodel;	/* Echo char when getting <del> ?	   */
	UWORD	column;		/* CRT column number for expanding tabs	   */
	XADDR	chainp;		/* Used for chain to program call	   */
	UBYTE	curdsk;		/* Currently selected disk		   */
	UBYTE	dfltdsk;	/* Default disk (last selected by fcn 14)  */
	UBYTE	user;		/* Current user number			   */
	struct dph *dphp;	/* pointer to disk parm hdr for cur disk   */
	struct dirent *dirbufp; /* pointer for directory buff for process  */
				/* stored here so that each process can	   */
				/* have a separate dirbuf.		   */
	struct dpb *parmp;	/* pointer to disk parameter block for cur */
				/* disk. Stored here to save ref calc	   */
	UWORD	srchpos;	/* position in directory for search next   */
	XADDR	dmaadr; 	/* Disk dma address			   */
	XADDR	srchp;		/* Pointer to search FCB for function 17   */
	UBYTE	*excvec[18];	/* Array of exception vectors		   */
	UBYTE	multcnt;	/* Multi-sector count, 1..128 (fcn 44).	   */
				/* Records moved per read/write call by	   */
				/* fcns 20, 21, 33, 34 and 40.		   */
	UBYTE	errmode;	/* BDOS error mode (fcn 45): 0 displays the */
				/* error and terminates the program, 0xff   */
				/* returns it silently, anything else	   */
				/* displays it and returns it		   */
	UBYTE	errcode;	/* Error code for the function in progress, */
				/* 0 = none.  Returned in the high byte	   */
				/* with 0xff in the low byte.		   */
	UBYTE	curfx;		/* Function number in progress		   */
	struct fcb *curfcb;	/* FCB of the function in progress, or NULL */
	UWORD	conmode;	/* Console mode (fcn 109): bit 0 ^C-only    */
				/* status, bit 1 no ^S/^Q, bit 2 raw	   */
				/* output, bit 3 no ^C termination	   */
	UWORD	retcode;	/* Program return code (fcn 108)	   */
	UBYTE	conpage;	/* Console page length in lines (@CONPAGE, */
				/* SCB 1Ch).  0 means "not configured", and */
				/* for the BDOS's own pager that means OFF: */
				/* it is the switch, not page$mode.  See	   */
				/* src/bdos/conbdos.c pagelf.		   */
	UBYTE	conline;	/* Lines printed on the current page	   */
				/* (@CONLINE, SCB 1Dh)			   */
	UBYTE	pagemode;	/* Console page mode (page$mode, SCB 2Ch): */
				/* 0 = pause at the foot of each page,	   */
				/* non-zero = do not.  v3's sense exactly   */
				/* (ref/cpm3/ccp3.asm:196, "0=on, 0ffH=off")*/
	UBYTE	pmdefault;	/* What a warm boot resets pagemode to	   */
				/* (pm$default, SCB 2Dh).  v3's CCP does	   */
				/* this reset per command, ccp3.asm:603-614 */
	WORD	dirown;		/* This rdwrt() is dir_rd's or dir_wr's,   */
				/* so the transfer is INTO pdirbuf and	   */
				/* dirsecn is about to describe it.  It	   */
				/* was a file-scope static in dskutil.c	   */
				/* on the reading that it is set and	   */
				/* cleared inside one rdwrt() and that no   */
				/* switch point lies between -- and one	   */
				/* does: rdwrt() calls error() on an I/O	   */
				/* failure, and error() asks the operator,  */
				/* which is a console read, which is where  */
				/* a process parks (conbdos.c getch).  A	   */
				/* second process entering rdwrt() while	   */
				/* the first waits at that prompt would	   */
				/* read the first process's flag and skip   */
				/* invalidating ITS OWN dirsecn -- and fn   */
				/* 40's zero fill (bdosrw.c) borrows the    */
				/* directory buffer for exactly that	   */
				/* transfer.  Per-process, like dirsecn.    */
	WORD	dirsecn;	/* Directory record now in pdirbuf, or -1. */
				/* It was a file-scope static in dskutil.c  */
				/* and it is here for the same reason	   */
				/* pdirbuf is: it describes THIS process's  */
				/* directory buffer, and once a process can */
				/* park inside a BDOS call two of them are  */
				/* in flight at once.  See below.	   */
	UWORD	dirgen;		/* The value of dskutil.c's SHARED per-drive */
				/* dirwgen[] at the moment pdirbuf was	   */
				/* filled.  dirsecn alone says WHICH record */
				/* this process cached; this says whether   */
				/* anybody has written that record since.   */
				/* Without it a process with cks == 0 -- and */
				/* every C900 drive has cks == 0 -- reuses   */
				/* a record another process has already	   */
				/* changed, and close() writes all 128	   */
				/* bytes of it back over the newer entry.   */
				/* See dirhave() in dskutil.c.		   */
	struct dirent pdirbuf[SECLEN / sizeof (struct dirent)];
				/* THE PER-PROCESS DIRECTORY BUFFER, and   */
				/* the thing DRI's own comment on dirbufp  */
				/* above was holding the door open for.	   */
				/* It is INSIDE stvars on purpose: proc.c  */
				/* moves this whole structure by copy at   */
				/* every switch, so anything that lives in */
				/* here is per-process for free, and	   */
				/* anything that does not is shared.  The  */
				/* BIOS's own 128-byte `dirbuf' (the one	   */
				/* every dph's dbufp points at,		   */
				/* bios900.c:203) is no longer the file	   */
				/* system's scratch; log_in() points	   */
				/* dirbufp here instead (fileio.c).	   */
};

/* console mode bits, function 109 */
#define CM_CTLC   0x0001	/* fcn 11 reports only a waiting ^C	*/
#define CM_NOSTOP 0x0002	/* ^S/^Q stop-scroll disabled		*/
#define CM_RAW    0x0014	/* no tab expansion, no printer echo	*/
#define CM_NOTERM 0x0008	/* ^C does not terminate the program	*/

/* program return codes the BDOS sets when it ends a program itself */
#define RC_CTLC   0xfffe	/* terminated by ^C			*/
#define RC_BDOS   0xfffd	/* terminated by a BDOS error		*/


/* Console buffer structure declaration */
struct	conbuf
{
	UBYTE	maxlen;		/* Maximum length from calling routine */
	UBYTE	retlen;		/* Length actually found by BDOS */
	UBYTE	cbuf[1];	/* Console data			 */
};

