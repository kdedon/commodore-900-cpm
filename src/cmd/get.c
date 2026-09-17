/*
 * Portions Copyright (c) 2026 Kevin Dedon.
 */
/*
 * get.c - GET.Z8K: console input from a file, on the RSX mechanism.
 *
 *	GET [CONSOLE] [INPUT] [FROM] FILE name [[ECHO]|[NO ECHO]]
 *	GET [CONSOLE] [INPUT] [FROM] CONSOLE
 *
 * v3's GET is get.plm: a transient that parses the command, opens the
 * file, and hands both to a module it carries stapled to its own .COM
 * file (getrsx.asm, attached by the LOADER RSX).  There is no GENCOM
 * descriptor and no relocator here (src/bdos/rsxhdr.h note 3), so the
 * module is a separate flat file, GET.RSX, linked for one fixed TPA
 * offset; this program reads it, patches the two fields the module
 * leaves for it, and asks the system to link it -- BDOS function 60
 * sub-function 127, which is RSXLDR.Z8K's route (src/cmd/rsxldr.c).
 *
 * The module's own sub-functions are v3's numbers: 129 stops it and 130
 * asks whether it is there.  A program written against CP/M 3 therefore
 * finds them where it expects.
 *
 * WHAT IS AND IS NOT IMPLEMENTED.  [ECHO] and [NO ECHO] are.  The
 * others are refused BY NAME rather than accepted and ignored:
 *
 *   [SYSTEM] is accepted, because it describes what this GET does: the
 *	CCP is a transient here and goes through the RSX chain like
 *	any program (src/bdos/rsx.c), so the file feeds the command line
 *	as well as the program.
 *   [PROGRAM] is refused.  It is v3's default and means the opposite --
 *	the CCP reads the keyboard while the program reads the file --
 *	and telling the two apart needs the resident CCP's @CCPFLG bit,
 *	which a transient CCP does not set.
 *   [FILTERED], [NOT FILTERED] and [RAW] are refused: nothing here
 *	strips control characters out of the stream.
 *   AUXILIARY / AUXIN: are refused; this system has no AUX: to read.
 */

#include "cpm.h"

#define	RSXMAGIC	0x5253
#define	RSXHDRLEN	32
#define	RSX_ATTACH	127
#define	BDOS_CALLRSX	60

/* the module's sub-functions -- ref/cpm3/getrsx.asm:92-104 */
#define	GET_KILL	129
#define	GET_FCB		130

/* prefix offsets (src/bdos/rsxhdr.h) and the two fields src/cmd/getrsx.s
   leaves for us immediately after the prefix */
#define	H_MAGIC		8
#define	H_WARMF		0x0e
#define	H_ORG		0x1c
#define	H_LEN		0x1e
#define	H_ECHO		0x20
#define	H_FCB		0x22

#define	MODNAME		"GET.RSX"

struct rsxpb {
	char		rpfunc;
	char		rprsvd;
	unsigned	rporg;
	unsigned	rplen;
	long		rpsrc;
};

char		buf[2048];		/* the module image, as read	*/
struct fcb	filefcb;		/* the file the module reads	*/
struct fcb	tryfcb;			/* a copy, used to prove it opens */
struct rsxpb	pb;

/* the module's words are Z8001 words: high byte first */
static unsigned wordat(off)
int off;
{
	return (((buf[off] & 0xff) << 8) | (buf[off + 1] & 0xff));
}

/* one BDOS function 60 sub-function call with no other argument */
static unsigned subfn(n)
int n;
{
	pb.rpfunc = (char) n;
	pb.rprsvd = 0;
	return ((unsigned) __bdos(BDOS_CALLRSX, (long) &pb));
}

/* is GET.RSX resident?  The module answers sub-function 130 with the TPA
   offset of its FCB; with no module in the chain the BDOS answers
   RSX_NOTHANDLED (0FFh) instead (src/bdos/rsx.c rsxfn). */
static int active()
{
	register unsigned r;

	r = subfn(GET_FCB);
	return (r != 0 && r != 0xff);
}

/* an argument compared against a keyword, with the brackets, commas and
   equals signs a CP/M 3 command line puts around its options taken off
   first: the CCP splits the tail at blanks only, so `[NO ECHO]' arrives
   as the two words `[NO' and `ECHO]'. */
static int kwis(a, k)
register char *a;
register char *k;
{
	while (*a == '[' || *a == ']' || *a == ',' || *a == '=')
		a++;
	while (*k != 0) {
		if (*a++ != *k++)
			return (0);
	}
	while (*a == '[' || *a == ']' || *a == ',' || *a == '=')
		a++;
	return (*a == 0);
}

static VOID say(s)
char *s;
{
	cputs(s);
}

static VOID sayfcb(f)
register struct fcb *f;
{
	char	nm[13];
	register int i, j;

	j = 0;
	for (i = 0; i < 8 && f->fname[i] != ' '; i++)
		nm[j++] = f->fname[i];
	if (f->ftype[0] != ' ') {
		nm[j++] = '.';
		for (i = 0; i < 3 && f->ftype[i] != ' '; i++)
			nm[j++] = f->ftype[i];
	}
	nm[j] = 0;
	say(nm);
}

static VOID usage()
{
	say("usage: GET [CONSOLE] [INPUT] [FROM] FILE name [[ECHO]|[NO ECHO]]\r\n");
	say("       GET [CONSOLE] [INPUT] [FROM] CONSOLE\r\n");
}

/* read GET.RSX, patch the FCB and the echo flag into it, and attach it */
static int loadmod(echo)
int echo;
{
	register int	n, i;
	register unsigned org, len;
	register char	*p;

	mkfcb(MODNAME, &tryfcb);
	/* Mask before comparing: a directory function's failure return puts
	 * the extended error code in the HIGH byte (src/bdos/bdosmain.c
	 * returns 0x04ff and 0x02ff literally), so an unmasked `== 0xff'
	 * does not merely lose the code -- it misses the failure entirely
	 * and walks on with an FCB that never opened.  Twenty other sites
	 * in src/cmd already mask; these two were the exceptions. */
	if ((__bdos(BDOS_OPEN, (long) &tryfcb) & 0xff) == 0xff) {
		say("GET: cannot find ");
		say(MODNAME);
		say(" on the default drive\r\n");
		return (1);
	}
	for (n = 0; n < sizeof buf; n += SECLEN) {
		setdma(&buf[n]);
		if (__bdos(BDOS_READSEQ, (long) &tryfcb) != 0)
			break;
	}
	__bdos(BDOS_CLOSE, (long) &tryfcb);
	setdma(_base->buff);
	if (n < RSXHDRLEN || wordat(H_MAGIC) != RSXMAGIC) {
		say("GET: ");
		say(MODNAME);
		say(" is not a resident system extension\r\n");
		return (1);
	}
	org = wordat(H_ORG);
	len = wordat(H_LEN);
	if (len > (unsigned) n) {
		say("GET: ");
		say(MODNAME);
		say(" is shorter than the module claims\r\n");
		return (1);
	}

	buf[H_ECHO] = (char) (echo ? 1 : 0);
	p = (char *) &filefcb;
	for (i = 0; i < sizeof (struct fcb); i++)
		buf[H_FCB + i] = p[i];

	pb.rpfunc = RSX_ATTACH;
	pb.rprsvd = 0;
	pb.rporg = org;
	pb.rplen = len;
	pb.rpsrc = (long) buf;
	if (__bdos(BDOS_CALLRSX, (long) &pb) != 0) {
		say("GET: there is no room for the module below the resident\r\n");
		say("     chain -- modules cannot move here, so remove the\r\n");
		say("     others first (a warm boot drops the temporary ones)\r\n");
		return (1);
	}
	return (0);
}

int main(argc, argv)
int argc;
char *argv[];
{
	register int	i;
	int		echo, tocons, fileat;

	echo = 1;
	tocons = 0;
	fileat = 0;

	for (i = 1; i < argc; i++) {
		if (fileat == i) {		/* the file name itself */
			continue;
		}
		if (kwis(argv[i], "CONSOLE") || kwis(argv[i], "CON:")) {
			/*  Two different CONSOLEs share the word: the one
			    before FROM names what is being redirected and
			    is the only thing this system has, and the one
			    after it means `stop'.  A FILE seen already
			    settles which this is.  */
			if (fileat != 0)
				continue;
			tocons = 1;
			continue;
		}
		if (kwis(argv[i], "INPUT") || kwis(argv[i], "FROM"))
			continue;
		if (kwis(argv[i], "FILE")) {
			if (i + 1 >= argc) {
				say("GET: FILE needs a file name\r\n");
				return (1);
			}
			fileat = i + 1;
			tocons = 0;
			continue;
		}
		if (kwis(argv[i], "ECHO")) {
			continue;		/* NO/NOT already saw it */
		}
		if (kwis(argv[i], "NO") || kwis(argv[i], "NOT")) {
			echo = 0;
			continue;
		}
		if (kwis(argv[i], "SYSTEM"))
			continue;		/* what this GET already is */
		if (kwis(argv[i], "PROGRAM")) {
			say("GET: [PROGRAM] is not implemented.  The CCP is a\r\n");
			say("     transient here and reads through the same RSX\r\n");
			say("     chain as a program, so this GET is always\r\n");
			say("     [SYSTEM] and cannot feed one and not the other\r\n");
			return (1);
		}
		if (kwis(argv[i], "FILTERED") || kwis(argv[i], "RAW")) {
			say("GET: [FILTERED] and [RAW] are not implemented; the\r\n");
			say("     file is served byte for byte as it stands\r\n");
			return (1);
		}
		if (kwis(argv[i], "AUXILIARY") || kwis(argv[i], "AUXIN:")
		    || kwis(argv[i], "AUX:")) {
			say("GET: this system has no AUX: device to read\r\n");
			return (1);
		}
		if (fileat == 0 && !tocons) {
			fileat = i;		/* GET name, v3's short form */
			continue;
		}
		say("GET: unknown keyword or option: ");
		say(argv[i]);
		say("\r\n");
		return (1);
	}

	if (fileat == 0 && !tocons) {
		usage();
		return (1);
	}

	if (tocons) {			/* GET CONSOLE: stop reading */
		if (!active()) {
			say("GET: no file is being read\r\n");
			return (1);
		}
		subfn(GET_KILL);
		say("Getting console input from console\r\n");
		return (0);
	}

	if (active()) {
		say("GET: a file is already being read; GET CONSOLE first\r\n");
		return (1);
	}

	mkfcb(argv[fileat], &filefcb);
	for (i = 0; i < sizeof (struct fcb); i++)
		((char *) &tryfcb)[i] = ((char *) &filefcb)[i];
	if ((__bdos(BDOS_OPEN, (long) &tryfcb) & 0xff) == 0xff) {
		say("GET: no such file: ");
		say(argv[fileat]);
		say("\r\n");
		return (1);
	}
	__bdos(BDOS_CLOSE, (long) &tryfcb);

	if (loadmod(echo) != 0)
		return (1);

	say("Getting console input from file: ");
	sayfcb(&filefcb);
	say(echo ? " [ECHO]\r\n" : " [NO ECHO]\r\n");
	return (0);
}
