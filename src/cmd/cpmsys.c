/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/*
 * cpmsys.c - the system-call layer under the toolchain's COHERENT stdio
 * and malloc, for the src/app programs (written for DRI's CP/M C).
 *
 * COHERENT's stdio wants open, creat, close, read, write, lseek, isatty,
 * sbrk and _exit; the programs also call DRI's binary variants openb,
 * creatb and fopenb, and abort(code).  This file supplies all of them on
 * the BDOS, and the startup (_cstart, entered from crt0.s) that builds
 * argc/argv from the command tail with DRI's `<file', `>file' and
 * `>>file' redirection and `*'/`?' filename expansion, and runs main.
 *
 * Files are byte streams over 128-byte records, read and written through
 * one record buffer per descriptor with random-record BDOS calls (33, 34),
 * so lseek is exact.  DRI's C keeps two kinds of file, and so does this:
 * open, creat and fopen are TEXT -- LF is written as CR LF, CR is dropped
 * on read, ^Z ends the data, and a new record is padded with ^Z -- while
 * openb, creatb and fopenb are BINARY and pass every byte.  A binary file
 * written here is closed with its CP/M 3 last record byte count (function
 * 30, f6'), and one opened reads that count back (function 15 with cr
 * 0FFh), so it ends at its true length; a count of 0 is a full record.
 * lseek counts raw bytes in both kinds.  Descriptors 0, 1 and 2 are the
 * console unless redirected.
 */

#include <stdio.h>
#include <errno.h>
#include "cpm.h"

#define	BDOS_FILESIZE	35	/* compute file size -> random record	*/

#define	NFD	12		/* descriptors, console ones included	*/
#define	CTLZ	0x1a
#define	STKGAP	1024		/* heap stops this far below the stack	*/

#define	F_FILE	1		/* kind: a disk file			*/
#define	F_CON	2		/* kind: the console			*/

struct fd {
	char	kind;		/* 0 free, F_FILE, F_CON		*/
	char	text;		/* text translation on			*/
	char	dirty;		/* rec[] differs from the disk		*/
	char	pad;		/* rec[] was past the end of the file	*/
	char	wrote;		/* written to since it was opened	*/
	char	lrbc;		/* last record byte count at the open	*/
	long	pos;		/* byte offset of the next read/write	*/
	long	recno;		/* record held in rec[], -1 for none	*/
	long	nrec;		/* length of the file in records	*/
	long	size;		/* length in bytes (binary files)	*/
	struct fcb fcb;
	char	rec[SECLEN];
};

static struct fd fds[NFD];

struct bpage	*_base;
int	errno;
extern	int	main();
extern	FILE	*_fopen();

/* ---- file names ---- */

/* Copy one part of a name into the n blank-padded bytes at p, up to
   STOP, a blank or the end; with WILD a `*' ending the part fills the
   rest of it with `?'.  What follows the part, or 0 if it is too long. */
static char *part(s, p, n, stop, wild)
register char *s, *p;
int n, stop, wild;
{
	register int	i;

	for (i = 0; i < n; i++)
		p[i] = ' ';
	for (i = 0; *s != 0 && *s != stop && *s != ' '; i++) {
		if (wild && *s == '*') {
			while (i < n)
				p[i++] = '?';
			s++;
			return (*s == 0 || *s == stop || *s == ' ' ? s : 0);
		}
		if (i == n)
			return (0);
		p[i] = *s++;
	}
	return (s);
}

/* Parse "NAME.EXT" or "D:NAME.EXT", in either case, into a fresh FCB.
   0 on success, -1 for a name CP/M cannot hold (bad drive, an empty or
   overlong part, or -- unless WILD -- a wildcard). */
static int fname(s, f, wild)
register char *s;
register struct fcb *f;
int wild;
{
	register char	*p;
	register int	i, c;

	p = (char *) f;
	for (i = 0; i < sizeof (struct fcb); i++)
		*p++ = 0;
	while (*s == ' ')
		s++;
	if (s[0] != 0 && s[1] == ':') {
		c = s[0] & ~0x20;
		if (c < 'A' || c > 'P')
			return (-1);
		f->drvcode = c - 'A' + 1;
		s += 2;
	}
	if ((s = part(s, f->fname, 8, '.', wild)) == 0 || f->fname[0] == ' ')
		return (-1);
	for (i = 0; i < 3; i++)
		f->ftype[i] = ' ';
	if (*s == '.' && part(s + 1, f->ftype, 3, 0, wild) == 0)
		return (-1);
	for (p = f->fname; p < f->fname + 11; p++) {
		if (!wild && (*p == '*' || *p == '?') || *p < ' ')
			return (-1);
		if (*p >= 'a' && *p <= 'z')
			*p -= 0x20;
	}
	return (0);
}

static struct fd *getfd(fd)
int fd;
{
	if (fd < 0 || fd >= NFD || fds[fd].kind == 0) {
		errno = EBADF;
		return ((struct fd *) 0);
	}
	return (&fds[fd]);
}

/* ---- records ---- */

static int ranio(f, fn)
register struct fd *f;
int fn;
{
	f->fcb.ran0 = f->recno >> 16;	/* big-endian here (bdosrw.c) */
	f->fcb.ran1 = f->recno >> 8;
	f->fcb.ran2 = f->recno;
	setdma(f->rec);
	return (__bdos(fn, (long) &f->fcb) & 0xff);
}

static int flush(f)
register struct fd *f;
{
	if (!f->dirty)
		return (0);
	f->dirty = f->pad = 0;
	if (ranio(f, BDOS_WRITERAN) != 0) {
		errno = ENOSPC;
		return (-1);
	}
	if (f->recno >= f->nrec)
		f->nrec = f->recno + 1;
	return (0);
}

/* Bring record n into the buffer.  1 if it exists (or has been written
   to), 0 past the end of the file (the buffer is then padding), -1 on
   error.

   A record past the end is never asked of the BDOS.  A random read of
   an extent that does not exist fails with 4 but leaves the FCB naming
   that extent with the previous extent's block map still in it, and the
   random write that follows then sees "same extent", creates no
   directory entry, and writes over the previous extent's blocks
   (src/bdos/bdosrw.c new_ext).  So nrec, the file's length in records,
   decides instead. */
static int load(f, n)
register struct fd *f;
long n;
{
	register int	i, r;

	if (f->recno == n)
		return (f->pad && !f->dirty ? 0 : 1);
	if (flush(f) < 0)
		return (-1);
	f->recno = n;
	f->pad = 0;
	r = n < f->nrec ? ranio(f, BDOS_READRAN) : 1;
	if (r == 0)
		return (1);
	for (i = 0; i < SECLEN; i++)
		f->rec[i] = f->text ? CTLZ : 0;
	f->pad = 1;
	if (r == 1)			/* past the end, or a hole */
		return (0);
	f->recno = -1L;
	errno = EIO;
	return (-1);
}

/* ---- the console ---- */

static char	conbuf[2 + 126];	/* fn 10 buffer: max, count, text */
static int	connext, conend;

static int conread(buf, n)
register char *buf;
register int n;
{
	register int	i;

	if (connext >= conend) {
		conbuf[0] = sizeof conbuf - 2;
		conbuf[1] = 0;
		__bdos(BDOS_RDCONBUF, (long) conbuf);
		conout('\r');
		conout('\n');
		conend = 2 + (conbuf[1] & 0xff);
		if (conend > 2 && conbuf[2] == CTLZ) {
			conend = 0;
			return (0);
		}
		conbuf[conend++] = '\n';
		connext = 2;
	}
	for (i = 0; i < n && connext < conend; i++)
		*buf++ = conbuf[connext++];
	return (i);
}

static VOID conwrite(buf, n)
register char *buf;
register int n;
{
	char		out[128];
	register int	k;
	struct ccb	blk;

	blk.cbaddr = (long) out;
	while (n > 0) {
		for (k = 0; n > 0 && k < sizeof out - 1; n--) {
			if (*buf == '\n')
				out[k++] = '\r';
			out[k++] = *buf++;
		}
		blk.cblen = k;
		__bdos(BDOS_PRTBLK, (long) &blk);
	}
}

/* ---- descriptors ---- */

static int newfd()
{
	register int	i;

	for (i = 0; i < NFD; i++)
		if (fds[i].kind == 0)
			return (i);
	errno = EMFILE;
	return (-1);
}

/* open or create NAME as a TEXT or binary file on descriptor fd */
static int fdopen1(fd, name, text, make)
int fd, text, make;
char *name;
{
	register struct fd *f;

	if (fd < 0)
		return (-1);
	f = &fds[fd];
	if (fname(name, &f->fcb, 0) < 0) {
		errno = ENOENT;
		return (-1);
	}
	if (make) {
		__bdos(BDOS_DELETE, (long) &f->fcb);
		f->fcb.extent = f->fcb.s1 = f->fcb.s2 = f->fcb.rcdcnt = 0;
		if ((__bdos(BDOS_MAKE, (long) &f->fcb) & 0xff) == 0xff) {
			errno = ENOSPC;
			return (-1);
		}
	} else {
		f->fcb.cur_rec = 0xff;		/* asks for the byte count */
		if ((__bdos(BDOS_OPEN, (long) &f->fcb) & 0xff) == 0xff) {
			errno = ENOENT;
			return (-1);
		}
	}
	f->nrec = 0L;
	f->lrbc = 0;
	if (!make) {
		f->lrbc = f->fcb.cur_rec & (SECLEN - 1);
		__bdos(BDOS_FILESIZE, (long) &f->fcb);
		f->nrec = (f->fcb.ran0 & 0xffL) << 16
			| (f->fcb.ran1 & 0xffL) << 8 | (f->fcb.ran2 & 0xffL);
	}
	f->size = f->nrec << 7;
	if (!text && f->lrbc != 0 && f->nrec != 0)
		f->size -= SECLEN - f->lrbc;
	f->fcb.cur_rec = 0;
	f->kind = F_FILE;
	f->text = text;
	f->dirty = f->pad = f->wrote = 0;
	f->pos = 0L;
	f->recno = -1L;
	return (fd);
}

int open(name, mode)
char *name;
int mode;
{
	return (fdopen1(newfd(), name, 1, 0));
}

int openb(name, mode)
char *name;
int mode;
{
	return (fdopen1(newfd(), name, 0, 0));
}

int creat(name, pmode)
char *name;
int pmode;
{
	return (fdopen1(newfd(), name, 1, 1));
}

int creatb(name, pmode)
char *name;
int pmode;
{
	return (fdopen1(newfd(), name, 0, 1));
}

int close(fd)
int fd;
{
	register struct fd *f;
	register int	r;

	if ((f = getfd(fd)) == 0)
		return (-1);
	r = 0;
	if (f->kind == F_FILE) {
		r = flush(f);
		if ((__bdos(BDOS_CLOSE, (long) &f->fcb) & 0xff) == 0xff) {
			errno = EIO;
			r = -1;
		}
		/* Record the last record byte count of a binary file that
		   was written, and clear a stale one from a text file.  The
		   name carries the file's attributes back (function 30 sets
		   them all) less the archive bit, which the close cleared. */
		if (r == 0 && f->wrote && (!f->text || f->lrbc != 0)) {
			f->fcb.cur_rec = f->text ? 0 : (int) f->size & (SECLEN - 1);
			f->fcb.ftype[2] &= 0x7f;
			f->fcb.fname[5] |= 0x80;
			if ((__bdos(BDOS_SETATTR, (long) &f->fcb) & 0xff) == 0xff) {
				errno = EIO;
				r = -1;
			}
		}
	}
	f->kind = 0;
	return (r);
}

int unlink(name)
char *name;
{
	struct fcb	fcb;

	if (fname(name, &fcb, 0) < 0
	 || (__bdos(BDOS_DELETE, (long) &fcb) & 0xff) == 0xff) {
		errno = ENOENT;
		return (-1);
	}
	return (0);
}

int read(fd, buf, n)
int fd;
register char *buf;
int n;
{
	register struct fd *f;
	register int	got, c, r;

	if ((f = getfd(fd)) == 0)
		return (-1);
	if (f->kind == F_CON)
		return (conread(buf, n));
	for (got = 0; got < n; ) {
		if ((r = load(f, f->pos >> 7)) < 0)
			return (-1);
		if (r == 0 || !f->text && f->pos >= f->size)
			break;
		c = f->rec[(int) f->pos & (SECLEN - 1)];
		if (f->text && c == CTLZ)
			break;
		f->pos++;
		if (!f->text || c != '\r')
			buf[got++] = c;
	}
	return (got);
}

static int put1(f, c)
register struct fd *f;
int c;
{
	if (load(f, f->pos >> 7) < 0)
		return (-1);
	f->rec[(int) f->pos & (SECLEN - 1)] = c;
	f->dirty = f->wrote = 1;
	if (++f->pos > f->size)
		f->size = f->pos;
	return (0);
}

int write(fd, buf, n)
int fd;
register char *buf;
int n;
{
	register struct fd *f;
	register int	i;

	if ((f = getfd(fd)) == 0)
		return (-1);
	if (f->kind == F_CON) {
		conwrite(buf, n);
		return (n);
	}
	for (i = 0; i < n; i++, buf++) {
		if (f->text && *buf == '\n' && put1(f, '\r') < 0)
			return (-1);
		if (put1(f, *buf) < 0)
			return (-1);
	}
	return (n);
}

long lseek(fd, off, whence)
int fd;
long off;
int whence;
{
	register struct fd *f;

	if ((f = getfd(fd)) == 0)
		return (-1L);
	if (f->kind != F_FILE) {
		errno = EINVAL;
		return (-1L);
	}
	if (whence == 1)
		off += f->pos;
	else if (whence == 2) {
		if (flush(f) < 0)
			return (-1L);
		off += f->text ? f->nrec << 7 : f->size;
	} else if (whence != 0) {
		errno = EINVAL;
		return (-1L);
	}
	if (off < 0L) {
		errno = EINVAL;
		return (-1L);
	}
	return (f->pos = off);
}

int isatty(fd)
int fd;
{
	return (fd >= 0 && fd < NFD && fds[fd].kind == F_CON);
}

/* DRI's binary fopen: the same modes, over openb/creatb. */
FILE *fopenb(name, type)
char *name, *type;
{
	register FILE	**fpp;
	register int	fd;

	if (*type == 'r')
		fd = openb(name, 0);
	else if (*type == 'w')
		fd = creatb(name, 0);
	else if (*type == 'a') {
		if ((fd = openb(name, 1)) < 0)
			fd = creatb(name, 0);
	} else
		return (NULL);
	if (fd < 0)
		return (NULL);
	for (fpp = &_fp[0]; fpp < &_fp[_NFILE]; fpp++)
		if (*fpp == NULL || !((*fpp)->_ff & _FINUSE))
			return (*fpp = _fopen(name, type, *fpp, fd));
	close(fd);
	return (NULL);
}

/* ---- memory ---- */

static char	*brkp;

/* The heap runs from the end of bss up to STKGAP below the stack;
   this is what is left of it. */
static long room()
{
	long		lim;

	if (brkp == 0)
		brkp = (char *) (_base->lbss + _base->bsslen);
	lim = _base->lbss + _base->bsslen + _base->freelen;
	if ((long) &lim - STKGAP < lim)
		lim = (long) &lim - STKGAP;
	return (lim - (long) brkp);
}

char *sbrk(incr)
unsigned incr;
{
	char		*old;

	if (incr > room())
		return ((char *) -1);
	old = brkp;
	brkp += incr;
	return (old);
}

/* malloc (src/malloc/malloc.c newarena) takes ulimit(3), when an sbrk
   fails, as the most it can ask for: so answer what is left. */
long ulimit(cmd, newlimit)
int cmd;
long newlimit;
{
	if (cmd != 3) {
		errno = EINVAL;
		return (-1L);
	}
	return (room());
}

/* ---- exit ---- */

/* Close every descriptor (writing out record buffers), publish the
   status as the program return code, warm boot.  exit() (stdio/exit.c)
   flushes stdio first.  The return code is written as cstart.c's _setrc()
   writes it (see there); cstart.o is not linked, this file being the
   startup. */
VOID _exit(status)
int status;
{
	register int	i;

	for (i = 0; i < NFD; i++)
		if (fds[i].kind != 0)
			close(i);
	if (status == -1)
		status = 1;
	__bdos(BDOS_RETCODE, (long) (unsigned) status);
	__bdos(BDOS_WBOOT, 0L);
	for (;;)
		;
}

/* DRI's abort(code): stop now, failing, without flushing stdio. */
VOID abort(code)
int code;
{
	_exit(code != 0 ? code : 1);
}

/* ---- startup ---- */

#define	NARGV	64

static char	tail[SECLEN + 1];
static char	*argv[NARGV + 1];
static char	names[NARGV * 15];	/* "D:NAME.EXT" of expanded arguments */
static int	nnames;

/* Add argument W to argv.  A `*' or `?' in it makes it a wildcard, which
   adds the files it matches instead, in directory order (search first
   and next, 17 and 18); one that matches nothing, or is no file name,
   is added as it is.  The new argc. */
static int addarg(w, argc)
char *w;
int argc;
{
	struct fcb	fcb;
	static char	dir[SECLEN];
	register char	*p, *e;
	register int	i, r, found;

	for (p = w; *p != 0 && *p != '*' && *p != '?'; p++)
		;
	found = 0;
	if (*p != 0 && fname(w, &fcb, 1) == 0) {
		setdma(dir);
		for (r = __bdos(BDOS_SFIRST, (long) &fcb) & 0xff; r != 0xff;
		     r = __bdos(BDOS_SNEXT, 0L) & 0xff) {
			found = 1;
			if (argc >= NARGV)
				continue;
			e = dir + (r & 3) * 32;
			p = argv[argc++] = names + nnames;
			if (fcb.drvcode != 0) {
				*p++ = 'A' - 1 + fcb.drvcode;
				*p++ = ':';
			}
			for (i = 1; i < 9 && (e[i] & 0x7f) != ' '; i++)
				*p++ = e[i] & 0x7f;
			if ((e[9] & 0x7f) != ' ')
				*p++ = '.';
			for (i = 9; i < 12 && (e[i] & 0x7f) != ' '; i++)
				*p++ = e[i] & 0x7f;
			*p++ = 0;
			nnames = p - names;
		}
	}
	if (!found && argc < NARGV)
		argv[argc++] = w;
	return (argc);
}

/* `>>file': NAME as a text file on descriptor fd, made if it is not
   there, positioned at its end -- on the ^Z ending the text if its last
   record holds one, so what is written goes on from the text. */
static int append(fd, name)
int fd;
char *name;
{
	register struct fd *f;
	register int	i;

	if (fdopen1(fd, name, 1, 0) < 0)
		return (fdopen1(fd, name, 1, 1));
	f = &fds[fd];
	f->pos = f->nrec << 7;
	if (f->nrec > 0 && load(f, f->nrec - 1) > 0)
		for (i = 0; i < SECLEN; i++)
			if (f->rec[i] == CTLZ) {
				f->pos -= SECLEN - i;
				break;
			}
	return (fd);
}

/* Split the command tail into argv, taking `<file', `>file' and `>>file'
   as redirection of descriptors 0 and 1.  main falling off its end (as
   these programs do) exits 0; exit() gives any other status. */
int _cstart(bp)
struct bpage *bp;
{
	register char	*p, *w;
	register int	argc, n, i;
	int		r;

	_base = bp;
	for (i = 0; i < 3; i++)
		fds[i].kind = F_CON;

	n = bp->buff[0] & 0x7f;
	for (i = 0; i < n; i++)
		tail[i] = bp->buff[i + 1];
	tail[n] = 0;

	argv[0] = "";
	argc = 1;
	for (p = tail; ; ) {
		while (*p == ' ' || *p == '\t')
			p++;
		if (*p == 0)
			break;
		w = p;
		while (*p != 0 && *p != ' ' && *p != '\t')
			p++;
		if (*p != 0)
			*p++ = 0;
		if (*w == '<' || *w == '>') {
			i = *w++ == '<' ? 0 : 1;
			fds[i].kind = 0;
			if (i == 1 && *w == '>')
				r = append(i, ++w);
			else
				r = fdopen1(i, w, 1, i);
			if (r < 0) {
				fds[i].kind = F_CON;
				perror(w);
				_exit(1);
			}
		} else
			argc = addarg(w, argc);
	}
	argv[argc] = 0;
	main(argc, argv);
	exit(0);
	return (0);
}
