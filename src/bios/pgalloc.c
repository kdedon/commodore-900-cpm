/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/* 64 KB segment/page allocator.
 * Slot i pairs logical segment PGSEG(i) with a physical pool page.
 * Capacity comes from the ROM memory report, capped by available segments;
 * reserve the top RAM page for ROM state. A 512 KB machine has no free slots.
 * Allocated segments are accessible in Normal mode; freeing restores the
 * System-only attribute. Pool pages are not offered as loader TPA regions.
 *
 * pgtpaswap() exchanges a pool page with the physical backing of TPASEG.
 * TPASEG stays fixed because transient binaries have no relocation records.
 * The caller must preserve resident process state and split-I/D banks.
 * pgrelall() restores the boot TPA mapping and releases unheld slots.
 *
 * OWNERSHIP IS A PROCESS, NOT A BOOLEAN.  pgown[i] holds pgcur+1, the
 * process the slot was handed to; pgcur is the running process's
 * descriptor index and src/bdos/proc.c keeps it (it is that file's `pcur',
 * published here because the allocator is resident and proc.c is not the
 * only caller).  It matters because pgrelall() runs on EVERY warm boot: a
 * foreground ^C used to release every allocated, unheld slot, which is
 * every scratch segment a BACKGROUND 8086 or Z80 interpreter was running
 * out of -- and the pool then handed those segments to somebody else while
 * the interpreter was still using them.  A warm boot now releases only the
 * warm-booting process's own. */
#include "stdio.h"
#include "c900cfg.h"

extern int mapseg();
extern short sysstk;

/* Segment 1 offset zero contains a CPU far pointer, (seg<<24)|offset,
 * to the ROM configuration block. rom_bram/rom_eram are its first two
 * words, in 1 KB clicks (COHERENT machine.h ctob() uses shift 10). */
#define ROMCONF_PP	0x01000000L	/* seg 1:0 -- the ROM's far ptr	*/
#define RC_BRAM		0		/* word 0: first click of RAM	*/
#define RC_ERAM		1		/* word 1: click past the end	*/

/* clicks are 1 KB, a page is 64 KB: 64 clicks to the page */
#define CLICKPAGE	6		/* clicks >> 6 = page		*/

static char	pgown[PGNSLOT];	/* 0: slot i free.  Otherwise the owning
				 * process's index plus one -- see the
				 * banner.  Zero cannot be an owner code
				 * because a bss-cleared table has to read
				 * as empty. */
int		pgcur;		/* THE RUNNING PROCESS, as a descriptor
				 * index; proc.c assigns it wherever it
				 * assigns pcur.  Zero before there are any
				 * processes, which is right: everything
				 * allocated then belongs to process 0. */
static char	pghld[PGNSLOT];	/* slot i is a LIVE PROCESS's parked 64 KB
				 * image (src/bdos/proc.c), not a transient's
				 * scratch.  The distinction exists for one
				 * reason: pgrelall() runs on every warm boot
				 * and a warm boot is the end of ONE program,
				 * not of the machine.  A held slot is not
				 * reclaimed there and does not take part in
				 * pgtparest(), because the page it holds is
				 * a program that is still alive. */
static char	pgpg[PGNSLOT];	/* the physical page slot i points at.
				 * Normally PGPGLO+i, and it starts there;
				 * a pgtpaswap() exchange is the only thing
				 * that ever makes it differ, and then it
				 * holds whatever page the TPA gave up. */
int		tpaphys;	/* the page segment TPASEG points at now */
int		pgnslot;	/* slots this machine can actually give	*/
unsigned	pgbram, pgeram;	/* the ROM's report, kept for the record */

/*
 * The slot a logical segment belongs to: PGSEG() run backwards (c900cfg.h).
 * Anything that is not a pool segment answers a negative index or one past
 * PGNSLOT, which every caller already refuses along with slots this
 * machine does not have.
 */
static int pgslot(seg)
int seg;
{
	if (seg >= PGSEGLO)
		return (seg - PGSEGLO < PGNUP ? seg - PGSEGLO : -1);
	return (PGNUP + (PGSEGLO - 1 - seg));
}

/*
 * How many slots a ROM report of [bram, eram) clicks can back.  Split
 * from pginit() so the arithmetic can be exercised for RAM sizes no
 * real machine here has.
 */
int pgsize(bram, eram)
unsigned bram, eram;
{
	register int lo, hi, n;

	lo = (int)(bram >> CLICKPAGE);
	hi = (int)(eram >> CLICKPAGE);	/* one past the last page */

	/*
	 * The resident layout must lie inside the RAM the ROM reports,
	 * or the report is not describing this machine and none of the
	 * arithmetic below means anything.
	 */
	if (lo > SYSPHYSPAGE || hi <= PGPGLO)
		return (0);

	hi--;				/* the ROM's segment-1 page */
	if (hi <= PGPGLO)
		return (0);		/* 512 KB: nothing left over */

	n = hi - PGPGLO;
	if (n > PGNSLOT)
		n = PGNSLOT;
	return (n);
}

/* Size the pool once at cold boot. Unrecognized ROM reports leave it
 * empty; resident pages and the top RAM page are never allocated. */
pginit()
{
	register long pp;
	register unsigned *rc;
	register int i;

	pgnslot = 0;
	pgbram = pgeram = 0;
	pgcur = 0;
	tpaphys = TPAPHYSPAGE;		/* where crt.s left segment TPASEG */
	for (i = 0; i < PGNSLOT; i++) {
		pgown[i] = 0;
		pghld[i] = 0;
		pgpg[i] = PGPGLO + i;
	}

	pp = *(long *)ROMCONF_PP;
	if (((pp >> 24) & 0x7fL) != 1L)
		return (0);		/* not the ROM's pointer */
	rc = (unsigned *)pp;
	pgbram = rc[RC_BRAM];
	pgeram = rc[RC_ERAM];

	pgnslot = pgsize(pgbram, pgeram);
	return (pgnslot);
}

/*
 * Allocate one segment: its logical segment number, or 0 on failure.
 * Zero is the refusal because segment 0 is the ROM's code segment and
 * can never be handed out, so no caller has to be told a sentinel.
 */
int pgalloc()
{
	register int i;

	for (i = 0; i < pgnslot; i++)
		if (!pgown[i]) {
			pgown[i] = (char)(pgcur + 1);
			mapseg(PGSEG(i), pgpg[i] << 8, 0x00);
			return (PGSEG(i));
		}
	return (0);
}

/* Release an allocated slot, returning 1. Invalid or already-free slots
 * return 0. The descriptor becomes System-only. */
int pgfree(seg)
int seg;
{
	register int i;

	i = pgslot(seg);
	if (i < 0 || i >= pgnslot || !pgown[i])
		return (0);
	pgown[i] = 0;
	pghld[i] = 0;
	mapseg(seg, pgpg[i] << 8, 0x02);	/* System only from here */
	return (1);
}

/* May the running process write len bytes at seg:off?  Its TPA, a slot it
 * owns, or, in the supervisor stacks, its own stack from lo (the end of
 * the copying gate's frame) to its top: the trap frame a debugger's
 * handler edits.  A copy wraps within its segment, so only 0x3F needs
 * the range checked. */
int pgmine(seg, off, len, lo)
int seg;
unsigned off, len, lo;
{
	register int i;
	register unsigned top;

	if (seg == TPASEG)
		return (1);
	if (seg == 0x3f) {
		top = sysstk;
		return (off >= lo && off <= top && len <= top - off);
	}
	i = pgslot(seg);
	return (i >= 0 && i < pgnslot && pgown[i] == pgcur + 1);
}

/* Mark a slot as a live process's parked image. The scheduler sets this
 * flag on creation and clears it on exit; pgrelall() preserves held slots. */
pghold(seg, on)
int seg, on;
{
	register int i;

	i = pgslot(seg);
	if (i < 0 || i >= pgnslot || !pgown[i])
		return (0);
	pghld[i] = on ? 1 : 0;
	return (1);
}

/* Is any slot holding a live process?  pgrelall() asks. */
static int pgheld()
{
	register int i;

	for (i = 0; i < pgnslot; i++)
		if (pgown[i] && pghld[i])
			return (1);
	return (0);
}

/* Exchange the TPA backing page with an allocated pool segment's page.
 * Both mappings retain Normal-mode access. Only resident code may call:
 * a caller executing in the TPA would replace its own instructions. */
int pgtpaswap(seg)
int seg;
{
	register int i, old;

	i = pgslot(seg);
	if (i < 0 || i >= pgnslot || !pgown[i])
		return (0);

	old = tpaphys;
	tpaphys = pgpg[i];
	pgpg[i] = old;
	mapseg(TPASEG, tpaphys << 8, 0x00);
	mapseg(seg, old << 8, 0x00);
	return (1);
}

/*
 * Put the TPA back on the page crt.s gave it.  A no-op unless a swap is
 * outstanding, and then it is the same exchange run backwards: the slot
 * holding TPAPHYSPAGE takes the page the TPA is on now.  The slot may or
 * may not still be owned, so its attribute is restored to whichever of
 * the two states it was in.
 */
pgtparest()
{
	register int i;

	if (tpaphys == TPAPHYSPAGE)
		return (0);
	for (i = 0; i < pgnslot; i++)
		if (pgpg[i] == TPAPHYSPAGE) {
			pgpg[i] = tpaphys;
			tpaphys = TPAPHYSPAGE;
			mapseg(TPASEG, TPAPHYSPAGE << 8, 0x00);
			mapseg(PGSEG(i), pgpg[i] << 8,
			       pgown[i] ? 0x00 : 0x02);
			return (1);
		}
	return (0);		/* cannot happen: the page went somewhere */
}

/* How many are left.  Zero on a 512 KB machine, always. */
int pgcount()
{
	register int i, n;

	n = 0;
	for (i = 0; i < pgnslot; i++)
		if (!pgown[i])
			n++;
	return (n);
}

/*
 * Release the scratch segments of the process that is running now, and
 * nobody else's.  A slot owned by another process is left exactly as it
 * is even though it is unheld: unheld means "not a parked 64 KB process
 * image", which is true of an 8086 guest's data group while the guest is
 * running.  The TPA mapping is not touched here; pgrelall() does that.
 * proc.c calls this for a dying background process, whose scratch would
 * otherwise stay allocated to a descriptor index that has been freed.
 */
pgrelproc()
{
	register int i;
	register int me;

	me = pgcur + 1;
	for (i = 0; i < pgnslot; i++)
		if (pgown[i] == (char)me && !pghld[i])
			pgfree(PGSEG(i));
	return (0);
}

/* Give everything back.  The warm-boot path; see the banner. */
pgrelall()
{
	/* Do not restore the boot TPA while a live process is parked: that could
	 * put its image under the CCP loader. Release only unheld scratch slots. */
	if (!pgheld())
		pgtparest();
	return (pgrelproc());
}
