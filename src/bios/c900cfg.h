#define	TPASEG		0x32
#define	TPABASE		0x32000000L	/* (long)TPASEG<<24 -- keep in sync */
#define	TPAPHYSPAGE	0x0a		/* phys 0x0A0000, high descriptor byte */

/*
 * Split-I/D loader shim (Option 6, 0xEE0B programs).  Code runs natively
 * in the TPA (code bank), data lives in a second 64K bank, and the
 * load-time scanner's side table in a third.  Both are mapped at load
 * time (splitld.c spload), SYS-only: the program itself never addresses
 * them -- every access goes through the SC #255 trap handler (splitsc.c)
 * or the BDOS gate's split-aware pointer mapping (bdosglue.s).  Neither
 * segment is in the MRT, so the BDOS never offers them as TPA.
 */
#define	SPLITDSEG	0x35		/* data bank */
#define	SPLITDBASE	0x35000000L
#define	SPLITDPAGE	0x0b00		/* phys 0x0B0000 (mapseg base page) */
#define	SPLITTSEG	0x36		/* side table + scanner scratch */
#define	SPLITTBASE	0x36000000L
#define	SPLITMSEG	0x37		/* relocated shim slow path */
#define	SPLITMBASE	0x37000000L
#define	SPLITMPAGE	0x0f00		/* phys 0x0F0000 (mapseg base page) */
#define	SPLITMPHYSPAGE	0x0f		/* the same, high descriptor byte */
#define	SPLITMLIM	0xE3		/* last valid page: 0x0FE3FF, one
					 * short of the ROM's seg-1 cells */
#define	SPM_MAGIC	0x5350		/* 'SP' at module offset 0 */
#define	SPM_VERS	1		/* interface version, offset 2 */
#define	SPM_EMU		0x37000004	/* jp spemu_  (SC #255 slow path) */
#define	SPM_SCAN	0x3700000A	/* jp spscan_ (load-time scan) */
#define	SPM_TOP		0x37000010	/* zw sptop */
#define	SPM_USP		0x37000012	/* zw spusp */
#define	SPM_UFP		0x37000014	/* zw spufp */

#define	BUFSEG		0x33
#define	BUFBASE		0x33000000L
#define	BUFPHYS		0x000C0000L	/* phys 0x0C0000 (DMA target) */
#define	NBCB		32		/* sector buffers in the pool */
#define	DHBASE		0x33004000L	/* NBCB*512 = 0x4000 */
#define	DHPHYS		0x000C4000L
#define	DHMAX		512		/* directory entries the table covers */
					/* per-drive on/off: dskhash.c hashen[] */

#define	SPLITSCW	0x7FFF		/* the patch word: SC #255 */
#define	SPLITMAXT	0xE000		/* max text bytes: the side table's
					 * scan scratch begins at this offset
					 * in the SPLITTSEG segment */
