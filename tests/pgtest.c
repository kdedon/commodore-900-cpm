/* pgtest.c -- the segment pool's sizing and segment numbers, on the host.
 *
 * The emulator has 1 MB of RAM and no way to be given another size, so
 * pginit()'s arithmetic for other machines cannot be run there.  This
 * compiles src/bios/pgalloc.c itself (its "stdio.h" is the host's here; it
 * uses nothing from it) and drives pgsize(), pgalloc() and pgfree() for a
 * ROM report of 512, 1024 and 2560 KB, and for RAM up to the video card.
 * `make pgtest'.  Exit status is the verdict.
 */
#include <stdio.h>
#include <string.h>

static int mseg, mbase, mattr, mcalls;
int mapseg(seg, base, attr) int seg, base, attr;
{ mseg = seg; mbase = base; mattr = attr; mcalls++; return 0; }

#include "pgalloc.c"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("pgtest: FAIL -- " __VA_ARGS__); printf("\n"); } } while (0)

/* A ROM report for `kb' of RAM starting at phys 0x080000, in 1 KB clicks. */
static void machine(const char *label, unsigned kb, int want, const char *segs)
{
	unsigned bram = 512, eram = 512 + kb;
	int n, i, s, seen[64], got = 0;
	char list[256] = "";

	for (i = 0; i < PGNSLOT; i++) { pgown[i] = pghld[i] = 0; pgpg[i] = PGPGLO + i; }
	memset(seen, 0, sizeof seen);
	pgcur = 0;
	n = pgnslot = pgsize(bram, eram);
	CHECK(n == want, "%s: pgsize gave %d slots, want %d", label, n, want);
	while ((s = pgalloc()) != 0) {
		CHECK(s >= 2 && s < 0x30, "%s: slot %d is segment 0x%02X, outside 0x02..0x2F",
		      label, got, s);
		CHECK(s != PGSEGVIDA && s != PGSEGVIDB, "%s: segment 0x%02X is a display plane", label, s);
		CHECK(!seen[s & 63], "%s: segment 0x%02X handed out twice", label, s);
		seen[s & 63] = 1;
		CHECK(mseg == s && mbase == (PGPGLO + got) << 8 && mattr == 0,
		      "%s: slot %d mapped 0x%02X -> page 0x%04X attr %d", label, got, mseg, mbase, mattr);
		CHECK(pgslot(s) == got, "%s: pgslot(0x%02X) = %d, want %d", label, s, pgslot(s), got);
		if (got < 7)
			CHECK(s == 0x28 + got, "%s: slot %d is 0x%02X; the first seven must stay 0x28..0x2E",
			      label, got, s);
		if (strlen(list) < 200) sprintf(list + strlen(list), " %02X", s);
		if (++got > PGNSLOT) break;
	}
	CHECK(got == want, "%s: pgalloc served %d, want %d", label, got, want);
	CHECK(pgcount() == 0, "%s: pgcount %d after exhausting the pool", label, pgcount());
	CHECK(pgfree(0x30) == 0 && pgfree(0x01) == 0 && pgfree(0x00) == 0 && pgfree(0x3A) == 0,
	      "%s: pgfree accepted a resident or ROM segment", label);
	if (want < PGNSLOT)
		CHECK(pgfree(PGSEG(want)) == 0, "%s: pgfree accepted 0x%02X, a slot this machine lacks",
		      label, PGSEG(want));
	for (i = 0; i < got; i++)
		CHECK(pgfree(PGSEG(i)) == 1 && mattr == 2, "%s: pgfree(0x%02X) refused", label, PGSEG(i));
	CHECK(pgcount() == want, "%s: pgcount %d after freeing all, want %d", label, pgcount(), want);
	CHECK(segs == NULL || strcmp(list, segs) == 0,"%s: segments%s, want%s", label, list, segs ? segs : "");
	printf("pgtest: %-8s %4u KB -> %2d slots:%s\n", label, kb, n, list);
}

int main(void)
{
	machine("512 KB", 512, 0, "");
	machine("1024 KB", 1024, 7, " 28 29 2A 2B 2C 2D 2E");
	machine("2560 KB", 2560, 31, NULL);
	machine("ceiling", 0x370000 / 1024 - 512, 38, NULL);	/* RAM to the LR card */
	machine("beyond", 16384, 38, NULL);			/* a report past it */
	CHECK(pgsize(1024, 1536) == 0, "a report starting above CPM.SYS was accepted");
	if (fails) { printf("pgtest: FAIL -- %d check(s)\n", fails); return 1; }
	printf("pgtest: PASS -- 512 KB 0, 1024 KB 7 (0x28..0x2E as before), 2560 KB 31, ceiling %d\n", PGNSLOT);
	return 0;
}
