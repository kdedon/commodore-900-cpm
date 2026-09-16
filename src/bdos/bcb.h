/*
 * bcb.h -- interface to the BDOS-internal LRU sector cache (bcb.c).
 *
 * A "BCB" is one 512-byte physical-sector buffer in the pool at BUFBASE
 * (c900cfg.h).  bcbfind() returns the index of the buffer holding a block,
 * reading it in if it is not resident; BCBADR() gives that buffer's far
 * address.  Every routine returning something wider than an int is
 * declared here so callers get the width right.
 */

#define BCBLEN	512			/* bytes per buffer */
#define BCBSHF	9			/* log2 BCBLEN */

/* Far address of buffer ix.  The whole pool is under 64 KB, so the offset
   is computed as a word and never needs a long multiply. */
#define BCBADR(ix)	(BUFBASE + (LONG)(WORD)((ix) << BCBSHF))

EXTERN WORD	bcbfind();		/* (block, fill) -> index, -1 = error */
EXTERN WORD	bcbput();		/* (index) write one back now */
EXTERN WORD	bcbflush();		/* write every dirty buffer back */
EXTERN		bcbmark();		/* (index) mark dirty */
EXTERN		bcbinit();
