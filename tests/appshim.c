/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/*
 * appshim.c -- the CP/M-8000 calls src/app/'s sources make that a POSIX
 * host does not have, so that those sources can be compiled FOR THE HOST
 * with -fsanitize=address and driven against malformed input.
 *
 * src/app/SDBIO.H and SDB.H both `#define CPM68K', so the branches these
 * fill in are the ones the shipped Z8001 binaries take: openb(), creatb()
 * and fopenb() are CP/M's "open this in binary mode" spellings, and on a
 * host where every file is binary they are open(), creat() and fopen().
 *
 * appabort() is here because src/app/FROMHEX.C calls abort() with an
 * argument -- 1982 C, and the argument is the exit status the tool means.
 * FROMHEX is compiled with -Dabort=appabort so that it exits with that
 * status rather than raising SIGABRT, which is what lets a test tell
 * FROMHEX's own refusal, rc 3, apart from an AddressSanitizer abort, rc 1.
 *
 * Nothing here is compiled into anything that runs on the machine.
 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>

int openb(name, mode)
char *name;
int mode;
{
	return (open(name, mode));
}

int creatb(name, mode)
char *name;
int mode;
{
	return (open(name, O_WRONLY | O_CREAT | O_TRUNC, 0666));
}

FILE *fopenb(name, mode)
char *name, *mode;
{
	return (fopen(name, mode));
}

/*  src/app/CMD.C:50 calls exit() with NO argument -- again 1982 C, where
    the status was whatever happened to be in the return register.  A host
    stdlib's exit() is prototyped, so SDB is compiled with -Dexit=appexit
    and gets this one, which exits zero as CP/M's did.  */

void appexit(n)
int n;
{
	exit(0);
}

/*  FROMHEX.C's abort(n): the status is the tool's own diagnosis.  */
void appabort(n)
int n;
{
	exit(n);
}
