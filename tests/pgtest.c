/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/* pgtest.c -- the segment pool's sizing and segment numbers, on the host.
 *
 * This compiles the allocator and its sizing itself (their "stdio.h" is
 * the host's here) and drives pgsize(), pgalloc(), pghold() and pgfree()
 * for ROM reports from 512 KB to 16 MB, with and without a loader's map,
 * including maps that must be refused.  `make pgtest'.  Exit status is
 * the verdict.
 */
#include <stdio.h>
#include <string.h>

static int mseg, mbase, mattr, mcalls;
int mapseg(seg, base, attr) int seg, base, attr;
{ mseg = seg; mbase = base; mattr = attr; mcalls++; return 0; }
short sysstk;

#include "pgalloc.c"
#include "pginit.c"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("pgtest: FAIL -- " __VA_ARGS__); printf("\n"); } } while (0)

static void reset(void)
{
	memset(pgown, 0, sizeof pgown);
	memset(pghld, 0, sizeof pghld);
	memset(pgss, 0, sizeof pgss);
	memset(pgks, 0, sizeof pgks);
	pgcur = 0;
	tpaphys = TPAPHYSPAGE;
	pgbk[0] = pgbk[1] = 0;
}

/* A ROM report for `kb' of RAM starting at phys 0x080000, in 1 KB clicks. */
static void machine(const char *label, unsigned kb, int want, int top, const char *segs)
{
	unsigned bram = 512, eram = 512 + kb;
	int n, i, s, seen[64], pages[256], got = 0;
	char list[256] = "";

	reset();
	memset(seen, 0, sizeof seen);
	memset(pages, 0, sizeof pages);
	n = pgnslot = pgsize(bram, eram, NULL, 0);
	CHECK(n == want, "%s: pgsize gave %d slots, want %d", label, n, want);
	CHECK(n <= PGNSLOT, "%s: %d slots overflow PGNSLOT %d", label, n, PGNSLOT);
	for (i = 0; i < n; i++) {
		CHECK(pgpg[i] >= PGPGLO && pgpg[i] != 0x37 && pgpg[i] != 0x3e && pgpg[i] != 0x3f,
		      "%s: slot %d is page 0x%02X", label, i, pgpg[i]);
		CHECK(!pages[pgpg[i]]++, "%s: page 0x%02X twice", label, pgpg[i]);
	}
	CHECK(n == 0 || pgpg[n - 1] == top, "%s: last page 0x%02X, want 0x%02X",
	      label, n ? pgpg[n - 1] : 0, top);
	CHECK(pgcount() == (n < PGNSEG ? n : PGNSEG), "%s: pgcount %d before any allocation", label, pgcount());

	/* Park all but one allocation at a time, so every page gets served. */
	while ((s = pgalloc()) != 0) {
		CHECK(s >= 2 && s < 0x30, "%s: allocation %d is segment 0x%02X, outside 0x02..0x2F",
		      label, got, s);
		CHECK(s != PGSEGVIDA && s != PGSEGVIDB, "%s: segment 0x%02X is a display plane", label, s);
		CHECK(mseg == s && mbase == pgpg[got] << 8 && mattr == 0,
		      "%s: slot %d mapped 0x%02X -> page 0x%04X attr %d", label, got, mseg, mbase, mattr);
		CHECK(pgslot(s) == got, "%s: pgslot(0x%02X) = %d, want %d", label, s, pgslot(s), got);
		if (got < 7)
			CHECK(s == 0x28 + got, "%s: slot %d is 0x%02X; the first seven must stay 0x28..0x2E",
			      label, got, s);
		if (got < 7 && strlen(list) < 200) sprintf(list + strlen(list), " %02X", s);
		CHECK(!seen[s & 63], "%s: segment 0x%02X served while mapped", label, s);
		if (got < PGNSEG / 2)
			seen[s & 63] = 1;		/* scratch: stays mapped */
		else
			CHECK(pghold(s, 1) == PGPARK + got && mattr == 2 && pgss[pgsegix(s)] == 0,
			      "%s: parking slot %d kept segment 0x%02X", label, got, s);
		if (++got > PGNSLOT) break;
	}
	CHECK(got == want, "%s: pgalloc served %d, want %d", label, got, want);
	CHECK(pgcount() == 0, "%s: pgcount %d after exhausting the pool", label, pgcount());
	CHECK(pgfree(0x30) == 0 && pgfree(0x01) == 0 && pgfree(0x00) == 0 && pgfree(0x3A) == 0
	      && pgfree(0x09) == 0 && pgfree(PGPARK + want) == 0,
	      "%s: pgfree accepted a resident, ROM or unknown segment", label);
	for (i = 0; i < got; i++) {
		if (i < PGNSEG / 2)
			s = PGSEG(i);
		else {
			s = PGPARK + i;
			CHECK(pgmine(s, 0, 1, 0) == 0, "%s: pgmine accepted handle 0x%X", label, s);
			CHECK(pghold(s, 0) == s, "%s: unholding 0x%X", label, s);
		}
		mcalls = 0;
		CHECK(pgfree(s) == 1, "%s: pgfree(0x%X) refused", label, s);
		CHECK(s >= PGPARK ? mcalls == 0 : mattr == 2, "%s: pgfree(0x%X) mapping", label, s);
		CHECK(pgfree(s) == 0, "%s: pgfree(0x%X) twice", label, s);
	}
	CHECK(pgcount() == (n < PGNSEG ? n : PGNSEG), "%s: pgcount %d after freeing all", label, pgcount());
	CHECK(segs == NULL || strcmp(list, segs) == 0, "%s: segments%s, want%s", label, list, segs ? segs : "");
	printf("pgtest: %-8s %5u KB -> %3d slots, last page 0x%02X\n", label, kb, n, n ? pgpg[n - 1] : 0);
}

/* A parked process swaps with the TPA without a segment. */
static void swaps(void)
{
	int s, h, pg;

	reset();
	pgnslot = pgsize(512, 512 + 16384, NULL, 0);
	s = pgalloc();
	pg = pgpg[0];
	CHECK(pgtpaswap(s) == 1 && tpaphys == pg && pgpg[0] == TPAPHYSPAGE, "swap by segment");
	h = pghold(s, 1);
	CHECK(pgsegof(0) == 0, "held slot still mapped");
	mcalls = 0;
	CHECK(pgtpaswap(h) == 1 && tpaphys == TPAPHYSPAGE && pgpg[0] == pg && mcalls == 1,
	      "swap by handle touched %d descriptors", mcalls);
	CHECK(pgtpaswap(h) == 1 && pgheld() && pgtparest() == 1 && tpaphys == TPAPHYSPAGE
	      && pgpg[0] == pg && mcalls == 3, "restore onto a parked slot");
	CHECK(pgalloc() == PGSEG(0), "the parked slot's segment was not reused");
	pgcur = 1;
	pgrelproc();
	CHECK(pgown[0] && pgown[1], "another process's slots released");
	pgcur = 0;
	pgrelproc();
	CHECK(pgown[0] && !pgown[1], "held slot released or scratch kept");
}

/* A loader's map for a ROM report of `kb' from phys 0x080000: `want' slots,
 * the last `top', and 0x3E in the pool iff `hr3e'. */
static void mapped(const char *label, unsigned kb, unsigned short *ram, int nram,
		   int want, int top, int hr3e)
{
	int n, i, has3e = 0;

	reset();
	n = pgsize(512, 512 + kb, ram, nram);
	CHECK(n == want, "%s: %d slots, want %d", label, n, want);
	CHECK(n == 0 || pgpg[n - 1] == top, "%s: last page 0x%02X, want 0x%02X",
	      label, n ? pgpg[n - 1] : 0, top);
	for (i = 0; i < n; i++) {
		CHECK(pgpg[i] >= PGPGLO && pgpg[i] != PGLRTEXT && (i == 0 || pgpg[i] > pgpg[i - 1]),
		      "%s: slot %d is page 0x%02X", label, i, pgpg[i]);
		has3e |= pgpg[i] == 0x3e;
	}
	CHECK(has3e == hr3e, "%s: page 0x3E %s", label, has3e ? "taken" : "left out");
	printf("pgtest: map %-22s -> %3d slots\n", label, n);
}

static void maps(void)
{
	/* What kboot lists: the ROM's range less 0x00-0x07, 0x37, 0x3F and,
	 * with an HR board, 0x3E. */
	unsigned short nohr[] = { 0x08, 0x2f, 0x38, 7 };
	unsigned short hr[] = { 0x08, 0x2f, 0x38, 6 };
	unsigned short m25[] = { 0x08, 40 };
	unsigned short resv[] = { 0x08, 0x10, 0x20, 0x17, 0x38, 7 };	/* reserve 18 1F */
	unsigned short lrtext[] = { 0x08, 0x37 };
	unsigned short past[] = { 0x08, 0x2f, 0x38, 9 };
	unsigned short under[] = { 0x00, 0x37 };
	unsigned short down[] = { 0x38, 7, 0x08, 0x2f };
	unsigned short overlap[] = { 0x08, 0x2f, 0x30, 2 };
	unsigned short empty[] = { 0x08, 0x2f, 0x38, 0 };
	unsigned short huge[] = { 0x08, 0xffff };

	mapped("4 MB, no HR", 3584, nohr, 2, 50, 0x3e, 1);
	mapped("4 MB, HR", 3584, hr, 2, 49, 0x3d, 0);
	mapped("2.5 MB", 2560, m25, 1, 35, 0x2e, 0);
	mapped("4 MB, reserve 18-1F", 3584, resv, 3, 42, 0x3e, 1);
	mapped("none", 3584, NULL, 0, 49, 0x3d, 0);
	mapped("rejected: LR text", 3584, lrtext, 1, 49, 0x3d, 0);
	mapped("rejected: past the ROM", 3584, past, 2, 49, 0x3d, 0);
	mapped("rejected: below the ROM", 3584, under, 1, 49, 0x3d, 0);
	mapped("rejected: descending", 3584, down, 2, 49, 0x3d, 0);
	mapped("rejected: overlapping", 3584, overlap, 2, 49, 0x3d, 0);
	mapped("rejected: empty run", 3584, empty, 2, 49, 0x3d, 0);
	mapped("rejected: huge run", 3584, huge, 1, 49, 0x3d, 0);
}

/* The split-I/D banks: two slots, never the boot TPA page, not parked handles. */
static void banks(void)
{
	int n;

	reset();
	pgnslot = pgsize(512, 512 + 512, NULL, 0);
	pgpg[0] = TPAPHYSPAGE;			/* as after a swap */
	n = pgcount();
	CHECK(pgbanks() == 1 && pgcount() == n - 2, "banks: two slots not taken");
	CHECK(pgbk[0] != 1 && pgbk[1] != 1, "banks: took the boot TPA page");
	CHECK(mseg == SPLITTSEG && mattr == 2, "banks: side table mapped 0x%02X attr %d", mseg, mattr);
	CHECK(pgslot(PGPARK + pgbk[0] - 1) < 0, "banks: a bank named as a parked slot");
	CHECK(pgbanks() == 1 && pgcount() == n - 2, "banks: taken twice");
	CHECK(pgalloc() != 0 && pgbanks() == 1, "banks: lost");
	pgbkfree();
	CHECK(pgcount() == n - 1, "banks: not given back");
	CHECK(mseg == SPLITTSEG && mattr == 6, "banks: side table left mapped, attr %d", mattr);
	CHECK(pgalloc() != 0 && pgalloc() != 0 && pgbanks() == 0,
	      "banks: taken from a pool without two free slots");
	pgbkfree();
}

/* The CCP cache: a free slot while one is spare, given back when the pool
 * runs dry unless the loader has it mapped. */
static void ccpcache(void)
{
	int i, n;

	reset();
	pgcc = 0;
	pgnslot = n = pgsize(512, 512 + 512, NULL, 0);
	CHECK(pgccmap() == 1 && mseg == CCPCSEG && mattr == 2, "ccpc: not mapped");
	pgccunmap();
	CHECK(mseg == CCPCSEG && mattr == 6, "ccpc: left mapped, attr %d", mattr);
	CHECK(pgcount() == n, "ccpc: a spare cache page not counted free");
	CHECK(pgslot(PGPARK + pgcc - 1) < 0, "ccpc: the cache named as a parked slot");
	ccpcok = 1;
	for (i = 0; i < n; i++)
		CHECK(pgalloc() != 0, "ccpc: allocation %d refused", i);
	CHECK(pgcc == 0 && ccpcok == 0, "ccpc: the cache kept its page");
	CHECK(pgccmap() == 0, "ccpc: mapped with the pool dry");
	reset();
	pgcc = 0;
	pgnslot = n;
	CHECK(pgccmap() == 1, "ccpc: not mapped again");
	for (i = 0; i < n - 1; i++)
		pgalloc();
	CHECK(pgalloc() == 0 && pgcc != 0, "ccpc: a mapped cache was taken");
	pgccunmap();
	CHECK(pgbanks() == 0 && pgcc == 0, "ccpc: banks did not reclaim the cache");
}

int ccpcok;

int main(void)
{
	machine("512 KB", 512, 4, 0x0e, " 28 29 2A 2B");
	machine("1024 KB", 1024, 11, 0x16, " 28 29 2A 2B 2C 2D 2E");
	machine("2560 KB", 2560, 35, 0x2e, NULL);
	machine("4 MB", 4096 - 512, 49, 0x3d, NULL);	/* 0x0B..0x0E, 0x10..0x3E, less 0x37 */
	machine("16 MB", 16384 - 512, PGNSLOT, 0xfe, NULL);
	machine("beyond", 65000, PGNSLOT, 0xfe, NULL);
	CHECK(pgsize(1024, 1536, NULL, 0) == 0, "a report starting above CPM.SYS was accepted");
	swaps();
	maps();
	banks();
	ccpcache();
	if (fails) { printf("pgtest: FAIL -- %d check(s)\n", fails); return 1; }
	printf("pgtest: PASS -- 512 KB 4, 1024 KB 11 (0x28..0x2E first), 16 MB %d, holes skipped\n", PGNSLOT);
	return 0;
}
