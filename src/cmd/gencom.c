/*
 */

#include "cpm.h"

#define	GCMAGIC		0xEE05	/* the container (sys/pgmld.c X_GC_MAGIC) */
#define	GCHDRLEN	256	/* the header: two records		*/
#define	GCHDRRECS	2
#define	DESCOFF		16	/* first descriptor			*/
#define	DESCLEN		16
#define	MAXRSX		15	/* v3's own limit (gencom.plm:1646)	*/

#define	RSXMAGIC	0x5253	/* 'RS' at prefix offset 8 (sys/rsxhdr.h) */
#define	RSXHDRLEN	32
#define	H_NEXT		0x0a
#define	H_PREV		0x0c
#define	H_WARMFLG	0x0e
#define	H_NBANK		0x0f
#define	H_NAME		0x10
#define	H_ENDCHAIN	0x18
#define	H_ORG		0x1c
#define	H_LEN		0x1e

/*  The loader's placement rule, which GENCOM has to apply itself
    because it cannot move a module (sys/rsx.c rsxchk; v3's calcdest,
    loader3.asm:615-632).  BPLEN and DEFSTACK are pgmld.c's.	*/

#define	SEGLEN		0x10000L
#define	BPLEN		256
#define	DEFSTACK	256

char	rec[SECLEN];		/* one record in transit		*/
char	hdr[GCHDRLEN];		/* the container header being built	*/
char	pre[SECLEN];		/* record 0 of a program that is NOT a	*/
int	havepre;		/*   container: it had to be read to	*/
				/*   find that out, and it is the	*/
				/*   program's own first record, so it	*/
				/*   is held until the modules are out	*/

struct fcb	pfcb;		/* the program / container		*/
struct fcb	tfcb;		/* TEMP.$$$				*/
struct fcb	mfcb;		/* the .RSX file being read		*/
char		rnfcb[36];	/* function 23 wants two FCBs in one	*/

/*  The module list, in attach order: the entry the loader reaches first
    is the one lowest in memory, and modules attach in this order, so
    entry 0 ends up HIGHEST.  `from' is 0 for a module already in the
    container and k > 0 for one coming from argv[k].		*/

unsigned	morg[MAXRSX], mlen[MAXRSX];
unsigned	mnbank[MAXRSX], mwarm[MAXRSX];
char		mname[MAXRSX][8];
int		mfrom[MAXRSX];
int		oldrec[MAXRSX];	/* records the container already spends	*/
				/*   on module i -- kept apart from	*/
				/*   mlen[], which a replacement	*/
				/*   overwrites with the NEW length	*/
int		nmod;

char		tempnm[] = "TEMP.$$$";

VOID puthex(n)
unsigned n;
{
	register int	i, d;

	for (i = 12; i >= 0; i -= 4) {
		d = (n >> i) & 0xf;
		conout(d < 10 ? '0' + d : 'A' + d - 10);
	}
}

VOID putname(p)
register char *p;
{
	register int	i;

	for (i = 0; i < 8; i++)
		conout(p[i]);
}

/*  Words in the container and in a module prefix are Z8001 words: high
    byte first.  Both buffers are `char' (signed), so every byte is
    masked before it is shifted.  */

unsigned wordat(p, off)
char *p;
int off;
{
	return ((unsigned)(((p[off] & 0xff) << 8) | (p[off + 1] & 0xff)));
}

VOID setword(p, off, v)
char *p;
int off;
unsigned v;
{
	p[off] = (char)(v >> 8);
	p[off + 1] = (char) v;
}

/*  Records a byte count occupies.  A module is padded out to a record
    so the module after it, and the program at the end, start on one.  */

int nrecs(len)
unsigned len;
{
	return ((int)((len + (SECLEN - 1)) / SECLEN));
}

VOID die(msg)
char *msg;
{
	if (msg[0] != 0)
		cputs("gencom: ");
	cputs(msg);
	cputs("\r\n");
	__bdos(BDOS_DELETE, (long) &tfcb);	/* no half-built TEMP.$$$ */
	__bdos(BDOS_WBOOT, 0L);
}

/*  As die(), but for the one window in which TEMP.$$$ is the ONLY copy of
    the program: after the original has been deleted and before the rename
    has put the new file in its place.  Deleting the temporary there -- as
    die() does, correctly, everywhere else -- loses both copies, so this
    one keeps it and says where the program now lives.  */

VOID dielast(msg)
char *msg;
{
	cputs("gencom: ");
	cputs(msg);
	cputs("\r\n");
	cputs("gencom: the new program is in TEMP.$$$ and the old one is\r\n");
	cputs("        gone; rename TEMP.$$$ by hand.\r\n");
	__bdos(BDOS_WBOOT, 0L);
}

/*  Read one record of the currently open file into rec[].  Returns 0 at
    end of file.  */

int getrec(f)
struct fcb *f;
{
	setdma(rec);
	return (__bdos(BDOS_READSEQ, (long) f) == 0);
}

VOID putrec()
{
	setdma(rec);
	if (__bdos(BDOS_WRITESEQ, (long) &tfcb) != 0)
		die("disk write");
}

/*  Copy n records from f to TEMP.$$$.  Short is fatal: the container
    said they were there.  */

VOID copyrecs(f, n)
struct fcb *f;
register int n;
{
	while (--n >= 0) {
		if (!getrec(f))
			die("the file ends inside a module");
		putrec();
	}
}

/*  Read n records past: a module the container is losing, either to a
    replacement or to a strip.  */

VOID skiprecs(f, n)
struct fcb *f;
register int n;
{
	while (--n >= 0)
		if (!getrec(f))
			die("the file ends inside a module");
}

/****************************************************************
 *
 *	The prefix normalisation, gencom.plm:1186-1192.  rec[]
 *	holds the module's first record.
 *
 ****************************************************************/

VOID normalise(i)
int i;
{
	/*  v3: `if iobuff(15) <> 0 then iobuff(14) = 0ffh'.  A module
	    that is only good on a non-banked system is made temporary,
	    so it cannot outlive the program it came with.  */

	if ((rec[H_NBANK] & 0xff) != 0)
		rec[H_WARMFLG] = (char) 0xff;

	setword(rec, H_NEXT, 0);	/* v3 writes 6 -- the 8080 BDOS   */
	setword(rec, H_PREV, 0);	/*   vector; 0 is ours (rsx.c:194)*/
	rec[H_ENDCHAIN] = 0;		/* v3: `iobuff(24) = 0'		  */

	mnbank[i] = (unsigned)(rec[H_NBANK] & 0xff);
	mwarm[i] = (unsigned)(rec[H_WARMFLG] & 0xff);
}

/****************************************************************
 *
 *	Read one .RSX file's prefix into the module list.
 *
 ****************************************************************/

VOID readmod(name, i, argi)
char *name;
int i;
int argi;
{
	register int	k;

	mkfcb(name, &mfcb);
	if ((__bdos(BDOS_OPEN, (long) &mfcb) & 0xff) == 255) {
		cputs("gencom: no such RSX file: ");
		cputs(name);
		die("");
	}
	if (!getrec(&mfcb))
		die("an RSX file is empty");
	__bdos(BDOS_CLOSE, (long) &mfcb);
	setdma(_base->buff);

	if (wordat(rec, 8) != RSXMAGIC)
		die("that is not an RSX (bad prefix magic)");
	morg[i] = wordat(rec, H_ORG);
	mlen[i] = wordat(rec, H_LEN);
	if (mlen[i] < RSXHDRLEN)
		die("the module claims a length shorter than its prefix");
	normalise(i);
	for (k = 0; k < 8; k++)
		mname[i][k] = rec[H_NAME + k];
	mfrom[i] = argi;
}

/****************************************************************
 *
 *	The placement rule, applied to the whole list.  This is the
 *	check v3 does not need: with a relocator the loader picks
 *	each address, and calcdest can always find one or fail
 *	honestly at load time.  Here the addresses are already
 *	chosen, so the failure belongs at bind time.
 *
 ****************************************************************/

VOID checkstack()
{
	register int	i;
	long		top;

	top = SEGLEN;
	for (i = 0; i < nmod; i++) {
		if (morg[i] == 0)
			die("a module is linked for TPA offset 0");
		if ((long) morg[i] + (long) mlen[i] > top - BPLEN - DEFSTACK) {
			cputs("gencom: ");
			putname(mname[i]);
			cputs(" is linked at ");
			puthex(morg[i]);
			cputs(" length ");
			puthex(mlen[i]);
			cputs(", which does not fit below ");
			puthex((unsigned) top);
			cputs("\r\n");
			cputs("gencom: nothing relocates here, so the "
			      "modules must stack as linked\r\n");
			die("bind them highest address first "
			    "(sys/rsxhdr.h note 3)");
		}
		top = (long) morg[i];
	}
}

/****************************************************************
 *
 *	main
 *
 ****************************************************************/

int main(argc, argv)
int argc;
char *argv[];
{
	register int	i, k;
	int		oldn, rc, argi;
	unsigned	off;

	if (argc < 2) {
		cputs("usage: GENCOM prog.Z8K [mod.RSX ...]\r\n");
		return (1);
	}

	mkfcb(tempnm, &tfcb);
	__bdos(BDOS_DELETE, (long) &tfcb);

	mkfcb(argv[1], &pfcb);
	if ((__bdos(BDOS_OPEN, (long) &pfcb) & 0xff) == 255)
		die("no such program file");
	if (!getrec(&pfcb))
		die("the program file is empty");

	/*  Is it already a container?  v3 asks the same question of the
	    same byte: `if iobuff(0) = ret$inst' (gencom.plm:1929).	*/

	oldn = 0;
	if (wordat(rec, 0) == GCMAGIC) {
		for (i = 0; i < SECLEN; i++)
			hdr[i] = rec[i];
		if (!getrec(&pfcb))
			die("the container header is truncated");
		for (i = 0; i < SECLEN; i++)
			hdr[SECLEN + i] = rec[i];
		oldn = (int) wordat(hdr, 2);
		if (oldn < 0 || oldn > MAXRSX)
			die("the container header names an impossible number "
			    "of modules");
		for (i = 0; i < oldn; i++) {
			off = (unsigned)(DESCOFF + i * DESCLEN);
			mlen[i] = wordat(hdr, (int)(off + 2));
			mnbank[i] = (unsigned)(hdr[off + 4] & 0xff);
			for (k = 0; k < 8; k++)
				mname[i][k] = hdr[off + 6 + k];
			morg[i] = wordat(hdr, (int)(off + 14));
			mwarm[i] = 0;		/* re-read from the image  */
			mfrom[i] = 0;
			oldrec[i] = nrecs(mlen[i]);
		}
	} else if ((wordat(rec, 0) & 0xff00) != 0xEE00)
		die("that is not a program file (bad x.out magic)");
	else {
		for (i = 0; i < SECLEN; i++)
			pre[i] = rec[i];
		havepre = 1;
	}

	/*  Naming no module at all is v3's revert: the container goes
	    away and the program it was built from comes back
	    (tear$down, gencom.plm:1322-1340).			*/

	nmod = (argc > 2) ? oldn : 0;

	/*  Merge the command line in.  A name already in the container
	    replaces that entry where it stands; anything else is
	    appended, so the attach order of what was there does not
	    change (v3's remover, gencom.plm:1446-1552).		*/

	for (argi = 2; argi < argc; argi++) {
		if (nmod >= MAXRSX)
			die("there are not enough available RSX slots");
		readmod(argv[argi], nmod, argi);
		for (i = 0; i < nmod; i++) {
			for (k = 0; k < 8; k++)
				if (mname[i][k] != mname[nmod][k])
					break;
			if (k < 8)
				continue;
			if (mfrom[i] != 0) {
				cputs("gencom: ");
				putname(mname[i]);
				die(" is named twice on one command line");
			}
			/*  v3: `Duplicate RSX in header.  Replacing old
			    by new.'  (gencom.plm:381-382)		*/
			cputs("gencom: replacing ");
			putname(mname[i]);
			cputs(" already in the header\r\n");
			morg[i] = morg[nmod];
			mlen[i] = mlen[nmod];
			mnbank[i] = mnbank[nmod];
			mwarm[i] = mwarm[nmod];
			mfrom[i] = argi;
			mfrom[nmod] = -1;	/* consumed in place	*/
			break;
		}
		if (mfrom[nmod] != -1)
			nmod++;
	}

	if (nmod == 0 && oldn == 0)
		die("no header or RSXs to strip");
	checkstack();

	/*  Build the header record pair.  The offsets are computable
	    before anything is written because every module is padded to
	    a whole number of records.				*/

	for (i = 0; i < GCHDRLEN; i++)
		hdr[i] = 0;
	if (nmod > 0) {
		setword(hdr, 0, GCMAGIC);
		setword(hdr, 2, (unsigned) nmod);
		off = GCHDRRECS;
		for (i = 0; i < nmod; i++) {
			k = (int)(DESCOFF + i * DESCLEN);
			setword(hdr, k, off);
			setword(hdr, k + 2, mlen[i]);
			hdr[k + 4] = (char) mnbank[i];
			for (rc = 0; rc < 8; rc++)
				hdr[k + 6 + rc] = mname[i][rc];
			setword(hdr, k + 14, morg[i]);
			off += (unsigned) nrecs(mlen[i]);
		}
	}

	if ((__bdos(BDOS_MAKE, (long) &tfcb) & 0xff) == 255)
		die("no directory space for TEMP.$$$");

	if (nmod > 0) {
		for (i = 0; i < SECLEN; i++)
			rec[i] = hdr[i];
		putrec();
		for (i = 0; i < SECLEN; i++)
			rec[i] = hdr[SECLEN + i];
		putrec();
	}

	/*  The modules, in attach order.  pfcb is positioned at the
	    first old module (record 2 of the container) or at record 1
	    of a plain program file, and the old modules are still in
	    file order, so a module that is not being replaced is copied
	    straight through and one that is has its old records skipped.
	    Nothing is read at random.				*/

	for (i = 0; i < nmod; i++) {
		if (mfrom[i] == 0) {		/* still the container's */
			if (!getrec(&pfcb))
				die("the file ends inside a module");
			normalise(i);
			putrec();
			copyrecs(&pfcb, oldrec[i] - 1);
			continue;
		}
		if (i < oldn)			/* replaced: drop the old */
			skiprecs(&pfcb, oldrec[i]);
		mkfcb(argv[mfrom[i]], &mfcb);
		if ((__bdos(BDOS_OPEN, (long) &mfcb) & 0xff) == 255)
			die("an RSX file went away mid-run");
		rc = nrecs(mlen[i]);
		for (k = 0; k < rc; k++) {
			if (!getrec(&mfcb))
				die("an RSX file is shorter than its prefix "
				    "says");
			if (k == 0)
				normalise(i);
			putrec();
		}
		__bdos(BDOS_CLOSE, (long) &mfcb);
		setdma(_base->buff);
	}

	/*  Any old module the container is losing -- only a strip drops
	    one, so this runs only when nmod is 0 -- and then the program
	    itself, to end of file.				*/

	for (i = nmod; i < oldn; i++)
		skiprecs(&pfcb, oldrec[i]);

	if (havepre) {			/* the record the magic test ate */
		for (i = 0; i < SECLEN; i++)
			rec[i] = pre[i];
		putrec();
	}
	while (getrec(&pfcb))
		putrec();

	/*  The close is the last write TEMP.$$$ gets -- the final extent's
	    directory entry -- so it is the last thing that can fail, and
	    until it has succeeded TEMP.$$$ is not a complete program.  The
	    original must not be deleted before then: die() drops the
	    half-built temporary and leaves the program where it was.  */
	if ((__bdos(BDOS_CLOSE, (long) &tfcb) & 0xff) == 255)
		die("cannot close TEMP.$$$; the program file is untouched");
	__bdos(BDOS_CLOSE, (long) &pfcb);
	setdma(_base->buff);

	/*  MASK THE RETURN.  Every test in this file compared the gate's
	    result with 255 outright, and the BDOS returns the extended
	    error code in the HIGH byte: a delete refused for a password
	    comes back 0FEFFh, which is not 255, so this test passed, the
	    rename below then failed because the file was still there, that
	    test passed too, and GENCOM printed `GENCOM completed.' over a
	    program it had not touched and a TEMP.$$$ it left lying about.
	    Observed on an armed drive; verify-repl is the test.  */
	if ((__bdos(BDOS_DELETE, (long) &pfcb) & 0xff) == 255)
		die("cannot replace the program file");
	/*  Function 23 matches the directory on the FIRST 16 bytes and
	    writes the name from the second (sys/fileio.c rename() takes
	    it from fcb byte 17), so the old name -- TEMP.$$$ -- goes
	    first.  */
	for (i = 0; i < 16; i++) {
		rnfcb[i] = ((char *) &tfcb)[i];
		rnfcb[16 + i] = ((char *) &pfcb)[i];
	}
	if ((__bdos(BDOS_RENAME, (long) rnfcb) & 0xff) == 255)
		dielast("cannot rename TEMP.$$$ over the program file");

	for (i = 0; i < nmod; i++) {
		cputs("gencom: ");
		putname(mname[i]);
		cputs(" at ");
		puthex(morg[i]);
		cputs(" len ");
		puthex(mlen[i]);
		cputs(" nb=");
		puthex(mnbank[i]);
		cputs(mwarm[i] ? " temporary\r\n" : " resident\r\n");
	}
	cputs("GENCOM completed.\r\n");
	return (0);
}
