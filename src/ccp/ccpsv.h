


/*  Command-line and buffer sizes.  These repeat ccpdef.h's CMD_LEN,
    FCB_LEN, ERRLINE, PATHMAX, NDMAX, NDNAME and IFMAX rather than
    including it, because sys/ side files (ccprun.c) need the layout
    without the CCP's macro soup -- and a mismatch is caught at cold
    boot by the size check in ccpsvinit().  */

#define	SV_CMDLEN	128		/* ccpdef.h CMD_LEN		*/
#define	SV_FCBLEN	36		/* ccpdef.h FCB_LEN		*/
#define	SV_ERRLINE	130		/* ccpdef.h ERRLINE		*/
#define	SV_PATHMAX	8		/* ccpdef.h PATHMAX		*/
#define	SV_NDMAX	8		/* ccpdef.h NDMAX		*/
#define	SV_NDNAME	8		/* ccpdef.h NDNAME		*/
#define	SV_IFMAX	8		/* ccpdef.h IFMAX		*/

struct ccpsv {

    /*  ---- the system half.  ccprun.c reads and writes these; keep
	them first so the resident side never depends on the layout of
	anything below.  ---- */

    unsigned int sv_magic;	/* CCPSVMAGIC once ccpsvinit() has run	*/
    unsigned int sv_pend;	/* a program load is pending: the CCP	*/
				/*  resolved a command and warm booted	*/
				/*  instead of loading it itself, which	*/
				/*  it cannot do -- the transfer is an	*/
				/*  IRET in system mode (`glue.s' xfer_)*/
    char	sv_pfcb[SV_FCBLEN];	/* the program's open FCB	*/
    char	sv_pfcb1[SV_FCBLEN];	/* base-page FCB 1 (tail arg 1)	*/
    char	sv_pfcb2[SV_FCBLEN];	/* base-page FCB 2 (tail arg 2)	*/
    char	sv_ptlen;		/* command tail length		*/
    char	sv_ptail[SV_CMDLEN];	/* command tail, no length byte	*/

    /*  ---- the CCP half.  One field per mutable global of ccp.c and
	ccpext.c that has to outlive a load.  ---- */

    char	sv_load_try;
    char	sv_first_sub;
    char	sv_chain_sub;
    char	sv_end_of_file;
    char	sv_dirflag;
    char	sv_subprompt;
    char	sv_in_errhook;
    char	sv_autost;
    char	sv_submit;
    char	sv_morecmds;
    unsigned int sv_sub_index;
    unsigned int sv_user;
    unsigned int sv_cur_disk;

    char	sv_subfcb[SV_FCBLEN];	/* the OPEN .SUB file: extent,	*/
					/*  record count and record	*/
					/*  position are bytes 12-15	*/
					/*  and 32-35 of this array	*/
    char	sv_subcom[SV_CMDLEN+1];
    char	sv_subdma[SV_CMDLEN];
    char	sv_save_sub[SV_CMDLEN+1];
    char	sv_usercmd[SV_CMDLEN+2];
    char	sv_errline[SV_ERRLINE];

    char	*sv_user_ptr;		/* all three point into		*/
    char	*sv_glb_index;		/*  sv_usercmd or sv_subcom	*/
    char	*sv_tail;


    char	sv_cfg_done;
    char	sv_flow_en;
    char	sv_path_n;
    char	sv_nd_n;
    char	sv_path_d[SV_PATHMAX];
    char	sv_path_u[SV_PATHMAX];
    char	sv_nd_nam[SV_NDMAX][SV_NDNAME+1];
    char	sv_nd_d[SV_NDMAX];
    char	sv_nd_u[SV_NDMAX];
    char	sv_errcmd[SV_NDNAME+1];
    char	sv_fstk[SV_IFMAX];
    unsigned int sv_fdep;
};


#ifdef CCPTRANSIENT


#define	load_try	(CCPSV->sv_load_try)
#define	first_sub	(CCPSV->sv_first_sub)
#define	chain_sub	(CCPSV->sv_chain_sub)
#define	end_of_file	(CCPSV->sv_end_of_file)
#define	dirflag		(CCPSV->sv_dirflag)
#define	subprompt	(CCPSV->sv_subprompt)
#define	in_errhook	(CCPSV->sv_in_errhook)
#define	autost		(CCPSV->sv_autost)
#define	submit		(CCPSV->sv_submit)
#define	morecmds	(CCPSV->sv_morecmds)
#define	sub_index	(CCPSV->sv_sub_index)
#define	user		(CCPSV->sv_user)
#define	cur_disk	(CCPSV->sv_cur_disk)
#define	subfcb		(CCPSV->sv_subfcb)
#define	subcom		(CCPSV->sv_subcom)
#define	subdma		(CCPSV->sv_subdma)
#define	save_sub	(CCPSV->sv_save_sub)
#define	usercmd		(CCPSV->sv_usercmd)
#define	errline		(CCPSV->sv_errline)
#define	user_ptr	(CCPSV->sv_user_ptr)
#define	glb_index	(CCPSV->sv_glb_index)
#define	tail		(CCPSV->sv_tail)

#define	cfg_done	(CCPSV->sv_cfg_done)
#define	flow_en		(CCPSV->sv_flow_en)
#define	path_n		(CCPSV->sv_path_n)
#define	nd_n		(CCPSV->sv_nd_n)
#define	path_d		(CCPSV->sv_path_d)
#define	path_u		(CCPSV->sv_path_u)
#define	nd_nam		(CCPSV->sv_nd_nam)
#define	nd_d		(CCPSV->sv_nd_d)
#define	nd_u		(CCPSV->sv_nd_u)
#define	errcmd		(CCPSV->sv_errcmd)
#define	fstk		(CCPSV->sv_fstk)
#define	fdep		(CCPSV->sv_fdep)

#endif	/* CCPTRANSIENT */
