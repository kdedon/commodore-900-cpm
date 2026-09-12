/ ucrsx3.s -- UCASE3.RSX uses a distinct name and function 60/202 to test calls
/ passing through all three modules of a resident chain.

#define	RSXORG	0xE000
#define	RSXSUB	0xCA
#define	RSXNAM1	0x55, 0x43, 0x41, 0x53	/ UCAS
#define	RSXNAM2	0x45, 0x33, 0x20, 0x20	/ E3

#include "ucrsx.s"
