/*
 * put.c - PUT.Z8K: console output into a file, on the RSX mechanism.
 *
 *	PUT [CONSOLE] [OUTPUT] [TO] FILE name [[ECHO]|[NO ECHO]]
 *	PUT [CONSOLE] [OUTPUT] [TO] CONSOLE
 *
 * The mirror of src/cmd/get.c, and everything that file's header says
 * about the module, the attach and v3's sub-function numbers applies
 * here with PUT's numbers: 133 stops and closes, 134 asks whether the
 * module is there (ref/cpm3/putrsx.asm, get.plm/put.plm).
 *
 * WHAT IS AND IS NOT IMPLEMENTED.  [ECHO] and [NO ECHO] are: with
 * echoing on -- v3's default -- the console sees the output as well as
 * the file, and with it off only the file does.  The rest are refused
 * BY NAME:
 *
 *   [SYSTEM] is accepted: it describes what this PUT does, since the
 *	CCP is a transient and its output goes through the chain too.
 *   [PROGRAM] is refused, for get.c's reason -- there is no @CCPFLG
 *	bit here that says the CCP is the one calling.
 *   [FILTERED] and [RAW] are refused: the bytes are copied as they are.
 *   PRINTER / LIST / LST: are refused.  Capturing the list device means
 *	intercepting function 5, which this module does not claim.
 *
 * The file must not already exist.  v3 asks `File already exists;
 * Delete it?' (put.plm:732); a scripted session cannot answer that, so
 * this refuses and leaves the file alone.
 *
 * PUT CONSOLE OUTPUT TO CONSOLE is not optional bookkeeping: it is what
 * writes the last, partial record and closes the file.  Output made
 * after the last full record and never followed by it is lost.
 */

#include "cpm.h"

#define	RSXMAGIC	0x5253
#define	RSXHDRLEN	32
#define	RSX_ATTACH	127
#define	BDOS_CALLRSX	60

/* the module's sub-functions -- ref/cpm3/getrsx.asm:92-104 */
#define	PUT_KILL	133
#define	PUT_FCB		134

/* prefix offsets (src/bdos/rsxhdr.h) and the two fields src/cmd/putrsx.s
   leaves for us immediately after the prefix */
#define	H_MAGIC		8
#define	H_ORG		0x1c
#define	H_LEN		0x1e
#define	H_ECHO		0x20
#define	H_FCB		0x22

#define	MODNAME		"PUT.RSX"

struct rsxpb {
	char		rpfunc;
	char		rprsvd;
	unsigned	rporg;
	unsigned	rplen;
	long		rpsrc;
};

char		buf[2048];		/* the module image, as read	*/
struct fcb	filefcb;		/* the file the module writes	*/
struct fcb	tryfcb;
struct rsxpb	pb;

/* the module's words are Z8001 words: high byte first */
static unsigned wordat(off)
int off;
{
	return (((buf[off] & 0xff) << 8) | (buf[off + 1] & 0xff));
}

static unsigned subfn(n)
int n;
{
	pb.rpfunc = (char) n;
	pb.rprsvd = 0;
	return ((unsigned) __bdos(BDOS_CALLRSX, (long) &pb));
}

static int active()
{
	register unsigned r;

	r = subfn(PUT_FCB);
	return (r != 0 && r != 0xff);
}

/* see src/cmd/get.c kwis(): the CCP splits at blanks only, so the
   brackets of `[NO ECHO]' arrive stuck to the words */
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
	say("usage: PUT [CONSOLE] [OUTPUT] [TO] FILE name [[ECHO]|[NO ECHO]]\r\n");
	say("       PUT [CONSOLE] [OUTPUT] [TO] CONSOLE\r\n");
}

int echo;
{
	register int	n, i;
	register unsigned org, len;
	register char	*p;

	mkfcb(MODNAME, &tryfcb);
		say("PUT: cannot find ");
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
		say("PUT: ");
		say(MODNAME);
		say(" is not a resident system extension\r\n");
		return (1);
	}
	org = wordat(H_ORG);
	len = wordat(H_LEN);
	if (len > (unsigned) n) {
		say("PUT: ");
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
	pb.rpsrc = (long) buf;
	if (__bdos(BDOS_CALLRSX, (long) &pb) != 0) {
		say("PUT: there is no room for the module below the resident\r\n");
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
		if (fileat == i)
			continue;		/* the file name itself */
		if (kwis(argv[i], "CONSOLE") || kwis(argv[i], "CON:")
		    || kwis(argv[i], "CONOUT:")) {
			if (fileat != 0)
				continue;
			tocons = 1;
			continue;
		}
		if (kwis(argv[i], "OUTPUT") || kwis(argv[i], "TO"))
			continue;
		if (kwis(argv[i], "FILE")) {
			if (i + 1 >= argc) {
				say("PUT: FILE needs a file name\r\n");
				return (1);
			}
			fileat = i + 1;
			tocons = 0;
			continue;
		}
		if (kwis(argv[i], "ECHO"))
			continue;
		if (kwis(argv[i], "NO") || kwis(argv[i], "NOT")) {
			echo = 0;
			continue;
		}
		if (kwis(argv[i], "SYSTEM"))
			continue;
		if (kwis(argv[i], "PROGRAM")) {
			say("PUT: [PROGRAM] is not implemented.  The CCP is a\r\n");
			say("     transient here and writes through the same RSX\r\n");
			say("     chain as a program, so this PUT is always\r\n");
			say("     [SYSTEM] and cannot capture one and not the other\r\n");
			return (1);
		}
		if (kwis(argv[i], "FILTERED") || kwis(argv[i], "RAW")) {
			say("PUT: [FILTERED] and [RAW] are not implemented; the\r\n");
			say("     output is copied byte for byte as it stands\r\n");
			return (1);
		}
		if (kwis(argv[i], "PRINTER") || kwis(argv[i], "LIST")
		    || kwis(argv[i], "LST:")) {
			say("PUT: the printer is not implemented.  Capturing it\r\n");
			say("     means intercepting BDOS function 5, which\r\n");
			say("     PUT.RSX does not claim\r\n");
			return (1);
		}
		if (kwis(argv[i], "AUXILIARY") || kwis(argv[i], "AUXOUT:")
		    || kwis(argv[i], "AUX:")) {
			say("PUT: this system has no AUX: device to write\r\n");
			return (1);
		}
		if (fileat == 0 && !tocons) {
			fileat = i;		/* PUT name, the short form */
			continue;
		}
		say("PUT: unknown keyword or option: ");
		say(argv[i]);
		say("\r\n");
		return (1);
	}

	if (fileat == 0 && !tocons) {
		usage();
		return (1);
	}

	if (tocons) {			/* PUT CONSOLE: close and stop */
		if (!active()) {
			say("PUT: nothing is being written\r\n");
			return (1);
		}
		/*  The module hands back what BDOS function 16 gave it, and
		    a successful close is a DIRECTORY CODE 0..3, not a zero
		    (ref/cpm3/bdos30.asm; src/cmd/cpm.h BDOS_CLOSE).  Only
		    0FFh is a failure.  */
		if (subfn(PUT_KILL) == 0xff) {
			say("PUT: the file did not close cleanly\r\n");
			return (1);
		}
		say("PUT completed for console\r\n");
		return (0);
	}

	if (active()) {
		say("PUT: a file is already being written; PUT CONSOLE first\r\n");
		return (1);
	}

	mkfcb(argv[fileat], &filefcb);
	for (i = 0; i < sizeof (struct fcb); i++)
		((char *) &tryfcb)[i] = ((char *) &filefcb)[i];
	if (__bdos(BDOS_OPEN, (long) &tryfcb) != 255) {
		__bdos(BDOS_CLOSE, (long) &tryfcb);
		say("PUT: ");
		say(argv[fileat]);
		say(" already exists; erase it first\r\n");
		return (1);
	}
	for (i = 0; i < sizeof (struct fcb); i++)
		((char *) &tryfcb)[i] = ((char *) &filefcb)[i];
		say("PUT: no directory space for ");
		say(argv[fileat]);
		say("\r\n");
		return (1);
	}
	__bdos(BDOS_CLOSE, (long) &tryfcb);

		return (1);

	say("Putting console output to file: ");
	sayfcb(&filefcb);
	say(echo ? " [ECHO]\r\n" : " [NO ECHO]\r\n");
	return (0);
}
