/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/*
 * cpm.h - CP/M-8000 interface for MWC-built transient programs.
 *
 * Programs compiled with the project Z8001 pipeline (cc2 variant VTPA,
 * linked flat at the TPA segment base) run in segmented Normal mode
 * inside the 64K TPA.  All pointers are 32-bit far pointers whose value
 * is the CPU XADDR form (seg<<24 | offset), which is exactly what the
 * BDOS expects as a LONG parameter.  int = 16 bits, long = 32 bits.
 */

/* BDOS function numbers (CP/M 2.2 level, may83 BDOS) */
#define	BDOS_WBOOT	0	/* warm boot; never returns		*/
#define	BDOS_CONIN	1	/* console input, echoed -> char	*/
#define	BDOS_CONOUT	2	/* console output(char)			*/
#define	BDOS_PRINTSTR	9	/* print '$'-terminated string		*/
#define	BDOS_RDCONBUF	10	/* read console buffer			*/
#define	BDOS_CONST	11	/* console status			*/
#define	BDOS_OPEN	15	/* open file(fcb) -> 0..3 ok, 255 fail	*/
#define	BDOS_CLOSE	16	/* close file(fcb)			*/
#define	BDOS_SFIRST	17	/* search first(fcb)			*/
#define	BDOS_SNEXT	18	/* search next				*/
#define	BDOS_DELETE	19	/* delete file(fcb)			*/
#define	BDOS_READSEQ	20	/* read sequential(fcb) -> 0 ok		*/
#define	BDOS_WRITESEQ	21	/* write sequential(fcb) -> 0 ok	*/
#define	BDOS_MAKE	22	/* create file(fcb) -> 255 = full	*/
#define	BDOS_RENAME	23	/* rename: old fcb at 0, new at 16	*/
#define	BDOS_SETATTR	30	/* set file attributes(fcb)		*/
#define	BDOS_SETDMA	26	/* set DMA address			*/
#define	BDOS_READRAN	33	/* random read(fcb)			*/
#define	BDOS_WRITERAN	34	/* random write(fcb)			*/
#define	BDOS_SETRAN	36	/* set random record from cur_rec	*/

/* CP/M 3 additions (V1 wave) */
#define	BDOS_RESET	13	/* reset disk system			*/
#define	BDOS_SELDSK	14	/* select disk (0 = A:)			*/
#define	BDOS_CURDSK	25	/* return current disk			*/
#define	BDOS_GETDPB	31	/* copy out the disk parameter block	*/
#define	BDOS_ROVEC	29	/* return read-only drive vector	*/
#define	BDOS_SETMULTI	44	/* set multi-sector count, 1..128	*/
#define	BDOS_FREESP	46	/* free space on a drive -> 4 bytes to DMA */
#define	BDOS_ERRMODE	45	/* set BDOS error mode			*/
#define	BDOS_FREEBLK	98	/* free temporarily allocated blocks	*/
#define	BDOS_SERIAL	107	/* return 6-byte serial number		*/
#define	BDOS_RETCODE	108	/* get/set program return code		*/
#define	BDOS_CONMODE	109	/* get/set console mode			*/
#define	BDOS_OUTDELIM	110	/* get/set fn 9 output delimiter	*/
#define	BDOS_PRTBLK	111	/* print block to console		*/
#define	BDOS_LSTBLK	112	/* print block to list device		*/

/* CP/M 3 date and time stamping */
#define	BDOS_SETLABEL	100	/* set directory label(fcb)		*/
#define	BDOS_GETLABEL	101	/* return dir label data(drive) -> mode	*/
#define	BDOS_RDSTAMPS	102	/* read file date stamps(fcb) -> DMA	*/
#define	BDOS_SETTIME	104	/* set date and time(4-byte block)	*/
#define	BDOS_GETTIME	105	/* get date and time(4 bytes) -> BCD sec */

/* directory label mode bits, as returned by function 101 */
#define	DL_PASSWD	0x80
#define	DL_ACCESS	0x40
#define	DL_UPDATE	0x20
#define	DL_CREATE	0x10
#define	DL_EXISTS	0x01

/*
 * Function 50, direct BIOS call.  The parameter is the address of a
 * five-word block: the BIOS function code in one word, then two LONG
 * parameters.  The result is a LONG, so it comes back through
 * __bdosl(); 0FFFFFFFFh means the BDOS refused the code.  Which codes
 * are allowed, and why the rest are not, is sys/iosys.c bioscl().
 *
 * C900 deviation: CP/M 3's function 50 takes an 8080 register block
 * (bdos30.asm:4734-4740, "de -> function, a value, bc value, de value,
 * hl value"), which has no meaning on a Z8001.  This block is
 * CP/M-8000's own form and predates the port -- sys/bdosglue.s
 * `bioscall' has decoded it since M4.
 */
#define	BDOS_BIOSCALL	50

struct biospb {
	int	code;		/* BIOS function code			*/
	long	p1;		/* first parameter (BIOS D1)		*/
	long	p2;		/* second parameter (BIOS D2)		*/
};

#define	BIOS_REFUSED	0xffffffffL

#define	BIOS_HOME	8	/* seek track 0				*/
#define	BIOS_SELDSK	9	/* select disk -> dph address, 0 = none	*/
#define	BIOS_SETTRK	10	/* set track				*/
#define	BIOS_SETSEC	11	/* set sector (in 128-byte records)	*/
#define	BIOS_SETDMA	12	/* set DMA address (an XADDR here)	*/
#define	BIOS_READ	13	/* read the selected record -> 0 = ok	*/
#define	BIOS_WRITE	14	/* write it; 1 = directory write-through */
#define	BIOS_SECTRAN	16	/* logical -> physical sector		*/
#define	BIOS_FLUSH	21	/* flush the BIOS buffer cache		*/

/*
 * Segment allocation, BIOS function 25, and it IS on bioscl()'s allowed
 * list (sys/iosys.c says why), so a program may reach it through BDOS
 * function 50 and does not need the raw SC #3 gate.  P1 is one of the
 * SEG_* subfunctions; P2 is the segment, for SEG_PUT only.
 *
 * SEG_GET answers the logical segment number of 64 KB nothing else can
 * reach -- turn it into a pointer with SEGBASE() -- or 0 when there is
 * none, which is what the standard 512 KB machine always answers.
 * Every segment a program holds is released for it at the next warm
 * boot, so SEG_PUT is for a program that wants one back before it
 * exits, not for one that is exiting.
 */
#define	BIOS_SEGMENT	25	/* allocate/free/count 64 KB segments	*/
#define	SEG_GET		0L	/* -> segment number, or 0 if none left	*/
#define	SEG_PUT		1L	/* P2 = segment; -> 1 freed, 0 not ours	*/
#define	SEG_COUNT	2L	/* -> how many are free right now	*/

/* the XADDR of a segment's first byte: what SEG_GET's answer is for */
#define	SEGBASE(s)	((long)(s) << 24)

/*
 * BIOCOST-only codes, NOT reachable through function 50: bioscl()
 * (sys/iosys.c) refuses 2-7 outright, and these two do not even exist
 * there -- they are read through the raw SC #3 gate instead (biossc.s
 * __bios(), bdosglue.s `biosgate'), which every stock DRI BIOS trap
 * uses and which bioscl's refusal list has no say over.  Added to
 * src/bios/bios900.c's dispatch purely so src/cmd/biocost.c can reach,
 * one at a time, the two BIOS-internal primitives concost.c cannot:
 * the ROM's own glyph renderer and the direct video-RAM store that
 * bypasses it.  BIOS_CONOUT (4) is the stock CONOUT code, listed here
 * only as the SC #3 counterpart of BDOS function 2.
 */
#define	BIOS_CONOUT	4	/* console output(char) -- SC #3 only	*/
#define	BIOS_ROMCHAR	100	/* ROM putchar direct (romabi.h putchar) */
#define	BIOS_VSETCHAR	101	/* vsetcell direct video store		*/

/* error modes for function 45 */
#define	ERRMODE_DEFAULT	0x00	/* display the error and terminate	*/
#define	ERRMODE_RETURN	0xff	/* return the error to the program	*/
#define	ERRMODE_DISPRET	0xfe	/* display the error AND return it	*/

/* character control block for functions 111/112 (C900 deviation: the
   address is a 32-bit XADDR, not the 8080's 2-byte pointer) */
struct ccb {
	long	cbaddr;		/* address of the character block	*/
	unsigned cblen;		/* number of characters to send		*/
};

#define	SECLEN	128		/* one CP/M record			*/

struct fcb {
	char	drvcode;	/* 0 = default drive, 1..16 = A..P	*/
	char	fname[8];	/* file name, blank padded		*/
	char	ftype[3];	/* file type, blank padded		*/
	char	extent;
	char	s1, s2;
	char	rcdcnt;
	char	dskmap[16];
	char	cur_rec;
	char	ran0, ran1, ran2;
};

struct bpage {			/* the base page pgmld builds		*/
	long	ltpa;		/* addresses are XADDR far pointers	*/
	long	htpa;
	long	lcode;
	long	codelen;
	long	ldata;
	long	datalen;
	long	lbss;
	long	bsslen;
	long	freelen;
	char	resvd1[20];
	struct fcb fcb2;
	struct fcb fcb1;
	char	buff[128];	/* command tail (len byte + text),	*/
				/*   also the default DMA buffer	*/
};

#ifndef	VOID
#define	VOID	int
#endif

extern struct bpage *_base;	/* set by the runtime startup		*/

extern int	__bdos();	/* __bdos(func, param) -> the SC #2 gate */
extern long	__bdosl();	/* the same gate, LONG result (fn 50)	*/
extern long	__bios();	/* __bios(func, p1, p2) -> the SC #3 raw
				   BIOS gate (biossc.s), bypassing the BDOS
				   and its function-50 refusal list	*/

/* libcpm.c */
extern int	conout();
extern int	conin();
extern VOID	cputs();
extern VOID	conputs();
extern VOID	printstr();
extern VOID	putdec();
extern VOID	setdma();
extern VOID	mkfcb();
