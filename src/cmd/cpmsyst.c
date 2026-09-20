/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/*
 * cpmsyst.c - exercises cpmsys.c, the system-call layer under the app
 * programs, and is linked the way they are.
 *
 *   CPMSYST ARGS arg...	print each argument on a line of its own,
 *				as _cstart expanded them
 *   CPMSYST ODD n FILE		write n pattern bytes to FILE as a binary
 *				file, reopen it, and print where lseek to
 *				the end lands and how many bytes read gives
 */

#include <stdio.h>

extern	FILE	*fopenb();
extern	long	lseek();

main(argc, argv)
int argc;
char *argv[];
{
	register FILE	*fp;
	register int	i, n, fd, k;
	long		end, got;
	char		buf[100];

	if (argc >= 2 && strcmp(argv[1], "ARGS") == 0) {
		for (i = 2; i < argc; i++)
			printf("%s\n", argv[i]);
		exit(0);
	}
	if (argc == 4 && strcmp(argv[1], "ODD") == 0) {
		n = atoi(argv[2]);
		if ((fp = fopenb(argv[3], "w")) == NULL) {
			perror(argv[3]);
			exit(1);
		}
		for (i = 0; i < n; i++)
			putc((i * 7 + 3) & 0xff, fp);
		fclose(fp);
		if ((fd = openb(argv[3], 0)) < 0) {
			perror(argv[3]);
			exit(1);
		}
		end = lseek(fd, 0L, 2);
		lseek(fd, 0L, 0);
		for (got = 0L; (k = read(fd, buf, sizeof buf)) > 0; got += k)
			;
		close(fd);
		printf("ODD %s lseek %ld read %ld\n", argv[3], end, got);
		exit(0);
	}
	fprintf(stderr, "Usage: CPMSYST ARGS arg... | ODD n file\n");
	exit(1);
}
