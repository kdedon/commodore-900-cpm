/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/* 64 KB segment/page allocator.
 * Slot i is physical page pgpg[i]; pginit() sizes the pool.  An allocated
 * slot is mapped on a free logical segment PGSEG(k), pgss[k] = i+1, and
 * the segment is its name.  A parked process image is unmapped and named
 * by the handle PGPARK+i instead, so parked processes do not use up
 * segments.
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

#define	PGBKOWN	0x7f		/* pgown[] of a split-I/D bank slot */

char		pgown[PGNSLOT];	/* 0: slot i free.  Otherwise the owning
				 * process's index plus one -- see the
				 * banner.  Zero cannot be an owner code
				 * because a bss-cleared table has to read
				 * as empty. */
int		pgcur;		/* THE RUNNING PROCESS, as a descriptor
				 * index; proc.c assigns it wherever it
				 * assigns pcur.  Zero before there are any
				 * processes, which is right: everything
				 * allocated then belongs to process 0. */
char		pghld[PGNSLOT];	/* slot i is a LIVE PROCESS's parked 64 KB
				 * image (src/bdos/proc.c), not a transient's
				 * scratch.  The distinction exists for one
				 * reason: pgrelall() runs on every warm boot
				 * and a warm boot is the end of ONE program,
				 * not of the machine.  A held slot is not
				 * reclaimed there and does not take part in
				 * pgtparest(), because the page it holds is
				 * a program that is still alive. */
unsigned char	pgpg[PGNSLOT];	/* the physical page slot i points at.
				 * pginit() fills it; a pgtpaswap()
				 * exchange changes it to whatever page
				 * the TPA gave up. */
unsigned char	pgss[PGNSEG];	/* slot+1 mapped on PGSEG(k), 0 if free	*/
unsigned char	pgks[PGNSLOT];	/* pgss inverted: k+1 of slot i, 0 if parked */
int		tpaphys;	/* the page segment TPASEG points at now */
int		pgnslot;	/* slots this machine can actually give	*/
unsigned	pgbram, pgeram;	/* the ROM's report, kept for the record */

/* PGSEG() run backwards: the index k of a pool segment, or -1. */
static int pgsegix(seg)
int seg;
{
	if (seg >= PGSEGLO)
		return (seg - PGSEGLO < PGNUP ? seg - PGSEGLO : -1);
	seg = PGNUP + (PGSEGLO - 1 - seg);
	return (seg < PGNSEG ? seg : -1);
}

/* The segment slot i is mapped on, or 0. */
static int pgsegof(i)
int i;
{
	return (pgks[i] ? PGSEG(pgks[i] - 1) : 0);
}

/* The allocated slot a segment or parked handle names, or -1. */
static int pgslot(h)
int h;
{
	register int i;

	if (h >= PGPARK) {
		i = h - PGPARK;
		if (i < pgnslot && pgown[i] && pgown[i] != PGBKOWN && !pgks[i])
			return (i);
		return (-1);
	}
	if ((i = pgsegix(h)) < 0)
		return (-1);
	return (pgss[i] - 1);
}

/*
 * Allocate one segment: its logical segment number, or 0 on failure.
 * Zero is the refusal because segment 0 is the ROM's code segment and
 * can never be handed out, so no caller has to be told a sentinel.
 * It takes a free page and a free segment.
 */
int pgalloc()
{
	register int i, k;

	for (k = 0; k < PGNSEG; k++)
		if (!pgss[k])
			break;
	if (k == PGNSEG)
		return (0);
	for (i = 0; i < pgnslot; i++)
		if (!pgown[i]) {
			pgown[i] = (char)(pgcur + 1);
			pgss[k] = i + 1;
			pgks[i] = k + 1;
			mapseg(PGSEG(k), pgpg[i] << 8, 0x00);
			return (PGSEG(k));
		}
	return (0);
}

/* The split-I/D data bank and side table: two slots, owned by no process
 * and mapped System-only on SPLITDSEG and SPLITTSEG while a split program
 * is live.  The slot holding the boot TPA page is never taken, so
 * pgtparest() cannot move a bank. */
static unsigned char pgbk[2];	/* slot+1 of each bank, 0 if none */

/* Map both banks, taking slots unless they are held already: 1, or 0 if
 * the pool has too few. */
int pgbanks()
{
	register int i, n;

	if (pgbk[0])
		return (1);
	for (i = n = 0; i < pgnslot && n < 2; i++)
		if (!pgown[i] && pgpg[i] != TPAPHYSPAGE)
			pgbk[n++] = i + 1;
	if (n < 2) {
		pgbk[0] = pgbk[1] = 0;
		return (0);
	}
	for (n = 0; n < 2; n++)
		pgown[pgbk[n] - 1] = PGBKOWN;
	mapseg(SPLITDSEG, pgpg[pgbk[0] - 1] << 8, 0x02);
	mapseg(SPLITTSEG, pgpg[pgbk[1] - 1] << 8, 0x02);
	return (1);
}

/* Give both banks back to the pool and make their segments trap on any
 * CPU access. */
pgbkfree()
{
	register int n;

	for (n = 0; n < 2; n++)
		if (pgbk[n]) {
			pgown[pgbk[n] - 1] = 0;
			pgbk[n] = 0;
		}
	mapseg(SPLITDSEG, 0, 0x06);	/* SYS | CPU-inhibit */
	mapseg(SPLITTSEG, 0, 0x06);
	return (0);
}

/* Unmap a pool segment; the descriptor becomes System-only. */
static pgunmap(seg, i)
int seg, i;
{
	pgss[pgsegix(seg)] = 0;
	pgks[i] = 0;
	mapseg(seg, pgpg[i] << 8, 0x02);
}

/* Release an allocated slot, by segment or handle, returning 1. Invalid
 * or already-free slots return 0. */
int pgfree(h)
int h;
{
	register int i;

	if ((i = pgslot(h)) < 0)
		return (0);
	pgown[i] = 0;
	pghld[i] = 0;
	if (h < PGPARK)
		pgunmap(h, i);
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
	i = pgsegix(seg);
	return (i >= 0 && pgss[i] && pgown[pgss[i] - 1] == pgcur + 1);
}

/* May the running process read len bytes at seg:off?  In 0x3F only its
 * own stack and those above it are mapped. */
int pgpeek(seg, off, len)
int seg;
unsigned off, len;
{
	if (seg != 0x3f)
		return (1);
	return (off >= (unsigned)sysstk - SSTKSZ && len <= (unsigned)(0 - off));
}

/* Mark a slot as a live process's parked image, or no longer one.  The
 * scheduler sets this on creation and clears it on exit; pgrelall()
 * preserves held slots.  Holding unmaps the segment: the answer is the
 * handle that names the slot from then on, 0 on failure. */
pghold(h, on)
int h, on;
{
	register int i;

	if ((i = pgslot(h)) < 0)
		return (0);
	pghld[i] = on ? 1 : 0;
	if (on && h < PGPARK) {
		pgunmap(h, i);
		h = PGPARK + i;
	}
	return (h);
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

/* Exchange the TPA backing page with an allocated slot's page.
 * Both mappings retain Normal-mode access. Only resident code may call:
 * a caller executing in the TPA would replace its own instructions. */
int pgtpaswap(h)
int h;
{
	register int i, old;

	if ((i = pgslot(h)) < 0)
		return (0);

	old = tpaphys;
	tpaphys = pgpg[i];
	pgpg[i] = old;
	mapseg(TPASEG, tpaphys << 8, 0x00);
	if (h < PGPARK)
		mapseg(h, old << 8, 0x00);
	return (1);
}

/*
 * Put the TPA back on the page crt.s gave it.  A no-op unless a swap is
 * outstanding, and then it is the same exchange run backwards: the slot
 * holding TPAPHYSPAGE takes the page the TPA is on now, and is remapped
 * if it is on a segment.
 */
pgtparest()
{
	register int i, seg;

	if (tpaphys == TPAPHYSPAGE)
		return (0);
	for (i = 0; i < pgnslot; i++)
		if (pgpg[i] == TPAPHYSPAGE) {
			pgpg[i] = tpaphys;
			tpaphys = TPAPHYSPAGE;
			mapseg(TPASEG, TPAPHYSPAGE << 8, 0x00);
			if ((seg = pgsegof(i)) != 0)
				mapseg(seg, pgpg[i] << 8, 0x00);
			return (1);
		}
	return (0);		/* cannot happen: the page went somewhere */
}

/* How many pgalloc() calls would succeed. */
int pgcount()
{
	register int i, n, m;

	n = m = 0;
	for (i = 0; i < pgnslot; i++)
		if (!pgown[i])
			n++;
	for (i = 0; i < PGNSEG; i++)
		if (!pgss[i])
			m++;
	return (n < m ? n : m);
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
	register int k, i;
	register int me;

	me = pgcur + 1;
	for (k = 0; k < PGNSEG; k++)
		if ((i = pgss[k] - 1) >= 0 && pgown[i] == (char)me
		    && !pghld[i])
			pgfree(PGSEG(k));
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
