/* -*-c,save-*- */
#include <stdio.h>
main()
 register char *l1,*l2,*p;
 register int which, c;

	which = 0;
	p = l1 = &line1[0];
	l2 = &line2[0];

	while ((c=getchar()) != EOF) {
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
