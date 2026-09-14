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

	linecnt = 0;
	chp = &line[0];
	while ((c = getchar()) != EOF) {
		else {
			*chp = '\0';
			linecnt++;
			lines[linecnt-1] = calloc(strlen(line)+1,1);
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
