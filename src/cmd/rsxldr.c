/*
 * rsxldr.c - RSXLDR.Z8K: load a Resident System Extension.
 *
 *	RSXLDR name.RSX [T] [name.RSX [T]] ...
 *
 * In CP/M 3 nothing like this program exists, because the loader is
 * inside the system: GENCOM staples an RSX onto a .COM file
 * (gencom.plm:307-330) and the LOADER RSX pulls it out, relocates it
 * below the chain and links it while loading the program
 * (loader3.asm:216-267).  Two of those three steps need a PRL
 * relocator and a .COM header, neither of which exists here
 * (sys/rsxhdr.h note 3), so the parts that remain -- read the module,
 * ask the system to link it -- are done from a transient instead.  The
 * system half is BDOS function 60 sub-function 127 (sys/rsx.c).
 *
 * `T' sets the module's warm-boot flag (prefix offset 0Eh) before the
 * attach, which is v3's temporary RSX: removed at the next warm boot
 * (loader3.asm:345-380).  Since a transient's exit IS a warm boot, a
 * module loaded that way is gone by the time the next command runs --
 * which is exactly what a GENCOM'd program's RSX does when the program
 * it came with finishes.
 *
 * More than one module may be named in one command, and that is not a
 * convenience: it is the only way a TEMPORARY module can still be
 * resident when a second module attaches, because the warm boot that
 * removes it is this program's own exit.  v3 gets the same situation
 * from one .COM file carrying several RSX descriptors, which its loader
 * attaches in a single pass (loader3.asm:210-233 `rsxf1', looping over
 * the descriptors until the offset field is zero).  The modules attach
 * left to right, so the leftmost ends up highest in memory -- v3's
 * order too, since each new module is placed below the chain
 * (calcdest, loader3.asm:615-632).
 */

#include "cpm.h"

#define	RSXMAGIC	0x5253
#define	RSXHDRLEN	32
#define	RSX_ATTACH	127
#define	RSX_QUERY	126	/* the system's chain query (sys/rsx.c)	*/
#define	BDOS_CALLRSX	60

/* prefix offsets, sys/rsxhdr.h */
#define	H_MAGIC		8
#define	H_WARMFLG	0x0e
#define	H_NEXT		0x0a
#define	H_PREV		0x0c
#define	H_ENDCHAIN	0x18
#define	H_ORG		0x1c
#define	H_LEN		0x1e

struct rsxpb {
	char		rpfunc;
	char		rprsvd;
	unsigned	rporg;
	unsigned	rplen;
	long		rpsrc;
};

char	buf[4096];		/* the module image, as read		*/
struct fcb	fcb;
struct rsxpb	pb;

/* the module's words are Z8001 words: high byte first */
unsigned wordat(off)
int off;
{
	return (((buf[off] & 0xff) << 8) | (buf[off + 1] & 0xff));
}

VOID puthex(n)
unsigned n;
{
	register int	i, d;

	for (i = 12; i >= 0; i -= 4) {
		d = (n >> i) & 0xf;
		conout(d < 10 ? '0' + d : 'A' + d - 10);
	}
}

/*  Read one module and ask the system to link it.  Returns 0 on success;
    a refusal is reported here because the code the system gives back
    (sys/rsxhdr.h RSX_E*) names which of v3's placement rules the module
    broke, and that is the whole diagnostic.  */

int attach(name, temp)
char *name;
int temp;
{
	register int	n;
	register unsigned org, len;

	mkfcb(name, &fcb);
	if (__bdos(BDOS_OPEN, (long) &fcb) == 255) {
		return (1);
	}
	for (n = 0; n < sizeof buf; n += SECLEN) {
		setdma(&buf[n]);
		if (__bdos(BDOS_READSEQ, (long) &fcb) != 0)
			break;
	}
	__bdos(BDOS_CLOSE, (long) &fcb);
	setdma(_base->buff);
	if (n < RSXHDRLEN) {
		return (1);
	}
	if (wordat(H_MAGIC) != RSXMAGIC) {
		return (1);
	}
	org = wordat(H_ORG);
	len = wordat(H_LEN);
	if (len > (unsigned) n) {
		return (1);
	}
	if (temp)
		buf[H_WARMFLG] = 0xff;

	pb.rpfunc = RSX_ATTACH;
	pb.rprsvd = 0;
	pb.rporg = org;
	pb.rplen = len;
	pb.rpsrc = (long) buf;
	if ((n = __bdos(BDOS_CALLRSX, (long) &pb)) != 0) {
		puthex((unsigned) n);
		return (1);
	}

	/*  This message is printed AFTER the attach, so it goes through
	    the module that was just linked in -- the first proof that
	    the chain is live.  */
	puthex(org);
	return (0);
}

/*  The chain as it stands, printed from the program that built it and
    BEFORE the warm boot that ends the program.  That moment is the only
    one in which the links attach() wrote can be seen at all: the warm
    boot walks the chain and rebuilds every link in it (rsx.c
    rsxwboot(), v3's `rsx$chain', loader3.asm:326-380), so by the time
    the next command runs, a `prev' the attach never wrote has been
    written for it and a bad attach looks like a good one.  */

VOID chain()
{
	register char	*p;
	register unsigned org;

	pb.rpfunc = RSX_QUERY;
	pb.rprsvd = 0;
	org = (unsigned) __bdos(BDOS_CALLRSX, (long) &pb);
	while (org != 0) {
		p = (char *) (((long) &pb & 0xffff0000L) | (long) org);
		puthex(org);
		puthex((unsigned)(((p[H_PREV] & 0xff) << 8)
				  | (p[H_PREV + 1] & 0xff)));
		org = (unsigned)(((p[H_NEXT] & 0xff) << 8)
				 | (p[H_NEXT + 1] & 0xff));
		puthex(org);
		puthex((unsigned) p[H_ENDCHAIN] & 0xff);
	}
}

int main(argc, argv)
int argc;
char *argv[];
{
	register int	i;
	int		temp;

	if (argc < 2) {
		return (1);
	}
	for (i = 1; i < argc; i++) {
		if (argv[i][0] == 'T' && argv[i][1] == 0)
			continue;	/* the flag of the module before it */
		temp = (i + 1 < argc && argv[i + 1][0] == 'T'
			&& argv[i + 1][1] == 0);
		if (attach(argv[i], temp) != 0)
			return (1);
	}
	chain();
	return (0);
}
