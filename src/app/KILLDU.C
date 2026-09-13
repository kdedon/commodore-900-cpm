/* -*-c,save-*- */
#include <stdio.h>
#define LINESIZ 128
main()
{static char line1[LINESIZ],line2[LINESIZ];
 register char *l1,*l2,*p;
 register int which, c;

	which = 0;
	p = l1 = &line1[0];
	l2 = &line2[0];

	/*  `*p++ = c' was unbounded, so a line longer than LINESIZ ran off
	    the end of whichever of the two arrays was current -- l1 and l2
	    are swapped on every line, so the overrun alternates between
	    them.  p always starts at l1, so l1 is the buffer being filled
	    and l1+LINESIZ-1 is the last byte of it, kept free for the
	    terminator stored below.  A line that does not fit is
	    TRUNCATED, which is what a duplicate filter can do without
	    losing lines.  (c900)  */
	while ((c=getchar()) != EOF) {
		if (c != '\n') {
			if (p < l1 + LINESIZ - 1) *p++ = c;
			}
		else {
			*p++ = '\0';
			switch (which) {
			case 0: {
				printf("%s\n",l1);
				p = l2;
				l2 = l1;
				l1 = p;
				which = 1;
				break;
				}
			case 1: {
				if (strcmp(l1,l2) != 0) {
					printf("%s\n",l1);
					p = l2;
					l2 = l1;
					l1 = p;
					}
				else p = l1;
				break;
				}
			}
		}
	}

}
