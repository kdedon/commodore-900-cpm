/*
 * submit.c - Expand a CP/M 3 SUBMIT file for the CCP.
 *
 *   SUBMIT
 *   SUBMIT FILE
 *   SUBMIT FILE P1 P2 ...
 *
 * The source .SUB file is expanded into $$$.SUB. Supported substitutions are
 * $n parameters, $$, ^X control characters, ! command separators, and the '<'
 * program-input marker. The original base-page command tail is retained
 * because argv parsing replaces its separators with NUL bytes.
 *
 * $$$.SUB is written on the SCB temporary drive when configured, otherwise on
 * A:, which is where this CCP reads it.
 */

#include "cpm.h"

#define	SUBEOF	0x1a			/* CP/M end of file		*/
#define	CR	13
#define	LF	10
#define	TAB	9

#define	SCB_TEMPDRIVE	0x50		/* sys/scb.h temp$drive		*/

static char	sstring[SECLEN + 4];	/* the command tail		*/
static int	ssbp;			/* scan position in sstring	*/

static struct fcb	sfcb;		/* the .SUB source		*/
static struct fcb	dfcb;		/* the $$$.SUB output		*/

static char	ibuf[SECLEN];		/* source record		*/
static int	sbp = SECLEN;		/* read position; SECLEN forces a read */
static char	obuf[SECLEN];		/* output record		*/
static int	obp;			/* fill position in obuf	*/

static int	pending;		/* line ends not yet emitted	*/
static char	ln[6];			/* the 5-digit line number	*/
static char	line[48];		/* the prompt's input buffer	*/

static int	scbpb[2];		/* function 49 parameter block	*/

/* one byte of the SCB (function 49, get) */
static int scbgetb(off)
int off;
{
	char	*p;

	p = (char *) scbpb;
	p[0] = (char) off;
	p[1] = 0;
	p[2] = 0;
	p[3] = 0;
	return (__bdos(49, (long) scbpb) & 0xff);
}

static VOID crlf()
{
	cputs("\r\n");
}

/* "00001", bumped once per CR read from the source. */
static VOID bumpline()
{
	register int	i;

	for (i = 4; i >= 0; i--) {
		if (++ln[i] <= '9')
			return;
		ln[i] = '0';
	}
}

/*
 * Abandon the run: report the line, remove the half-built $$$.SUB and
 * quit.
 */
static VOID fatal(msg)
char *msg;
{
	crlf();
	cputs("Error On Line ");
	cputs(ln);
	cputs(" : ");
	cputs(msg);
	crlf();
	__bdos(BDOS_DELETE, (long) &dfcb);
	__bdos(BDOS_WBOOT, 0L);
}

/* the next source character, or SUBEOF at end of file */
static int getsource()
{
	register int	b;

	if (sbp >= SECLEN) {
		setdma(ibuf);
		if ((__bdos(BDOS_READSEQ, (long) &sfcb) & 0xff) != 0)
			return (SUBEOF);
		sbp = 0;
	}
	b = ibuf[sbp++] & 0xff;
	if (b == CR)
		bumpline();
	return (b);
}

/* one byte into $$$.SUB, a record at a time */
static VOID putout(b)
int b;
{
	obuf[obp++] = (char) b;
	if (obp >= SECLEN) {
		setdma(obuf);
		if ((__bdos(BDOS_WRITESEQ, (long) &dfcb) & 0xff) != 0)
			fatal("Disk Write Error");
		obp = 0;
	}
}

/*
 * Line ends are counted, not emitted, so that the CR,LF closing the last
 * line can be dropped without a second pass over the output.
 */
static VOID endline()
{
	pending++;
}

static VOID emit(b)
int b;
{
	while (pending > 0) {
		pending--;
		putout(CR);
		putout(LF);
	}
	putout(b);
}

/* Spaces and tabs are skipped between parameters. */
static VOID deblankparm()
{
	while (sstring[ssbp] == ' ' || sstring[ssbp] == TAB)
		ssbp++;
}

/*
 * True while sstring[ssbp] is part of the current parameter. Tabs delimit
 * parameters only while leading whitespace is skipped; within a parameter,
 * this scanner stops only at a space or NUL.
 */
static int notend()
{
	if (sstring[ssbp] != ' ' && sstring[ssbp] != 0) {
		ssbp++;
		return (1);
	}
	return (0);
}

/*
 * Copy parameter n of the command tail to the output.  Parameter 0 is
 * the .SUB file name itself, because the scan restarts at the head of
 * the tail. A parameter that was never typed expands to nothing.
 */
static VOID subparm(n)
int n;
{
	ssbp = 0;
	deblankparm();
	while (n != 0) {
		n--;
		while (notend())
			;
		deblankparm();
	}
	while (notend())
		emit(sstring[ssbp - 1] & 0xff);
}

/*
 * ^X: '^^' is a literal '^', '^' followed by a character below '@' takes
 * that character minus 20h, '@'..'_' minus 40h, and anything above minus
 * 60h, so both ^C and ^c give 03h.
 */
static VOID subctl()
{
	register int	b;

	b = getsource();
	if (b == '^') {
		emit('^');
		return;
	}
	if (b < '@')
		emit((b - ' ') & 0xff);
	else if (b < '`')
		emit((b - '@') & 0xff);
	else
		emit((b - '`') & 0xff);
}

/* Expand the source into the output stream. */
static VOID expand()
{
	register int	b;
	int		newline, progline, reading;

	reading = 1;
	while (reading) {
		newline = 1;
		progline = 1;
		while ((b = getsource()) != SUBEOF && b != CR) {
			if (b == LF)
				continue;
			if (newline) {
				newline = 0;
				if (b != '<')
					progline = 0;
			}
			if (b == '$') {
				b = getsource();
				if (b == '$') {
					emit('$');
					continue;
				}
				b -= '0';
				if (b < 0 || b > 9)
					fatal("Parameter Error");
				subparm(b);
			} else if (b == '^')
				subctl();
			else if (b == '!' && !progline)
				endline();
			else
				emit(b);
		}
		reading = (b == CR);
		endline();
	}

	/* drop the CR,LF that closed the last line, then mark the end */
	if (pending > 0)
		pending--;
	emit(SUBEOF);
	while (obp != 0)		/* pad the tail of the record	*/
		putout(SUBEOF);
}

/* one line from the console (BDOS function 10) */
static char *getline()
{
	register int	n;

	line[0] = 40;
	line[1] = 0;
	__bdos(BDOS_RDCONBUF, (long) line);
	n = line[1] & 0x7f;
	line[n + 2] = 0;
	crlf();
	return (&line[2]);
}

/*
 * Locate the .SUB file named by the first token of the tail, force its
 * type to SUB and open it (submit.plm:400-437).
 */
static VOID setup()
{
	register char	*p;
	register int	i;
	char		name[20];

	ssbp = 0;
	deblankparm();
	if (sstring[ssbp] == 0) {
		cputs("CP/M 3 SUBMIT Version 3.0\r\n");
		cputs("Enter File to SUBMIT: ");
		p = getline();
		for (i = 0; i < SECLEN && p[i] != 0; i++)
			sstring[i] = p[i];
		sstring[i] = 0;
		ssbp = 0;
		deblankparm();
		if (sstring[ssbp] == 0) {
			cputs("Invalid file name\r\n");
			__bdos(BDOS_WBOOT, 0L);
		}
	}

	for (i = 0; i < 19 && notend(); i++)
		name[i] = sstring[ssbp - 1];
	name[i] = 0;

	mkfcb(name, &sfcb);
	sfcb.ftype[0] = 'S';		/* the type is always SUB	*/
	sfcb.ftype[1] = 'U';
	sfcb.ftype[2] = 'B';

	if ((__bdos(BDOS_OPEN, (long) &sfcb) & 0xff) == 0xff) {
		cputs("ERROR: No 'SUB' File Found\r\n");
		__bdos(BDOS_WBOOT, 0L);
	}
}

/* create $$$.SUB, replacing any leftover copy (submit.plm:626-650) */
static VOID makefile()
{
	register char	*p;
	register int	i;
	int		drv;

	p = (char *) &dfcb;
	for (i = 0; i < sizeof dfcb; i++)
		p[i] = 0;
	dfcb.fname[0] = '$';
	dfcb.fname[1] = '$';
	dfcb.fname[2] = '$';
	for (i = 3; i < 8; i++)
		dfcb.fname[i] = ' ';
	dfcb.ftype[0] = 'S';
	dfcb.ftype[1] = 'U';
	dfcb.ftype[2] = 'B';

	drv = scbgetb(SCB_TEMPDRIVE);
	dfcb.drvcode = (char) (drv != 0 ? drv : 1);

	__bdos(BDOS_DELETE, (long) &dfcb);
	dfcb.extent = 0;
	dfcb.s1 = 0;
	dfcb.s2 = 0;
	dfcb.rcdcnt = 0;
	if ((__bdos(BDOS_MAKE, (long) &dfcb) & 0xff) == 0xff) {
		cputs("ERROR: Directory Full\r\n");
		__bdos(BDOS_WBOOT, 0L);
	}
}

int main(argc, argv)
int argc;
char *argv[];
{
	register int	i, n;

	for (i = 0; i < 5; i++)
		ln[i] = '0';
	ln[4] = '1';
	ln[5] = 0;

	/* the tail, straight from the base page (buff[] is also the DMA
	   buffer, so this must happen before any disk call) */
	n = _base->buff[0] & 0x7f;
	if (n > SECLEN - 1)
		n = SECLEN - 1;
	for (i = 0; i < n; i++)
		sstring[i] = _base->buff[i + 1];
	sstring[n] = 0;

	setup();
	makefile();
	expand();

	if ((__bdos(BDOS_CLOSE, (long) &dfcb) & 0xff) == 0xff) {
		cputs("ERROR: Cannot Close $$$.SUB\r\n");
		return (1);
	}
	return (0);
}
