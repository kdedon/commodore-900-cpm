/*
 * initdir.c -- INITDIR: give a drive CP/M 3 date and time stamping.
 *
 *
 */

#include "cpm.h"

#define	NENTREC	4		/* directory entries per 128-byte record	*/
#define	ENTSIZE	32
#define	MAXENT	512		/* entries this INITDIR can index	*/

#define	T_FREE	0xe5		/* free slot -- exactly 0E5h		*/
#define	T_SFCB	0x21		/* stamps for the 3 preceding entries	*/

/*
 * The disk parameter block as function 31 copies it out
 * (sys/bdosdef.h `struct dpb').  Members word-align on the Z8001, so
 * `dpbdum' is a real field and not padding to be dropped.
 */
struct dpb {
	unsigned spt;		/* 128-byte records per track		*/
	char	bsh, blm, exm, dpbdum;
	unsigned dsm;		/* highest block number			*/
	unsigned drm;		/* highest directory entry number	*/
	unsigned dir_al;
	unsigned cks;
	unsigned trk_off;	/* first track of this drive on the	*/
};				/*   device				*/

static struct dpb	dpb;
static char	rec[SECLEN];	/* the record being converted		*/
static char	rec2[SECLEN];	/* the record a relocation lands in	*/
static char	freebm[MAXENT / 8];	/* free non-4th slots, from pass 1 */
static int	nextfr;		/* next bit to try in freebm		*/
static int	drive;		/* 1..16, the drive being formatted	*/
static int	curdisk;	/* the drive to give back at the end	*/
static int	nrec;		/* directory records on this drive	*/

/* ---------------------------------------------------------------- */

static VOID crlf()
{
	conout('\r');
	conout('\n');
}

static VOID say(s)
char *s;
{
	cputs(s);
	crlf();
}

/*
 * One console line, uppercased, first non-blank character returned.
 * v3's `get list(yesno)' reads a single token; reading to the CR here
 * means a scripted session's line terminator is consumed rather than
 * left to be read as the answer to the NEXT question.
 */
static int askchar()
{
	register int	c, ans;

	ans = 0;
	while ((c = conin()) != '\r' && c != '\n') {
		if (c == 0)
			break;
		if (ans == 0 && c != ' ' && c != '\t')
			ans = (c >= 'a' && c <= 'z') ? c - 'a' + 'A' : c;
	}
	crlf();
	return (ans);
}

/* ---------------------------------------------------------------- */

/*
 * The one call this program exists for.  Refusals from the BDOS
 * (0FFFFFFFFh, sys/iosys.c bioscl) are fatal and loud: every code used
 * here is on the allowed list, so a refusal means the resident system
 * and this program disagree about the interface, and carrying on would
 * mean writing to a drive whose geometry we may not have.
 */
static long biosc(code, p1, p2)
int code;
long p1, p2;
{
	struct biospb	pb;
	long		r;

	pb.code = code;
	pb.p1 = p1;
	pb.p2 = p2;
	r = __bdosl(BDOS_BIOSCALL, (long) &pb);
	if (r == BIOS_REFUSED) {
		say("ERROR: BDOS function 50 refused BIOS call");
		putdec((unsigned) code);
		crlf();
		say("INITDIR TERMINATED.");
		__bdos(BDOS_WBOOT, 0L);
	}
	return (r);
}

/*
 * Directory record n (128 bytes, four entries) <-> disk.
 * INITDIR.PLI:998-1034 does the same arithmetic against a physical
 * sector holding several records; this BIOS deblocks internally and
 * takes a 128-byte record directly (src/bios900.c dskread/dskwrite), so
 * there is no physical-sector blocking factor here and no
 * TPA-sized directory buffer: one record in, one record out.  It is
 * also why the whole directory never has to be in memory at once.
 */
static VOID seekrec(n)
int n;
{
	biosc(BIOS_SETTRK, (long) (dpb.trk_off + n / dpb.spt), 0L);
	biosc(BIOS_SETSEC,
	      biosc(BIOS_SECTRAN, (long) (n % dpb.spt), 0L), 0L);
}

static VOID readrec(n, buf)
int n;
char *buf;
{
	seekrec(n);
	biosc(BIOS_SETDMA, (long) buf, 0L);
	if (biosc(BIOS_READ, 0L, 0L) != 0L) {
		say("ERROR: directory read failed.");
		say("INITDIR TERMINATED.");
		__bdos(BDOS_WBOOT, 0L);
	}
}

static VOID writerec(n, buf)
int n;
char *buf;
{
	seekrec(n);
	biosc(BIOS_SETDMA, (long) buf, 0L);
	/* mode 1 = directory write: written through the BIOS buffer cache
	   rather than left dirty, which is what INITDIR.PLI:1029 asks for
	   (`wrsec(1)') and what the BDOS's own dir_wr uses. */
	if (biosc(BIOS_WRITE, 1L, 0L) != 0L) {
		say("ERROR: directory write failed -- the directory on this");
		say("drive is now PARTLY converted.  Do not use it until it");
		say("has been checked.");
		say("INITDIR TERMINATED.");
		__bdos(BDOS_WBOOT, 0L);
	}
}

/* ---------------------------------------------------------------- */

/*
 * Give the drive back exactly as v3 does (INITDIR.PLI:987-993
 * `restore'), and this is the part that is easy to leave out and
 * impossible to see: the BDOS has been caching this drive's directory
 * behind our back and every cache is now wrong.
 *
 *   - the BIOS buffer cache holds the records we wrote (they went
 *     through it, so it is consistent, not stale) -- FLUSH pushes them
 *     to the medium before anything else happens;
 *   - `seldsk(curdisk)' puts the BIOS back on the drive it had;
 *   - BDOS function 13 (v3's `reset') is what actually invalidates:
 *     it clears log_dsk, so the next select re-logs the drive and
 *     rebuilds its allocation vector, its directory hash signatures and
 *     its label/SFCB flags from the medium, and it poisons curdsk, so
 *     that select also calls dirdrop() and forgets which record the
 *     directory buffer holds.  Without it the BDOS answers from a
 *     signature table that still says a relocated file is in its old
 *     slot -- the file is then not found, on a disk where it is
 *     perfectly present.  Nothing else does this job: the warm boot at
 *     exit deliberately does NOT reset the disk system (sys/bdosmisc.c
 *     warmboot).
 *   - function 13 also sets the default drive to A:, so the drive the
 *     user was on is selected again afterwards -- v3's `select(curdisk)'
 *     on the next line.
 */
static VOID restore()
{
	biosc(BIOS_FLUSH, 0L, 0L);
	biosc(BIOS_SELDSK, (long) curdisk, 1L);
	__bdos(BDOS_RESET, 0L);
	__bdos(BDOS_SELDSK, (long) curdisk);
}

static VOID errprint(msg)
char *msg;
{
	crlf();
	cputs("ERROR: ");
	say(msg);
	say("INITDIR TERMINATED.");
	restore();
	__bdos(BDOS_WBOOT, 0L);
}

/* ---------------------------------------------------------------- */

/*
 * Pass 1 -- INITDIR.PLI:469 `countdir'.  Classify every slot without
 * writing anything: how many SFCBs are already there, how many
 * occupied 4th slots have to be moved out of the way, and which
 * non-4th slots are free to move them into.  The answer decides
 * whether the run happens at all.
 */
static int nsfcb, nreloc, nfree, n4th;

static VOID countdir()
{
	register int	r, k, i;
	int		t;

	nsfcb = nreloc = nfree = n4th = 0;
	for (i = 0; i < MAXENT / 8; i++)
		freebm[i] = 0;

	for (r = 0; r < nrec; r++) {
		readrec(r, rec);
		for (k = 0; k < NENTREC; k++) {
			i = r * NENTREC + k;
			if (i > (int) dpb.drm)
				break;
			t = rec[k * ENTSIZE] & 0xff;
			if ((i & 3) == 3) {
				n4th++;
				if (t == T_SFCB)
					nsfcb++;
				else if (t != T_FREE)
					nreloc++;
			} else if (t == T_FREE) {
				nfree++;
				freebm[i >> 3] |= 1 << (i & 7);
			}
		}
	}
}

/* The lowest still-free non-4th slot, or -1.  host/mkcpmfs.py's
   free_slot() rescans the live buffer each time and so returns exactly
   this sequence: relocation only ever consumes free slots, never
   creates one. */
static int freeslot()
{
	while (nextfr < MAXENT) {
		if (freebm[nextfr >> 3] & (1 << (nextfr & 7))) {
			freebm[nextfr >> 3] &= ~(1 << (nextfr & 7));
			return (nextfr++);
		}
		nextfr++;
	}
	return (-1);
}

static VOID copyent(dst, src)
register char *dst, *src;
{
	register int	i;

	for (i = 0; i < ENTSIZE; i++)
		*dst++ = *src++;
}

/*
 * Move the entry at `src' into directory slot `dst'.  `rrec' is the
 * record currently held in rec[]; when dst falls inside it the move is
 * done in place, which also makes the whole conversion of that record
 * a single write.
 *
 * Any stale SFCB sub-record at the destination is zeroed, matching
 * mkcpmfs.py's clear_sfcb() after the same move.  Entry dst's SFCB is
 * entry (dst | 3), which is always in dst's own record, so this never
 * needs a third buffer.  The entry being moved brings no stamps with
 * it: it was sitting in a 4th slot, which is the SFCB's own slot, so
 * nothing ever stamped it.
 */
static VOID putent(dst, src, rrec)
int dst;
char *src;
int rrec;
{
	register char	*b;
	register int	slot;

	slot = dst & 3;
	if ((dst >> 2) == rrec) {
		b = rec;
	} else {
		b = rec2;
		readrec(dst >> 2, b);
	}
	copyent(b + slot * ENTSIZE, src);
	if ((b[3 * ENTSIZE] & 0xff) == T_SFCB) {
		register int	i, o;

		o = 3 * ENTSIZE + 1 + 10 * slot;
		for (i = 0; i < 10; i++)
			b[o + i] = 0;
	}
	if (b == rec2)
		writerec(dst >> 2, b);
}

/*
 * Pass 2 -- reserve every 4th slot.  Ordering is deliberate: when a
 * relocation crosses records the DESTINATION record is written first,
 * so the window in which a power failure could matter leaves the entry
 * in TWO places rather than in none.  A duplicate is repairable by
 * hand; a lost FCB is a lost file.  v3 has the same window
 * (INITDIR.PLI:376-386 writes the rebuilt sector before clearing the
 * old one, and its comment says so).
 */
static int moved;

static VOID buildnew()
{
	register int	r, k, i;
	int		t, dst, j;

	moved = 0;
	nextfr = 0;
	for (r = 0; r < nrec; r++) {
		readrec(r, rec);
		k = 3;
		i = r * NENTREC + k;
		if (i > (int) dpb.drm)
			break;
		t = rec[k * ENTSIZE] & 0xff;
		if (t == T_SFCB)
			continue;
		if (t != T_FREE) {
			dst = freeslot();
			if (dst < 0)		/* countdir proved otherwise */
				errprint("Not enough room in directory.");
			putent(dst, rec + k * ENTSIZE, r);
			moved++;
		}
		rec[k * ENTSIZE] = (char) T_SFCB;
		for (j = 1; j < ENTSIZE; j++)
			rec[k * ENTSIZE + j] = 0;
		writerec(r, rec);
	}
}

/* ---------------------------------------------------------------- */

static VOID putnum(label, n)
char *label;
unsigned n;
{
	cputs(label);
	putdec(n);
	crlf();
}

int main(argc, argv)
int argc;
char **argv;
{
	int	c;

	crlf();
	say("INITDIR WILL ACTIVATE TIME STAMPS FOR SPECIFIED DRIVE.");

	/* The block the BDOS reads for function 50 must be exactly five
	   words: one for the code and two per LONG.  It is built by the C
	   compiler and read by hand-written assembly (sys/bdosglue.s
	   `ldm r3,(rr2),$5'), so the two have to agree about member
	   alignment -- check rather than assume. */
	if (sizeof (struct biospb) != 10) {
		say("ERROR: struct biospb is not a 5-word block.");
		return (1);
	}

	curdisk = __bdos(BDOS_CURDSK, 0L);	/* restore BIOS to this  */
						/*   (INITDIR.PLI:269)   */

	/* The drive, and it is never defaulted: this program rewrites a
	   directory, so a command that named no drive is asked
	   (INITDIR.PLI:288-321 `wrongdisk' / `getdisk') rather than
	   guessed at.
	   C900 deviation: v3 reads the drive out of the base page's first
	   FCB (`dfcb0()', :272-274), where 0 means "none named".  Our CCP
	   cannot say that -- sys/ccp.c fill_fcb:538 puts the CURRENT drive
	   in the byte whenever the command line omits one, so a base-page
	   FCB here always names a drive and "INITDIR" alone would silently
	   format whatever drive the user happened to be on.  The command
	   tail is read instead, which is the same question asked of a
	   source that can still answer "nothing was named". */
	drive = 0;
	if (argc > 1) {
		c = argv[1][0] & 0xff;
		if (c >= 'a' && c <= 'z')
			c = c - 'a' + 'A';
		if (c >= 'A' && c <= 'P'
		    && (argv[1][1] == 0 || (argv[1][1] == ':'
					    && argv[1][2] == 0)))
			drive = c - 'A' + 1;
	}
	while (drive < 1 || drive > 16) {
		crlf();
		cputs("ERROR: Unrecognized drive.");
		if (argc > 1) {
			cputs("  DRIVE: ");
			cputs(argv[1]);
		}
		crlf();
		cputs("Enter Drive: ");
		c = askchar();
		drive = (c >= 'A' && c <= 'P') ? c - 'A' + 1 : 0;
		argc = 0;		/* the command line has had its say */
	}

	cputs("Do you want to re-format the directory on drive: ");
	conout('A' + drive - 1);
	cputs("  (Y/N)?  ");
	c = askchar();
	if (c != 'Y') {
		say("INITDIR TERMINATED.");
		return (0);
	}

	if (__bdos(BDOS_ROVEC, 0L) & (1 << (drive - 1)))
		errprint("Disk is READ ONLY.");

	/* Select the drive, then take its geometry.  Function 31 always
	   answers for the DEFAULT drive (sys/bdosmain.c case 31), so the
	   select is not decoration -- without it this would format one
	   drive using another's track offset. */
	__bdos(BDOS_SELDSK, (long) (drive - 1));
	__bdos(BDOS_GETDPB, (long) &dpb);

	if (biosc(BIOS_SELDSK, (long) (drive - 1), 1L) == 0L)
		errprint("Cannot select drive.");	/* :175, :1046 */

	/* Report the drive as the SYSTEM describes it, not as the command
	   line spelled it.  Two drive letters here differ in size and in
	   where they start, so these three numbers are the proof that the
	   directory about to be rewritten belongs to the named drive. */
	cputs("Drive ");
	conout('A' + drive - 1);
	cputs(": ");
	putnum("directory entries: ", (unsigned) dpb.drm + 1);
	putnum("  disk blocks: ", (unsigned) dpb.dsm + 1);
	putnum("  track offset: ", dpb.trk_off);

	if ((unsigned) dpb.drm + 1 > MAXENT)
		errprint("Directory is larger than this INITDIR can index.");
	if (dpb.spt == 0)
		errprint("Drive reports no records per track.");

	nrec = ((int) dpb.drm + NENTREC) / NENTREC;

	countdir();
	putnum("Existing time stamp entries: ", (unsigned) nsfcb);
	putnum("Entries to relocate: ", (unsigned) nreloc);
	putnum("Free slots available: ", (unsigned) nfree);

	/* Already done: every 4th slot is an SFCB.  v3 reaches the same
	   state through `query' (:578) and answers `errnotnew' (:171). */
	if (nreloc == 0 && nsfcb == n4th && n4th != 0)
		errprint("Directory already re-formatted.");

	/* The refusal that matters.  Decided from pass 1, before a single
	   byte has been written, because a directory this program gives up
	   halfway through is a directory with files missing from it. */
	if (nreloc > nfree)
		errprint("Not enough room in directory.");

	say("End of PASS 1.");

	buildnew();

	putnum("Time stamp entries created: ", (unsigned) n4th);
	putnum("Entries relocated: ", (unsigned) moved);
	putnum("File slots now usable: ",
	       (unsigned) (((int) dpb.drm + 1) - n4th));

	restore();
	say("INITDIR complete.");
	return (0);
}
