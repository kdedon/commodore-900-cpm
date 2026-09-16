/*
 * dskhash.h -- interface to the directory signature table (dskhash.c).
 *
 * struct dhq is one scan's filter, built by dhstart() from the dirscan
 * function and FCB and consulted per directory index by dhcand().  It is
 * the caller's automatic, so a nested dirscan (the checksum-error relog
 * path) cannot disturb an outer scan's filter.
 */

struct dhq
{
	WORD	mode;		/* 0 no filter, 1 name match, 2 empty slot */
	WORD	chk;		/* DHC_ bits that are armed		   */
	UWORD	usr;		/* wanted directory entry byte		   */
	UWORD	nam;		/* wanted name hash			   */
	UWORD	ext;		/* wanted (s2 << 8) | extent		   */
	UWORD	exmask;		/* extent compare mask, ~exm & 0xff	   */
};

#define DHC_USR	1
#define DHC_NAM	2
#define DHC_EXT	4

EXTERN WORD	dhstart();	/* (q, funcp, fcbp) -> mode	*/
EXTERN WORD	dhcand();	/* (q, index) -> could match	*/
EXTERN		dhopen();	/* (drive, drm) begin a rebuild	*/
EXTERN		dhadd();	/* (index, dirent ptr) if rebuilding */
EXTERN		dhset();	/* (index, dirent ptr)		*/
EXTERN		dhrec();	/* (dir record, dirent ptr)	*/
EXTERN		dhdone();	/* rebuild complete		*/
