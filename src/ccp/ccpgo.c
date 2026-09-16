/************************************************************************
*									*
*		The CCP's half of the program load			*
*									*
*	When the CCP was resident, `go.c' did the whole job: it built	*
*	the Load Parameter Block, called BDOS function 59, filled the	*
*	base page and jumped into the program with `xfer'.  Half of	*
*	that is CCP work and half of it is system work, and a		*
*	transient CCP can only do the first half:			*
*									*
*	  - the two base-page FCBs come from parsing the command tail,	*
*	    which only the CCP can do (`fill_fcb' reads `parm', `index'	*
*	    and `nd_argu', all of them CCP parse state);		*
*	  - the transfer of control is an IRET in SYSTEM mode		*
*	    (`src/glue.s' `xfer_': it writes NSPSEG/NSPOFF and resets	*
*	    the system stack).  A program in the TPA runs in Normal	*
*	    mode and cannot do it.					*
*									*
*	So the CCP leaves a request in the state page and warm boots.	*
*	`sys/ccprun.c' picks the request up on the way back and does	*
*	the second half.  CP/M 3 needs no such split because its CCP	*
*	and its programs share one address space and one privilege	*
*	level: `ccp3.asm' calls the LOADER RSX and then jumps to 100h	*
*	itself.								*
*									*
*	This is also why the request carries the FCBs and not the	*
*	command: the system does not parse command lines, the CCP	*
*	does, and function 47 (chain to program, `bdosmain.c:404')	*
*	already exists for the other direction -- a PROGRAM handing a	*
*	command line back to the CCP.					*
*									*
************************************************************************/

#include	"ccpdef.h"
#include	"c900cfg.h"
#include	"ccpsv.h"

extern UWORD	bdos();
extern UWORD	fill_fcb();
extern BYTE	cmdfcb[];		/* the FCB cmd_file opened	*/
				/* `tail' needs no declaration: it	*/
				/*  is a field of the state page	*/
				/*  (ccpsv.h) and ccp.c reaches it by	*/
				/*  the same name			*/


		/********************************/
		/*				*/
		/*        _ _ L O A D           */
		/*				*/
		/********************************/

VOID __LOAD()
{
	REG BYTE	*tp;
	REG BYTE	*sp;
	REG UWORD	i;
	REG UWORD	tlen;

/*  The FCB of the file cmd_file() opened.  It has to be copied out
    before fill_fcb() is called, because fill_fcb() rebuilds cmdfcb in
    place for the base page -- go.c relied on the load having already
    happened by then, and here the load has not happened yet.	*/

	for (i = 0; i < FCB_LEN; i++)
		CCPSV->sv_pfcb[i] = cmdfcb[i];

/*  Command tail.  go.c computed the length the same way, because the
    CCP does not supply one.	*/

	tlen = 0;
	sp = CCPSV->sv_ptail;
	for (tp = tail; *tp != NULL; tp++)
		if (tlen < SV_CMDLEN) {
			*sp++ = *tp;
			tlen++;
		}
	CCPSV->sv_ptlen = tlen;

/*  Clear the rest.  go.c copied 127 bytes straight out of `usercmd', so
    what followed the tail was the NUL the console read left; here the
    buffer is reused across commands and what follows would be the
    previous command's bytes.  A stock tool that scans the tail for a
    terminator rather than trusting the length byte reads them: STAT saw
    `STAT HELLO.TXT' as two arguments and printed the SET form of its
    output.  Caught by `make verify-set'.	*/

	for (i = tlen; i < SV_CMDLEN; i++)
		CCPSV->sv_ptail[i] = NULL;

/*  The two base-page FCBs, parsed out of the tail.  */

	fill_fcb(1, cmdfcb);
	for (i = 0; i < FCB_LEN; i++)
		CCPSV->sv_pfcb1[i] = cmdfcb[i];

	fill_fcb(2, cmdfcb);
	for (i = 0; i < FCB_LEN; i++)
		CCPSV->sv_pfcb2[i] = cmdfcb[i];


	CCPSV->sv_pend = 1;
	bdos(WARMBOOT, 0L);		/* does not return		*/
}
