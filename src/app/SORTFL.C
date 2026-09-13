/* -*-c,save-*- */
#include <stdio.h>
#define MAXLINES 1000
#define LINESIZ 128
main()
{static char *lines[MAXLINES],
	     line[LINESIZ];
 register int linecnt;
 register char *chp;
 register int c;
 char *calloc();
 int strcmp1();
 register int i;

	/*  Both arrays were unbounded: a line longer than LINESIZ ran
	    `*chp++' off the end of line[], and the 1,001st line stored a
	    pointer past lines[].  A line that does not fit is TRUNCATED
	    and its tail discarded -- this is a sort filter, and silently
	    dropping the rest of the file would be worse -- while input
	    past MAXLINES lines is refused outright, because a sort that
	    silently omits lines is not a sort.  (c900)  */
	linecnt = 0;
	chp = &line[0];
	while ((c = getchar()) != EOF) {
		if (c != '\n') {
			if (chp < &line[LINESIZ-1])
				*chp++ = c;
			}
		else {
			*chp = '\0';
			if (linecnt >= MAXLINES) {
				fprintf(stderr,
				    "sortfl: more than %d lines\n",MAXLINES);
				exit(1);
				}
			linecnt++;
			lines[linecnt-1] = calloc(strlen(line)+1,1);
			if (lines[linecnt-1] == NULL) {
				fprintf(stderr,"sortfl: out of memory\n");
				exit(1);
				}
			strcpy(lines[linecnt-1],line);
			chp = &line[0];
			}
	}
	qsort(&lines[0],linecnt,sizeof(char *),strcmp1);
	for (i = 0; i<linecnt; i++)
		printf("%s\n",lines[i]);
}
int strcmp1(a,b)
register char **a,**b;
{register int cmp;

	cmp = strcmp(*a,*b);
	return(cmp);
}
