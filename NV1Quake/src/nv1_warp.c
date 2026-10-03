/*
nv1_warp.c -- sky and water.

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

#include "quakedef.h"
#include "nv1quake.h"

extern	model_t	*loadmodel;

int		solidskytexture;
int		alphaskytexture;
float		speedscale;		/* for top sky and bottom sky */

msurface_t	*warpface;

/* gl_subdivide_size is defined in nv1_model.c, as it was in gl_model.c */

/*
 * Same layout as the polygons nv1_rsurf.c builds: xyz, texture s/t in texels,
 * lightmap s/t in lightmap texels.
 */
typedef struct nv1poly_s
{
	struct nv1poly_s	*next;
	struct nv1poly_s	*chain;
	int			numverts;
	float			verts[4][7];
} nv1poly_t;

void BoundPoly (int numverts, float *verts, vec3_t mins, vec3_t maxs)
{
	int	i, j;
	float	*v;

	mins[0] = mins[1] = mins[2] = 9999;
	maxs[0] = maxs[1] = maxs[2] = -9999;
	v = verts;

	for (i = 0 ; i < numverts ; i++)
	{
		for (j = 0 ; j < 3 ; j++, v++)
		{
			if (*v < mins[j])
				mins[j] = *v;
			if (*v > maxs[j])
				maxs[j] = *v;
		}
	}
}

void SubdividePolygon (int numverts, float *verts)
{
	int		i, j, k;
	vec3_t		mins, maxs;
	float		m;
	float		*v;
	vec3_t		front[64], back[64];
	int		f, b;
	float		dist[64];
	float		frac;
	nv1poly_t	*poly;
	float		s, t;

	if (numverts > 60)
		Sys_Error ("numverts = %i", numverts);

	BoundPoly (numverts, verts, mins, maxs);

	for (i = 0 ; i < 3 ; i++)
	{
		m = (mins[i] + maxs[i]) * 0.5;
		m = gl_subdivide_size.value * floor (m / gl_subdivide_size.value + 0.5);
		if (maxs[i] - m < 8)
			continue;
		if (m - mins[i] < 8)
			continue;

	/* cut it */
		v = verts + i;
		for (j = 0 ; j < numverts ; j++, v += 3)
			dist[j] = *v - m;

	/* wrap cases */
		dist[j] = dist[0];
		v -= i;
		VectorCopy (verts, v);

		f = b = 0;
		v = verts;
		for (j = 0 ; j < numverts ; j++, v += 3)
		{
			if (dist[j] >= 0)
			{
				VectorCopy (v, front[f]);
				f++;
			}
			if (dist[j] <= 0)
			{
				VectorCopy (v, back[b]);
				b++;
			}
			if (dist[j] == 0 || dist[j + 1] == 0)
				continue;
			if ((dist[j] > 0) != (dist[j + 1] > 0))
			{
			/* clip point */
				frac = dist[j] / (dist[j] - dist[j + 1]);
				for (k = 0 ; k < 3 ; k++)
					front[f][k] = back[b][k] = v[k] + frac * (v[3 + k] - v[k]);
				f++;
				b++;
			}
		}

		SubdividePolygon (f, front[0]);
		SubdividePolygon (b, back[0]);
		return;
	}

	poly = (nv1poly_t *)Hunk_Alloc (sizeof(nv1poly_t) +
		(numverts - 4) * 7 * sizeof(float));
	poly->next = (struct nv1poly_s *)warpface->polys;
	warpface->polys = (glpoly_t *)poly;
	poly->numverts = numverts;

	for (i = 0 ; i < numverts ; i++, verts += 3)
	{
		VectorCopy (verts, poly->verts[i]);
		s = DotProduct (verts, warpface->texinfo->vecs[0]);
		t = DotProduct (verts, warpface->texinfo->vecs[1]);
		poly->verts[i][3] = s;
		poly->verts[i][4] = t;
		poly->verts[i][5] = 0;
		poly->verts[i][6] = 0;
	}
}

/*
================
GL_SubdivideSurface

Breaks a polygon up along axial 64 unit boundaries so that turbulent and sky
warps can be done reasonably.

On NV1 this matters twice over: a patch is drawn with a fixed texel grid, so a
big warped polygon would have to be subdivided to look right anyway.
================
*/
void GL_SubdivideSurface (msurface_t *fa)
{
	vec3_t	verts[64];
	int	numverts;
	int	i;
	int	lindex;
	float	*vec;

	warpface = fa;

/* convert edges back to a normal polygon */
	numverts = 0;
	for (i = 0 ; i < fa->numedges ; i++)
	{
		lindex = loadmodel->surfedges[fa->firstedge + i];

		if (lindex > 0)
			vec = loadmodel->vertexes[loadmodel->edges[lindex].v[0]].position;
		else
			vec = loadmodel->vertexes[loadmodel->edges[-lindex].v[1]].position;
		VectorCopy (vec, verts[numverts]);
		numverts++;
	}

	SubdividePolygon (numverts, verts[0]);
}

/*
=============================================================================

  water

=============================================================================
*/

#define	TURBSCALE	(256.0 / (2 * M_PI))

float	turbsin[] =
{
	#include "gl_warp_sin.h"
};

/*
=============
EmitWaterPolys

Does a water warp on the pre-fragmented glpoly_t chain.
=============
*/
void EmitWaterPolys (msurface_t *fa)
{
	nv1poly_t	*p;
	float		*v;
	int		i, n;
	float		s, t, os, ot;
	nv1vert_t	verts[MAX_SURFVERTS];
	texture_t	*t2;
	nv1texture_t	*tex;
	float		sw, sh;
	int		beta;

	t2 = fa->texinfo->texture;
	if (t2->gl_texturenum < 0 || t2->gl_texturenum >= numnv1textures)
		return;
	tex = &nv1textures[t2->gl_texturenum];

	sw = (float)tex->width / (float)t2->width;
	sh = (float)tex->height / (float)t2->height;

/*
 * Water has no lightmap in Quake, so it is drawn at a fixed level.  On a card
 * with real alpha we would also fade it by r_wateralpha; here it is opaque,
 * because the patch path streams texels and has no per-texel alpha at all.
 */
	beta = NV1_BETAONE;

	for (p = (nv1poly_t *)fa->polys ; p ; p = p->next)
	{
		n = p->numverts;
		if (n > MAX_SURFVERTS)
			n = MAX_SURFVERTS;

		for (i = 0 ; i < n ; i++)
		{
			v = p->verts[i];

			os = v[3];
			ot = v[4];

			s = os + turbsin[(int)((ot * 0.125 + realtime) * TURBSCALE) & 255];
			t = ot + turbsin[(int)((os * 0.125 + realtime) * TURBSCALE) & 255];

			NV1_TransformPoint (v, verts[i].xyz);
			verts[i].s = s * sw;
			verts[i].t = t * sh;
			verts[i].beta = beta;
		}

		if (n >= 3)
			NV1_DrawPolygon (verts, n, tex, false);
	}
}

/*
=============================================================================

  sky

  Quake's sky is two 128x128 layers scrolling at different rates: an opaque
  starfield and a cut-out cloud layer over it.  The cloud layer needs
  transparency, so until the ARGB1555 path existed only the solid layer was
  drawn and the sky had no moving detail.  Both are drawn now.

  It is not free -- the sky is often the largest thing on screen and this
  doubles what it costs -- so nv1_skylayers drops back to one layer.

=============================================================================
*/

cvar_t	nv1_skylayers = {"nv1_skylayers", "2", true};

/*
=============
EmitSkyPolys
=============
*/
static void EmitSkyLayer (msurface_t *fa, int texnum)
{
	nv1poly_t	*p;
	float		*v;
	int		i, n;
	float		s, t;
	vec3_t		dir;
	float		length;
	nv1vert_t	verts[MAX_SURFVERTS];
	nv1texture_t	*tex;

	if (texnum < 0 || texnum >= numnv1textures)
		return;
	tex = &nv1textures[texnum];

	for (p = (nv1poly_t *)fa->polys ; p ; p = p->next)
	{
		n = p->numverts;
		if (n > MAX_SURFVERTS)
			n = MAX_SURFVERTS;

		for (i = 0 ; i < n ; i++)
		{
			v = p->verts[i];

			VectorSubtract (v, r_origin, dir);
			dir[2] *= 3;	/* flatten the sphere */

			length = dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2];
			length = sqrt (length);
			length = 6 * 63 / length;

			dir[0] *= length;
			dir[1] *= length;

			s = (speedscale + dir[0]) / 128.0;
			t = (speedscale + dir[1]) / 128.0;

			NV1_TransformPoint (v, verts[i].xyz);
			verts[i].s = s * (float)tex->width;
			verts[i].t = t * (float)tex->height;
			verts[i].beta = NV1_BETAONE;
		}

		if (n >= 3)
			NV1_DrawPolygon (verts, n, tex, false);
	}
}

void EmitSkyPolys (msurface_t *fa)
{
	EmitSkyLayer (fa, solidskytexture);
}

/*
===============
EmitBothSkyLayers
===============
*/
void EmitBothSkyLayers (msurface_t *fa)
{
	speedscale = realtime * 8;
	speedscale -= (int)speedscale & ~127;

	EmitSkyLayer (fa, solidskytexture);

	if (nv1_skylayers.value < 2)
		return;

	speedscale = realtime * 16;
	speedscale -= (int)speedscale & ~127;

	EmitSkyLayer (fa, alphaskytexture);
}

/*
=================
R_DrawSkyChain
=================
*/
void R_DrawSkyChain (msurface_t *s)
{
	msurface_t	*fa;

	for (fa = s ; fa ; fa = fa->texturechain)
		EmitBothSkyLayers (fa);
}

/*
=============
R_InitSky

A sky texture is 256*128: the first 128*128 is the cloud layer and the second
is the solid starfield behind it.

The cloud layer marks its transparent texels with palette index 0, not the 255
the rest of Quake uses, so they are remapped before upload -- the texture
manager keys cut-outs on 255.
=============
*/
void R_InitSky (texture_t *mt)
{
	int		i, j;
	byte		*src;
	static byte	solid[128 * 128];
	static byte	alpha[128 * 128];
	byte		c;

	src = (byte *)mt + mt->offsets[0];

	for (i = 0 ; i < 128 ; i++)
	{
		for (j = 0 ; j < 128 ; j++)
		{
			solid[i * 128 + j] = src[i * 256 + j + 128];

			c = src[i * 256 + j];
			alpha[i * 128 + j] = (c == 0) ? 255 : c;
		}
	}

	solidskytexture = NV1_LoadTexture ("solidsky", 128, 128, solid, false, false);
	alphaskytexture = NV1_LoadTexture ("alphasky", 128, 128, alpha, false, true);
}
