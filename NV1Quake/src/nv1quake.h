/*
nv1quake.h -- the NV1 renderer interface for nv1Quake.

Why this is not called glquake.h:

quakedef.h reaches the renderer interface with #include "glquake.h", and MSVC
resolves a quoted include relative to the directory of the file doing the
including -- WinQuake -- before it consults any -I path.  So id's glquake.h
cannot be shadowed, and nv1Quake does not try to.  id's header stays exactly
where it is and keeps doing its real job, which is declaring the things every
shared file needs: particle_t, r_refdef, the r_* and gl_* cvars, the view
vectors.  Its handful of GL types are satisfied by the shim in src/GL, and the
OpenGL entry points it declares are simply never defined, because nothing calls
them.

Everything that is actually about the NV1 lives here, and every file in this
port includes it after quakedef.h.

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
*/

#ifndef NV1QUAKE_H
#define NV1QUAKE_H

#include <nv32.h>
#include <nvutypes.h>
#include <nvlib.h>
#include <nvvidmod.h>

#ifdef NV1_SOFT
#include "nvsoft.h"
#endif

#define NV1QUAKE_VERSION	0.10

/*
=============================================================================

  texture manager

  There is no texture memory and no texture objects on this card.  A texture is
  a lump of system RAM in RGB555 that the CPU walks while feeding the chip, so
  "binding" is setting a pointer and "uploading" is a palette conversion.

  Mip levels matter more here than on a card that samples for you: the
  resampler's inner loop strides through the source texture, so a level that
  roughly matches the on-screen size is the difference between staying in cache
  and walking a 128K wall texture at random.

=============================================================================
*/

#define MAX_NV1TEXTURES		2048
#define NV1_MIPLEVELS		8

typedef struct
{
	char		identifier[64];
	int		width, height;		/* of mip 0, always powers of two */
	int		wpow, hpow;		/* log2 of the above */
	int		nummips;
	qboolean	used;
	qboolean	alpha;			/* had transparent texels */
	unsigned short	*mip[NV1_MIPLEVELS];	/* RGB555, mip[0] is full size */
	unsigned short	*base;			/* single allocation for the chain */
} nv1texture_t;

extern nv1texture_t	nv1textures[MAX_NV1TEXTURES];
extern int		numnv1textures;
extern nv1texture_t	*nv1_currenttexture;

int  NV1_LoadTexture (char *identifier, int width, int height, byte *data,
	qboolean mipmap, qboolean alpha);
int  NV1_FindTexture (char *identifier);
void NV1_Bind (int texnum);
void NV1_UploadIntoTexture (int texnum, int width, int height, byte *data,
	qboolean mipmap, qboolean alpha);

/*
 * id's glquake.h prototypes these under their OpenGL names and the model
 * loader calls them that way.  The prototypes are harmless; these send the
 * calls to the NV1 implementations.
 */
#define GL_LoadTexture		NV1_LoadTexture
#define GL_FindTexture		NV1_FindTexture
#define GL_Bind			NV1_Bind
#define GL_BeginRendering	NV1_BeginRendering
#define GL_EndRendering		NV1_EndRendering
#define GL_Set2D		NV1_Set2D

/* 8-bit Quake palette to RGB555 */
extern unsigned short	nv1_palette555[256];
extern byte		nv1_palettealpha[256];

/*
=============================================================================

  frame

=============================================================================
*/

void NV1_BeginRendering (int *x, int *y, int *width, int *height);
void NV1_EndRendering (void);
void NV1_Set2D (void);
void NV1_DeviceInit (void);

/* ARGB1555 cut-out variants, implemented by nvsoft and by nvlib on hardware */
BOOL NVLIB_InitQTexQuadA (NVLIB_ColorFormat color_format);
void NVLIB_DrawQTexQuadA (U032 *texture, U032 width, U032 height,
	Nvu1Pt16 *points);
void NVLIB_DestroyQTexQuadA (void);

/*
=============================================================================

  transform and submission

  View space is x right, y up, z forward.  There is no transform hardware, no
  clipper and no depth buffer, so all three live in nv1_rmain.c.

=============================================================================
*/

typedef struct
{
	vec3_t		xyz;		/* VIEW space */
	float		s, t;		/* texture coords, in texels of mip 0 */
	int		beta;		/* 0 .. 32767, S1.15 light */
} nv1vert_t;

/* beta is S1.15 */
#define NV1_BETAONE		32767

/* nothing closer than this is drawn; everything is clipped against it first */
#define NV1_NEARCLIP		4.0

/*
 * The most vertices a polygon can carry through the clipper.
 *
 * Quake world surfaces routinely run past 32 edges, and clipping against five
 * planes can add one vertex each, so a 32 entry buffer silently truncated big
 * polygons -- which shows up as chunks missing from floors.
 */
#define MAX_SURFVERTS		64
#define MAX_CLIPVERTS		(MAX_SURFVERTS + 8)

extern float	nv1_xscale, nv1_yscale;
extern float	nv1_xcenter, nv1_ycenter;

void NV1_SetupTransform (void);
void NV1_PushEntityTransform (entity_t *e);
void NV1_PopEntityTransform (void);
void NV1_TransformPoint (vec3_t in, vec3_t viewout);

/*
 * Submit one convex polygon: clip in view space, project, fan into triangles,
 * and stream each as a quadratic patch.
 */
void NV1_DrawPolygon (nv1vert_t *verts, int numverts, nv1texture_t *tex,
	qboolean lit);
void NV1_DrawSolidPolygon (nv1vert_t *verts, int numverts, int color);
int  NV1_ClipPolygon (nv1vert_t *in, int numin, nv1vert_t *out);

/*
 * Entities are filed per leaf and drawn from inside the world walk, so they
 * land in the right place in the back-to-front order instead of on top of
 * everything.
 */
void NV1_LinkEntitiesToLeaves (void);
void NV1_DrawLeafEntities (int leafnum);
void NV1_DrawEarlyEntities (void);

void R_DrawAliasModel (entity_t *e);
void R_DrawBrushModel (entity_t *e);
void R_DrawSpriteModel (entity_t *e);

/* what the chip was actually fed this frame */
extern int	nv1_c_patches, nv1_c_texels;

/*
 * The NV1 knobs.  On this chip a primitive costs its texel count and nothing
 * else, so these are the whole performance story.
 */
extern cvar_t	nv1_subdiv;		/* texels per screen pixel */
extern cvar_t	nv1_maxsubdiv;		/* ceiling on the subdivision power */
extern cvar_t	nv1_minsubdiv;		/* floor */
extern cvar_t	nv1_lightmap;		/* per-vertex beta lighting on/off */
extern cvar_t	nv1_showtexels;
extern cvar_t	nv1_clear;
extern cvar_t	nv1_zslab;
extern cvar_t	nv1_maxtexture;
extern cvar_t	nv1_worldsubdiv;
extern cvar_t	nv1_skylayers;

extern cvar_t	gl_subdivide_size;

/* sky, from nv1_warp.c */
extern int	solidskytexture;
extern int	alphaskytexture;

#endif /* NV1QUAKE_H */
