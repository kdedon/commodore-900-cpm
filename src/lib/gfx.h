/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/*
 * gfx.h - drawing into a raster surface held in two segments.
 *
 * A surface's bytes run seg0[0 .. split-1], then seg1[0 ..].  Byte
 * offset y * stride + (x * bpp) / 8 names a pixel; the split may fall
 * anywhere, including inside a raster.
 *
 *	offset:	0                split               end
 *		| seg0 ........... | seg1 ............ |
 *
 * Pixels are big-endian within a byte: the most significant bit (1 bpp)
 * or nibble (4 bpp) is the leftmost.
 */

#ifndef	VOID
#define	VOID	int
#endif

struct gfxsurf {
	int		width;		/* pixels				*/
	int		height;		/* rasters				*/
	int		bpp;		/* 1 or 4				*/
	unsigned	stride;		/* bytes per raster			*/
	unsigned char	*seg0;		/* bytes 0 .. split-1			*/
	unsigned char	*seg1;		/* bytes split ..			*/
	long		split;		/* offset where seg0 ends		*/
};

/* writing modes */
#define	GFX_REPLACE	1	/* pixel = colour			*/
#define	GFX_XOR		2	/* pixel ^= colour			*/
#define	GFX_ERASE	3	/* pixel = 0				*/

#define	GFX_OK		0
#define	GFX_NOTSUP	(-1)	/* depth or mode this surface cannot do	*/

/* HR board geometry; the jumper picks one */
#define	HRSPLIT		65536L

extern VOID	gfx_hrmono();	/* (s, seg0, seg1) 1024x800x1		*/
extern VOID	gfx_hrclr();	/* (s, seg0, seg1) 640x400x4		*/
extern VOID	gfx_ram();	/* (s, w, h, bpp, stride, p0, p1, split) */

extern int	gfx_pixel();	/* (s, x, y, c, mode)			*/
extern int	gfx_get();	/* (s, x, y) -> colour, -1 outside	*/
extern int	gfx_span();	/* (s, x0, x1, y, c, mode) inclusive	*/
extern int	gfx_line();	/* (s, x0, y0, x1, y1, c, mode)		*/
extern int	gfx_fill();	/* (s, x, y, w, h, c, mode)		*/
extern int	gfx_box();	/* (s, x, y, w, h, c, mode) outline	*/
extern int	gfx_blit();	/* (s, src, sstride, sx, x, y, w, h, c, mode) */
extern int	gfx_pline();	/* (s, pts, n, c, bg, mode, pat) styled	*/

/* shared by the back-ends */
extern unsigned char *gfx_addr();	/* (s, off) -> byte at offset	*/
