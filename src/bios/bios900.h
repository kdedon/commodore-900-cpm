/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/* What bios900.c shares with the cold-boot code in boot900.c. */

/* The console table's size: console 0 and up to three bound lines.  It is
 * NOT the process-descriptor count PNPROC -- but every console with a
 * session running holds a
 * descriptor, so a larger table costs program slots.  Additional reported
 * channels remain unbound. */
#define CONMAX		4
#define NCHAN		16	/* bi_serial is 16 bits			*/

#define CD_NONE		0	/* nothing is attached to this console	*/
#define CD_ROM		1	/* the console the ROM selected at boot	*/
#define CD_SCCA		2	/* SCC channel A, raw			*/
/*
 * The device number of serial channel `c'.  CD_SCCA IS CD_SER(1) -- the
 * guest-visible numbering of BIOS function 28 is unchanged, and channels
 * past the first spare simply continue it.  CD_SER(0) is CD_ROM, which
 * is the truth: channel 0 is the ROM's own console channel, and console
 * 0 reaches it the ROM's way (through crsr.c, or the keyboard on a video
 * machine) rather than as a raw line.
 */
#define CD_SER(c)	((c) + 1)

/* kboot slots 8..14 describe drives A:..G:; zero-sized slots are absent.
 * Without a usable drive handoff, use dflpart[]. Layout is fixed at boot.
 * Tracks are drive-relative: record = track*64 + sector (zero-based),
 * physical block = drive base + record/4, offset = (record%4)*128.
 * Every DPB has trk_off=0; allocation blocks are 4096 bytes. */
#define ASPT		64		/* 128-byte records per track */
#define BLSBLKS		8		/* 512-byte blocks per 4096-byte alloc blk */
#define BIDRV0		8		/* bootinfo slot of drive A: */
#define NDRIVE		7		/* A: .. G:, slots 8..14 */
/* SECLEN (128) comes from bdosdef.h */
#define ALVPOOL		1536

extern int convid, pend[], ncon, auxchan, dskerr, curix;
extern char condev[], iobyte, drvthere[];
extern unsigned int conmap;
extern UBYTE dirbuf[], alvpool[];
extern struct dpb dpbtab[];
extern struct dph dphtab[];
extern long drvbase[], curbase;
