/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/*
 * tek.h - Tektronix 4010 graphics from a byte stream, drawn on a surface.
 *
 * GS enters vector mode, FS point mode, US leaves for alpha; ESC FF
 * clears and homes.  An address is up to four bytes, told apart by
 * their top bits; only Lo-X is compulsory, and it ends the address:
 *
 *	Hi-Y  01 yyyyy	(y >> 5)	omitted when unchanged
 *	Lo-Y  11 yyyyy	(y & 31)	omitted when it and Hi-X are unchanged
 *	Hi-X  01 xxxxx	(x >> 5)	a high byte after Lo-Y
 *	Lo-X  10 xxxxx	(x & 31)
 *
 * The 1024 x 780 screen, y up, is scaled onto the surface.
 */

#define	TEK_ALPHA	0
#define	TEK_VEC		1	/* each address draws from the last	*/
#define	TEK_PT		2	/* each address plots a point		*/

struct tek {
	struct gfxsurf	*s;
	int		mode;
	int		dark;	/* next address moves without drawing	*/
	int		esc;	/* ESC seen in a graphics mode		*/
	int		loy;	/* Lo-Y seen in this address		*/
	int		hy, ly, hx, lx;	/* address bytes, 5 bits each	*/
	int		x, y;	/* beam, surface pixels			*/
};

extern VOID	tekinit();	/* (t, s)				*/
extern int	tekput();	/* (t, c) -> 1 when c is alpha text	*/
extern VOID	tekclear();	/* (t) clear, home, alpha		*/
extern VOID	tekat();	/* (t, x, y) move the beam, Tek units	*/
