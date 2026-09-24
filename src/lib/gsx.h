/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/*
 * gsx.h - GSX graphics on a gfx surface.  Include gfx.h first.
 *
 * Both entries take GSX's parameter block; the arrays are 0-based here,
 * so contrl[0] is the opcode.  gsx() takes points in normalised
 * coordinates, 0..32767 with y up, and scales them as GDOS does.
 * gsxdrv() takes pixels, origin at the bottom left.
 */

struct gsxpb {
	int	*contrl;
	int	*intin;
	int	*ptsin;
	int	*intout;
	int	*ptsout;
};

#define	GSX_OK		0
#define	GSX_NA		(-1)	/* opcode, escape or GDP not available	*/

#define	GSXMAXV		256	/* ptsin vertices			*/

struct gsxws {
	struct gfxsurf	*s;
	int	ncol;		/* colours at once: 2 or 16		*/
	int	pix[16];	/* colour index -> pixel value		*/
	int	rgb[48];	/* R, G, B asked for, 0..1000		*/
	int	lt, lc;		/* polyline type, colour		*/
	int	mt, mc;		/* marker type, colour			*/
	int	tc, ts, up;	/* text colour, scale, up vector	*/
	int	fis, fsi, fc;	/* fill style, index, colour		*/
	int	wm;		/* writing mode 1..4			*/
	int	row, col, rev;	/* alpha cursor, from 1; reverse video	*/
	int	gc, gx, gy;	/* graphic cursor shown, where		*/
	int	xpix, ypix;	/* gsx()'s scale			*/
};

extern VOID	gsxinit();	/* (ws, s)				*/
extern int	gsx();		/* (ws, pb) normalised			*/
extern int	gsxdrv();	/* (ws, pb) device			*/
