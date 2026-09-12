
/********************************************************
*							*
*	BIOS definitions for P-CP/M			*
*							*
*	Copyright (c) 1982 Digital Research, Inc.	*
*							*
*	This include file simply defines the BIOS calls	*
*							*
*	Memory management added 821018 by SS at Zilog	*
*							*
********************************************************/

EXTERN long	bios();		/* main BIOS entry point	    */
EXTERN UBYTE	bios1();	/* used for character I/O functions */
EXTERN 		bios2();	/* parm1 is word, no return value   */
EXTERN		bios3();	/* used for set dma only	    */
				/* parm1 is a pointer, no return    */
EXTERN UBYTE	*bios4();	/* seldsk only, parm1 and parm2 are */
				/*   words, returns a pointer to dph */
EXTERN UWORD	bios5();	/* for sectran and set exception    */
EXTERN BYTE	*bios6();	/* for get memory segment table	    */


#define bwboot()	bios1(1)	/* warm boot 		*/	
/* Pass concur to console BIOS calls. Batched output packs the console
 * into count bits 23:16 and the character count into bits 15:0. */
			/* console output, a whole RUN of characters at
			   once (src/bios/bios900.c case 26).  The BDOS
			   still owns tab expansion, the column, ^S/^Q,
			   ^P and the function 109 modes -- this only
			   removes the per-character trip through
			   bios2_'s marshalling and the BIOS dispatch
			   switch for the plain text in between.  */
#define bconcnt()	((WORD)bios(29, 0L, 0L))
			/* runtime console count from the loader-supplied serial map */
#define blstout(parm)	bios2(5,parm)	/* list device output	*/
#define bpun(parm)	bios2(6,parm)	/* punch char output	*/
#define brdr()		bios1(7)	/* reader input		*/
#define bhome()		bios1(8)	/* recalibrate drive	*/
#define bseldsk(parm1,parm2) bios4(9,parm1,parm2)
					/* select disk and return info */
#define bsettrk(parm)	bios2(10,parm)	/* set track on disk	*/
#define bsetsec(parm)	bios2(11,parm)	/* set sector for disk	*/
#define bsetdma(parm)	bios3(12,parm)	/* set dma address   	*/
#define bread()		bios1(13)	/* read sector from disk */
#define bwrite(parm)	bios2(14,parm)	/* write sector to disk	*/
#define blistst()	bios1(15)	/* list device status	*/
#define bsectrn(parm1,parm2) bios5(16,parm1,(XADDR)parm2)
					/* sector translate	*/
#define bgetseg()	bios6(18)	/* get memory segment tbl */
#define bgetiob()	bios1(19)	/* get I/O byte		*/
#define bsetiob(parm)	bios2(20,parm)	/* set I/O byte		*/
#define bflush()	bios1(21)	/* flush buffers	*/
#define bsetvec(parm1,parm2) bios5(22,parm1,(XADDR)parm2)
					/* set exception vector	*/
#define btime(t,s)	((int)bios(23, (XADDR)(t), (long)(s)))
			/* read (s = 0) or set (s != 0) the real-time clock
			   through the caller's five-byte TOD block; 0 = ok,
			   0xff = no clock answered.  Both parameters are
			   cast here because bios() takes two LONGs and a
			   bare int argument would be passed a word wide. */


					/************************/
					/* MEMORY MANAGEMENT	*/
					/*----------------------*/
EXTERN XADDR map_adr();			/*(laddr, space)->paddr	*/
EXTERN VOID mem_cpy();			/*(src, dst, len)	*/
					/*----------------------*/
					/* copy in, out (s,d,l)	*/
					/*			*/
#define cpy_in(s,d,l) mem_cpy((XADDR)s, map_adr((XADDR)d, 0), (long)l)
#define cpy_out(s,d,l) mem_cpy(map_adr((XADDR)s, 0), (XADDR)d, (long)l)
					/*			*/
					/************************/


