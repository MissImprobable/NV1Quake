/*
nv1_draw.c -- 2D drawing and the NV1 texture manager.

Copyright (C) 1996-1997 Id Software, Inc.
Copyright (C) 2026 the nv1Quake port.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.


There is no scrap atlas here and no texture objects, because there is nothing
to pack into and nothing to bind.  On NV1 a texture is a lump of system RAM the
CPU walks while feeding the chip.

The 2D layer does not go through the patch renderer at all.  Console coordinates
and screen coordinates are the same in this port, so every 2D element is an
axis-aligned unscaled blit, which is what NVLIB_DrawImage does and it is far
cheaper than warping a patch to a rectangle.  The character set is split into
256 little 8x8 images at load, once, so drawing a character is one blit instead
of a sub-rectangle fetch the API cannot express.
*/

#include "quakedef.h"
#include "nv1quake.h"

/*
=============================================================================

  texture manager

=============================================================================
*/

nv1texture_t	nv1textures[MAX_NV1TEXTURES];
int		numnv1textures;
nv1texture_t	*nv1_currenttexture;

unsigned short	nv1_palette555[256];
byte		nv1_palettealpha[256];

int		currenttexture = -1;

/* the largest texture the patch renderer can stream in one primitive */
cvar_t		nv1_maxtexture = {"nv1_maxtexture", "256"};

byte		*draw_chars;		/* 8*8 graphic characters */
qpic_t		*draw_disc;
qpic_t		*draw_backtile;

int		translate_texture;
int		char_texture;

/*
 * A 2D picture.  Lives in the qpic_t's data area exactly as glpic_t did, so
 * id's caching code is untouched.
 */
typedef struct
{
	unsigned short	*data;		/* ARGB1555, top bit is opacity */
	int		width, height;
} nv1pic_t;

typedef struct
{
	char		name[MAX_QPATH];
	qpic_t		pic;
	byte		padding[32];	/* for appended glpic */
} cachepic_t;

#define	MAX_CACHED_PICS		128
cachepic_t	menu_cachepics[MAX_CACHED_PICS];
int		menu_numcachepics;

/* the character set, pre-split into 256 blittable tiles */
static unsigned short	*char_tiles[256];

byte		conback_buffer[sizeof(qpic_t) + sizeof(nv1pic_t)];
qpic_t		*conback = (qpic_t *)&conback_buffer;

/* the untranslated player pic, kept because the colour menu needs the indices
   long after the loaded file has gone */
static byte	menuplyr_pixels[4096];
static int	menuplyr_width, menuplyr_height;

/*
================
NV1_Bind
================
*/
void NV1_Bind (int texnum)
{
	if (texnum < 0 || texnum >= numnv1textures)
	{
		nv1_currenttexture = NULL;
		currenttexture = -1;
		return;
	}

	currenttexture = texnum;
	nv1_currenttexture = &nv1textures[texnum];
}

/*
================
NV1_FindTexture
================
*/
int NV1_FindTexture (char *identifier)
{
	int	i;

	for (i = 0 ; i < numnv1textures ; i++)
	{
		if (nv1textures[i].used && !strcmp (identifier, nv1textures[i].identifier))
			return i;
	}

	return -1;
}

/*
================
NV1_PowerOfTwo

Round down to a power of two, clamped to what a patch can stream.  Rounding
down rather than up matters: it is the difference between a 128x128 wall
costing 16K texels and 64K.
================
*/
static int NV1_PowerOfTwo (int v, int maxv)
{
	int	p;

	for (p = 1 ; p * 2 <= v ; p *= 2)
		;

	if (p > maxv)
		p = maxv;
	if (p < 1)
		p = 1;

	return p;
}

static int NV1_Log2 (int v)
{
	int	l = 0;

	while ((1 << l) < v)
		l++;

	return l;
}

/*
================
NV1_ResampleIndexed

Nearest-neighbour resample of 8-bit indexed data.  Point sampling is not a
shortcut, it is what the chip does to the grid anyway; filtering here would
only blur what is about to be point sampled again.
================
*/
static void NV1_ResampleIndexed (byte *in, int inwidth, int inheight,
	byte *out, int outwidth, int outheight)
{
	int		i, j;
	unsigned	frac, fracstep;
	byte		*inrow;

	fracstep = inwidth * 0x10000 / outwidth;

	for (i = 0 ; i < outheight ; i++, out += outwidth)
	{
		inrow = in + inwidth * (i * inheight / outheight);
		frac = fracstep >> 1;

		for (j = 0 ; j < outwidth ; j++)
		{
			out[j] = inrow[frac >> 16];
			frac += fracstep;
		}
	}
}

/*
================
NV1_BuildMipChain

Box filter in RGB555.  Mips exist here for cache behaviour rather than for
image quality: the resampler strides through the source texture, so a level
that roughly matches the on-screen size is the difference between staying in
cache and walking a 128K wall texture at random.
================
*/
static void NV1_BuildMipChain (nv1texture_t *t)
{
	int	level;
	int	keyed = t->alpha;

	for (level = 1 ; level < t->nummips ; level++)
	{
		unsigned short	*src = t->mip[level - 1];
		unsigned short	*dst = t->mip[level];
		int		sw = t->width >> (level - 1);
		int		sh = t->height >> (level - 1);
		int		dw = sw >> 1;
		int		dh = sh >> 1;
		int		x, y;

		if (dw < 1) dw = 1;
		if (dh < 1) dh = 1;

		for (y = 0 ; y < dh ; y++)
		{
			unsigned short	*r0 = src + (y * 2) * sw;
			unsigned short	*r1 = (sh > 1) ? r0 + sw : r0;

			for (x = 0 ; x < dw ; x++)
			{
				int	x0 = x * 2;
				int	x1 = (sw > 1) ? x0 + 1 : x0;
				int	r, g, b, n;
				unsigned short	q[4];
				int		k;

				q[0] = r0[x0]; q[1] = r0[x1];
				q[2] = r1[x0]; q[3] = r1[x1];

				r = g = b = n = 0;

				for (k = 0 ; k < 4 ; k++)
				{
				/*
				 * Averaging a cut-out texture across the alpha
				 * bit would bleed the transparent colour into
				 * the edges, so only opaque texels contribute
				 * and the result is transparent only if all
				 * four were.
				 */
					if (keyed && !(q[k] & 0x8000))
						continue;

					r += (q[k] >> 10) & 0x1f;
					g += (q[k] >> 5) & 0x1f;
					b += q[k] & 0x1f;
					n++;
				}

				if (!n)
				{
					dst[y * dw + x] = 0;
					continue;
				}

				dst[y * dw + x] = (unsigned short)
					((keyed ? 0x8000 : 0) |
					 (((r / n) << 10) | ((g / n) << 5) | (b / n)));
			}
		}
	}
}

/*
================
NV1_UploadIndexed

Convert 8-bit indexed Quake data into an RGB555 mip chain.  Non power of two
sources are scaled, because a patch's texel grid is a power of two by
definition and because masking u and v is how we get GL_REPEAT for free.
================
*/
static void NV1_UploadIndexed (nv1texture_t *t, int width, int height,
	byte *data, qboolean mipmap, qboolean alpha)
{
	int		w, h;
	int		maxsize;
	int		i, npix, total, level;
	byte		*scaled;
	unsigned short	*out;

	maxsize = (int)nv1_maxtexture.value;
	if (maxsize > 256) maxsize = 256;
	if (maxsize < 4) maxsize = 4;

	w = NV1_PowerOfTwo (width, maxsize);
	h = NV1_PowerOfTwo (height, maxsize);
	if (w < 1) w = 1;
	if (h < 1) h = 1;

	t->width = w;
	t->height = h;
	t->wpow = NV1_Log2 (w);
	t->hpow = NV1_Log2 (h);
	t->alpha = alpha;

	t->nummips = 1;
	if (mipmap)
	{
		while (t->nummips < NV1_MIPLEVELS &&
			(w >> t->nummips) >= 1 && (h >> t->nummips) >= 1)
			t->nummips++;
	}

/* one allocation for the whole chain */
	total = 0;
	for (level = 0 ; level < t->nummips ; level++)
	{
		int	lw = w >> level, lh = h >> level;

		if (lw < 1) lw = 1;
		if (lh < 1) lh = 1;
		total += lw * lh;
	}

	if (t->base)
		free (t->base);
	t->base = (unsigned short *)malloc (total * sizeof(unsigned short));
	if (!t->base)
		Sys_Error ("NV1_UploadIndexed: out of memory");

	out = t->base;
	for (level = 0 ; level < t->nummips ; level++)
	{
		int	lw = w >> level, lh = h >> level;

		if (lw < 1) lw = 1;
		if (lh < 1) lh = 1;
		t->mip[level] = out;
		out += lw * lh;
	}
	for (level = t->nummips ; level < NV1_MIPLEVELS ; level++)
		t->mip[level] = NULL;

/* level 0 */
	npix = w * h;

	if (w == width && h == height)
		scaled = NULL;
	else
	{
		scaled = (byte *)malloc (npix);
		if (!scaled)
			Sys_Error ("NV1_UploadIndexed: out of memory");
		NV1_ResampleIndexed (data, width, height, scaled, w, h);
		data = scaled;
	}

	if (alpha)
	{
	/*
	 * ARGB1555.  Quake marks cut-out texels with palette index 255, and
	 * bit 15 is the chip's alpha bit, so the two map straight onto each
	 * other: set for "draw this", clear for "skip it".  This is what
	 * sprites and the fence textures need, and without it a sprite draws
	 * its transparent border as a black box.
	 */
		for (i = 0 ; i < npix ; i++)
		{
			if (data[i] == 255)
				t->mip[0][i] = 0;
			else
				t->mip[0][i] = (unsigned short)
					(0x8000 | nv1_palette555[data[i]]);
		}
	}
	else
	{
		for (i = 0 ; i < npix ; i++)
			t->mip[0][i] = nv1_palette555[data[i]];
	}

	if (scaled)
		free (scaled);

	if (t->nummips > 1)
		NV1_BuildMipChain (t);
}

/*
================
NV1_LoadTexture
================
*/
int NV1_LoadTexture (char *identifier, int width, int height, byte *data,
	qboolean mipmap, qboolean alpha)
{
	int		i;
	nv1texture_t	*t;

	if (identifier[0])
	{
		for (i = 0 ; i < numnv1textures ; i++)
		{
			if (nv1textures[i].used &&
				!strcmp (identifier, nv1textures[i].identifier))
			{
				if (width != nv1textures[i].width ||
					height != nv1textures[i].height)
					goto reload;
				return i;
			}
		}
	}

	if (numnv1textures >= MAX_NV1TEXTURES)
		Sys_Error ("NV1_LoadTexture: MAX_NV1TEXTURES reached");

	i = numnv1textures++;

reload:
	t = &nv1textures[i];
	strncpy (t->identifier, identifier, sizeof(t->identifier) - 1);
	t->identifier[sizeof(t->identifier) - 1] = 0;
	t->used = true;

	NV1_UploadIndexed (t, width, height, data, mipmap, alpha);

	return i;
}

/*
================
NV1_UploadIntoTexture

Replace the contents of an existing texture, used for player skin translation.
================
*/
void NV1_UploadIntoTexture (int texnum, int width, int height, byte *data,
	qboolean mipmap, qboolean alpha)
{
	if (texnum < 0 || texnum >= MAX_NV1TEXTURES)
		return;

	if (texnum >= numnv1textures)
		numnv1textures = texnum + 1;

	nv1textures[texnum].used = true;
	NV1_UploadIndexed (&nv1textures[texnum], width, height, data, mipmap,
		alpha);
}

/*
=============================================================================

  2D pictures

=============================================================================
*/

/*
================
NV1_MakePic

Convert indexed pic data straight to ARGB1555.  Index 255 becomes a cleared
alpha bit, which is the transparency the chip understands.
================
*/
static void NV1_MakePic (nv1pic_t *p, int width, int height, byte *data)
{
	int		i, npix;
	unsigned short	*out;

	npix = width * height;

/* The pic header we are about to fill in overlaps the indexed pixels we are
   reading, so convert everything before touching p. */
/*
 * Deliberately malloc and not Hunk_AllocName.  On this card the converted
 * pixels ARE the texture -- there is no upload that hands them to the driver
 * and lets us drop them -- and Draw_Init loads the console background inside a
 * Hunk_LowMark/Hunk_FreeToLowMark pair.  Hunk memory would be handed straight
 * back and later reused by model and BSP data, which then gets drawn as
 * RGB555.  These live for the life of the process.
 */
	out = (unsigned short *)malloc (npix * sizeof(unsigned short));
	if (!out)
		Sys_Error ("NV1_MakePic: out of memory");

	for (i = 0 ; i < npix ; i++)
	{
		if (data[i] == 255)
			out[i] = 0;			/* alpha bit clear */
		else
			out[i] = (unsigned short)(0x8000 | nv1_palette555[data[i]]);
	}

	p->width = width;
	p->height = height;
	p->data = out;
}

/*
================
Draw_PicFromWad
================
*/
qpic_t *Draw_PicFromWad (char *name)
{
	qpic_t		*p;
	nv1pic_t	*np;

	p = W_GetLumpName (name);
	np = (nv1pic_t *)p->data;

	NV1_MakePic (np, p->width, p->height, (byte *)p->data);

	return p;
}

/*
================
Draw_CachePic
================
*/
qpic_t *Draw_CachePic (char *path)
{
	cachepic_t	*pic;
	int		i;
	qpic_t		*dat;
	nv1pic_t	*np;

	for (pic = menu_cachepics, i = 0 ; i < menu_numcachepics ; pic++, i++)
		if (!strcmp (path, pic->name))
			return &pic->pic;

	if (menu_numcachepics == MAX_CACHED_PICS)
		Sys_Error ("menu_numcachepics == MAX_CACHED_PICS");

	menu_numcachepics++;
	strcpy (pic->name, path);

/* load the pic from disk */
	dat = (qpic_t *)COM_LoadTempFile (path);
	if (!dat)
		Sys_Error ("Draw_CachePic: failed to load %s", path);
	SwapPic (dat);

	pic->pic.width = dat->width;
	pic->pic.height = dat->height;

	if (!strcmp (path, "gfx/menuplyr.lmp") &&
		dat->width * dat->height <= (int)sizeof(menuplyr_pixels))
	{
		memcpy (menuplyr_pixels, dat->data, dat->width * dat->height);
		menuplyr_width = dat->width;
		menuplyr_height = dat->height;
	}

	np = (nv1pic_t *)pic->pic.data;
	NV1_MakePic (np, dat->width, dat->height, dat->data);

	return &pic->pic;
}

/*
===============
Draw_CharToConback
===============
*/
void Draw_CharToConback (int num, byte *dest)
{
	int	row, col;
	byte	*source;
	int	drawline;
	int	x;

	row = num >> 4;
	col = num & 15;
	source = draw_chars + (row << 10) + (col << 3);

	drawline = 8;

	while (drawline--)
	{
		for (x = 0 ; x < 8 ; x++)
			if (source[x] != 255)
				dest[x] = 0x60 + source[x];
		source += 128;
		dest += 320;
	}
}

/*
===============
Draw_Init
===============
*/
void Draw_Init (void)
{
	int		i, x, y;
	qpic_t		*cb;
	byte		*dest, *src;
	int		start;
	byte		*ncdata;
	nv1pic_t	*np;
	char		ver[40];

	Cvar_RegisterVariable (&nv1_maxtexture);

/* load the console background and the charset by hand, because we need to
   write the version string into the background before it is converted */
	draw_chars = W_GetLumpName ("conchars");
	for (i = 0 ; i < 256 * 64 ; i++)
		if (draw_chars[i] == 0)
			draw_chars[i] = 255;	/* proper transparent colour */

/*
 * Split the character set into 256 standalone 8x8 tiles.  The blitter takes a
 * contiguous image and a destination rectangle; it has no concept of a source
 * sub-rectangle, so a shared 128x128 atlas would be useless to it.
 */
	for (i = 0 ; i < 256 ; i++)
	{
		unsigned short	*tile;
		int		row = i >> 4;
		int		col = i & 15;

		tile = (unsigned short *)malloc (8 * 8 * sizeof(unsigned short));
		if (!tile)
			Sys_Error ("Draw_Init: out of memory");
		src = draw_chars + (row << 10) + (col << 3);

		for (y = 0 ; y < 8 ; y++)
		{
			for (x = 0 ; x < 8 ; x++)
			{
				byte	c = src[y * 128 + x];

				if (c == 255)
					tile[y * 8 + x] = 0;
				else
					tile[y * 8 + x] = (unsigned short)
						(0x8000 | nv1_palette555[c]);
			}
		}

		char_tiles[i] = tile;
	}

	char_texture = 0;

	start = Hunk_LowMark ();

	cb = (qpic_t *)COM_LoadTempFile ("gfx/conback.lmp");
	if (!cb)
		Sys_Error ("Couldn't load gfx/conback.lmp");
	SwapPic (cb);

/* hack the version number directly into the pic */
	sprintf (ver, "(nv1Quake %2.2f) %4.2f", (float)NV1QUAKE_VERSION, (float)VERSION);
	dest = cb->data + 320 * 186 + 320 - 11 - 8 * strlen(ver);
	y = strlen (ver);
	for (x = 0 ; x < y ; x++)
		Draw_CharToConback (ver[x], dest + (x << 3));

	conback->width = cb->width;
	conback->height = cb->height;
	ncdata = cb->data;

	np = (nv1pic_t *)conback->data;
	NV1_MakePic (np, conback->width, conback->height, ncdata);

	conback->width = vid.width;
	conback->height = vid.height;

	Hunk_FreeToLowMark (start);

/* now the rest of the pics */
	draw_disc = Draw_PicFromWad ("disc");
	draw_backtile = Draw_PicFromWad ("backtile");
}

/*
================
NV1_BlitPic

The one primitive the whole 2D layer is built from.  transparent selects
between the plain image object and the alpha one; unscaled is the common case
and skips the resample entirely.
================
*/
static void NV1_BlitPic (int x, int y, int w, int h, nv1pic_t *p,
	qboolean transparent)
{
	NvuDim16	size;
	NvuRect16	dest;

	if (!p || !p->data)
		return;

	size.w = (unsigned short)p->width;
	size.h = (unsigned short)p->height;

	dest.x = (short)x;
	dest.y = (short)y;
	dest.w = (unsigned short)w;
	dest.h = (unsigned short)h;

	if (transparent)
		NVLIB_DrawImageA (p->data, size, dest);
	else
		NVLIB_DrawImage (p->data, size, dest);
}

/*
================
Draw_Character

Draws one 8*8 graphics character with 0 being transparent.
It can be clipped to the top of the screen to allow the console to be
smoothly scrolled off.
================
*/
void Draw_Character (int x, int y, int num)
{
	NvuDim16	size;
	NvuRect16	dest;

	if (num == 32)
		return;			/* space */

	num &= 255;

	if (y <= -8)
		return;			/* totally off screen */

	size.w = 8;
	size.h = 8;
	dest.x = (short)x;
	dest.y = (short)y;
	dest.w = 8;
	dest.h = 8;

	NVLIB_DrawImageA (char_tiles[num], size, dest);
}

/*
================
Draw_String
================
*/
void Draw_String (int x, int y, char *str)
{
	while (*str)
	{
		Draw_Character (x, y, *str);
		str++;
		x += 8;
	}
}

/*
================
Draw_DebugChar

Draws a single character directly to the screen.  With no direct framebuffer
access there is nothing "direct" about it here; it just draws.
================
*/
void Draw_DebugChar (char num)
{
	Draw_Character (8, 8, num);
}

/*
=============
Draw_AlphaPic
=============
*/
void Draw_AlphaPic (int x, int y, qpic_t *pic, float alpha)
{
	nv1pic_t	*np = (nv1pic_t *)pic->data;

/*
 * NV1 blends against the framebuffer with a beta value rather than a per-pixel
 * alpha.  Set the clamp, draw, put it back.
 */
	NVLIB_SetBetaClamp ((unsigned short)(alpha * 65535.0));
	NV1_BlitPic (x, y, np->width, np->height, np, true);
	NVLIB_SetBetaClamp (65535);
}

/*
=============
Draw_Pic
=============
*/
void Draw_Pic (int x, int y, qpic_t *pic)
{
	nv1pic_t	*np = (nv1pic_t *)pic->data;

	NV1_BlitPic (x, y, np->width, np->height, np, false);
}

/*
=============
Draw_TransPic
=============
*/
void Draw_TransPic (int x, int y, qpic_t *pic)
{
	nv1pic_t	*np = (nv1pic_t *)pic->data;

	if (x < 0 || (unsigned)(x + pic->width) > vid.width || y < 0 ||
		(unsigned)(y + pic->height) > vid.height)
	{
		Sys_Error ("Draw_TransPic: bad coordinates");
	}

	NV1_BlitPic (x, y, np->width, np->height, np, true);
}

/*
=============
Draw_TransPicTranslate

Only used for the player color selection menu
=============
*/
void Draw_TransPicTranslate (int x, int y, qpic_t *pic, byte *translation)
{
	static unsigned short	trans[64 * 64];
	byte			*src;
	int			u, v;
	int			p;
	NvuDim16		size;
	NvuRect16		dest;

	if (!menuplyr_width || !menuplyr_height)
		return;

	for (v = 0 ; v < 64 ; v++)
	{
		src = &menuplyr_pixels[((v * menuplyr_height) >> 6) * menuplyr_width];

		for (u = 0 ; u < 64 ; u++)
		{
			p = src[(u * menuplyr_width) >> 6];

			if (p == 255)
				trans[v * 64 + u] = 0;
			else
				trans[v * 64 + u] = (unsigned short)
					(0x8000 | nv1_palette555[translation[p]]);
		}
	}

	size.w = 64;
	size.h = 64;
	dest.x = (short)x;
	dest.y = (short)y;
	dest.w = (unsigned short)pic->width;
	dest.h = (unsigned short)pic->height;

	NVLIB_DrawImageA (trans, size, dest);
}

/*
================
Draw_ConsoleBackground
================
*/
void Draw_ConsoleBackground (int lines)
{
	int		y;
	nv1pic_t	*np = (nv1pic_t *)conback->data;
	NvuDim16	size;
	NvuRect16	dest;

	y = (vid.height * 3) >> 2;

	size.w = (unsigned short)np->width;
	size.h = (unsigned short)np->height;

	dest.x = 0;
	dest.y = (short)(lines - vid.height);
	dest.w = (unsigned short)vid.width;
	dest.h = (unsigned short)vid.height;

	if (lines > y)
		NVLIB_DrawImage (np->data, size, dest);
	else
	{
		NVLIB_SetBetaClamp ((unsigned short)((float)(1.2 * lines) / y * 65535.0));
		NVLIB_DrawImageA (np->data, size, dest);
		NVLIB_SetBetaClamp (65535);
	}
}

/*
=============
Draw_TileClear

This repeats a 64*64 tile graphic to fill the screen around a sized down
refresh window.
=============
*/
void Draw_TileClear (int x, int y, int w, int h)
{
	nv1pic_t	*np = (nv1pic_t *)draw_backtile->data;
	int		tx, ty;
	int		tw = np->width;
	int		th = np->height;
	NvuRect16	clip;

	if (tw < 1 || th < 1)
		return;

/*
 * There is no texture wrap on a blit, so the tile is stepped by hand and the
 * clip rectangle keeps the edges honest.  Cheaper than it looks: the tile is
 * 64x64 and stays in cache.
 */
	clip.x = (short)x;
	clip.y = (short)y;
	clip.w = (unsigned short)w;
	clip.h = (unsigned short)h;
	NVLIB_SetClip (clip);

	for (ty = y - (y % th) ; ty < y + h ; ty += th)
		for (tx = x - (x % tw) ; tx < x + w ; tx += tw)
			NV1_BlitPic (tx, ty, tw, th, np, false);

	NVLIB_SetClip (NVLIB_CLIP_OFF);
}

/*
=============
Draw_Fill

Fills a box of pixels with a single color
=============
*/
void Draw_Fill (int x, int y, int w, int h, int c)
{
	NvuRect16	rect;

	rect.x = (short)x;
	rect.y = (short)y;
	rect.w = (unsigned short)w;
	rect.h = (unsigned short)h;

	NVLIB_DrawRectangle (nv1_palette555[c & 255], &rect);
}

/*
================
Draw_FadeScreen
================
*/
void Draw_FadeScreen (void)
{
	NvuRect16	rect;

	rect.x = 0;
	rect.y = 0;
	rect.w = (unsigned short)vid.width;
	rect.h = (unsigned short)vid.height;

	NVLIB_SetBetaClamp ((unsigned short)(0.8 * 65535.0));
	NVLIB_DrawRectangleA (0, &rect);
	NVLIB_SetBetaClamp (65535);

	Sbar_Changed ();
}

/*
================
Draw_BeginDisc

Draws the little blue disc in the corner of the screen.
Call before beginning any disc IO.
================
*/
void Draw_BeginDisc (void)
{
/*
 * GLQuake drew this straight to the front buffer.  The NV1 pipe has no way to
 * reach the visible buffer without flipping, so the disc is simply skipped
 * rather than corrupting the frame in progress.
 */
}

/*
================
Draw_EndDisc
================
*/
void Draw_EndDisc (void)
{
}

/*
================
NV1_Set2D

Setup for single-pixel-accurate 2D drawing.  With no transform hardware there
is no projection to change; all this has to do is drop the clip rectangle back
to the full screen after the 3D view has been drawn.
================
*/
void NV1_Set2D (void)
{
	NVLIB_SetClip (NVLIB_CLIP_OFF);
}
