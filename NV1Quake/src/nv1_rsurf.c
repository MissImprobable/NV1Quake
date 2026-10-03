/*
nv1_rsurf.c -- world surface rendering.

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


Two things are different from glquake, and both come from the same fact: there
is no depth buffer.

First, the BSP is walked BACK to front and surfaces are drawn as they are
reached, rather than being gathered into texture chains and sorted by texture.
A BSP tree walked far-child-first is an exact back-to-front ordering, which is
what Quake's own software renderer relied on before hardware z-buffers existed.
Sorting by texture would be faster on a card that cares about state changes;
this one has no texture state to change, so it would only break the ordering.

Second, there are no lightmap textures.  The chip has one texture unit and no
multitexture, but it does have a per-control-point beta that modulates the
texels.  So lightmaps are sampled per vertex at load-time-computed coordinates
and become four beta values per patch.  Quake's lightmaps are one sample per 16
world units, and Quake's polygons are small, so per vertex is much closer to
per texel here than it sounds.
*/

#include "quakedef.h"
#include "nv1quake.h"

extern model_t	*loadmodel;

void BoundPoly (int numverts, float *verts, vec3_t mins, vec3_t maxs);

/*
 * A surface's drawing data.  This replaces glpoly_t; the layout is the same
 * idea but the texture coordinates are kept in texels rather than normalised,
 * because the resampler wants texels and normalising only to multiply back up
 * every frame would be silly.
 */
typedef struct nv1poly_s
{
	struct nv1poly_s	*next;
	struct nv1poly_s	*chain;
	int			numverts;
	float			verts[4][7];	/* variable sized: xyz s t ls lt */
} nv1poly_t;

msurface_t	*skychain = NULL;
msurface_t	*waterchain = NULL;

void R_RenderDynamicLightmaps (msurface_t *fa);

/*
===============
R_TextureAnimation

Returns the proper texture for a given time and base texture
===============
*/
texture_t *R_TextureAnimation (texture_t *base)
{
	int	reletive;
	int	count;

	if (currententity->frame)
	{
		if (base->alternate_anims)
			base = base->alternate_anims;
	}

	if (!base->anim_total)
		return base;

	reletive = (int)(cl.time * 10) % base->anim_total;

	count = 0;
	while (base->anim_min > reletive || base->anim_max <= reletive)
	{
		base = base->anim_next;
		if (!base)
			Sys_Error ("R_TextureAnimation: broken cycle");
		if (++count > 100)
			Sys_Error ("R_TextureAnimation: infinite cycle");
	}

	return base;
}

/*
=============================================================================

  lighting

  Sampled per vertex straight out of the BSP lightmap data.  No lightmap
  textures are ever built, so none of glquake's block allocator, upload
  scheduling or dirty rectangle tracking exists here.

=============================================================================
*/

/*
===============
NV1_SampleLightmap

ls and lt are lightmap texel coordinates within the surface.
Returns S1.15 beta.
===============
*/
static int NV1_SampleLightmap (msurface_t *surf, float ls, float lt)
{
	int	smax, tmax;
	int	is, it;
	byte	*lightmap;
	int	maps;
	int	light;

	if (r_fullbright.value || !cl.worldmodel->lightdata || !surf->samples)
		return NV1_BETAONE;

	smax = (surf->extents[0] >> 4) + 1;
	tmax = (surf->extents[1] >> 4) + 1;

	is = (int)(ls + 0.5f);
	it = (int)(lt + 0.5f);

	if (is < 0) is = 0; else if (is >= smax) is = smax - 1;
	if (it < 0) it = 0; else if (it >= tmax) it = tmax - 1;

	lightmap = surf->samples + it * smax + is;

	light = 0;
	for (maps = 0 ; maps < MAXLIGHTMAPS && surf->styles[maps] != 255 ; maps++)
	{
		light += (*lightmap) * d_lightstylevalue[surf->styles[maps]];
		lightmap += smax * tmax;
	}

/*
 * >> 7 and clamp is exactly what glquake's R_BuildLightMap does before it
 * hands the value to the lightmap texture.  A style scale is around 264, so a
 * sample of about 124 already saturates; getting this shift wrong by one makes
 * the whole world half as bright, which is what it did.
 */
	light >>= 7;
	if (light > 255)
		light = 255;

/* 0..255 to S1.15 */
	return light << 7;
}

/*
===============
NV1_AddDynamicLight

glquake rebuilds a lightmap block and re-uploads it.  With per-vertex light
there is nothing to rebuild: the contribution is just evaluated at the point.
===============
*/
static int NV1_AddDynamicLight (msurface_t *surf, vec3_t world, int beta)
{
	int	lnum;
	float	dist;
	float	add;
	vec3_t	delta;

	if (!r_dynamic.value)
		return beta;

	for (lnum = 0 ; lnum < MAX_DLIGHTS ; lnum++)
	{
		if (!(surf->dlightbits & (1 << lnum)))
			continue;
		if (cl_dlights[lnum].die < cl.time || !cl_dlights[lnum].radius)
			continue;

		VectorSubtract (cl_dlights[lnum].origin, world, delta);
		dist = Length (delta);

		add = cl_dlights[lnum].radius - dist;
		if (add <= 0)
			continue;

		beta += (int)(add * (NV1_BETAONE / 256.0));
		if (beta >= NV1_BETAONE)
			return NV1_BETAONE;
	}

	return beta;
}

/*
=============================================================================

  surface drawing

=============================================================================
*/

/*
================
NV1_BuildSurfaceVerts

Fill the submission vertex array from the surface's stored polygon.
================
*/
static int NV1_BuildSurfaceVerts (msurface_t *fa, nv1poly_t *p,
	nv1texture_t *tex, nv1vert_t *out, int maxout, qboolean lit)
{
	int	i, n;
	float	*v;
	float	texw, texh;
	float	sw, sh;

	n = p->numverts;
	if (n > maxout)
		n = maxout;

/*
 * The stored coordinates are in the texels of the full size texture.  If the
 * texture manager had to scale the texture to a power of two, scale with it.
 */
	texw = (float)tex->width;
	texh = (float)tex->height;
	sw = texw / (float)fa->texinfo->texture->width;
	sh = texh / (float)fa->texinfo->texture->height;

	for (i = 0 ; i < n ; i++)
	{
		v = p->verts[i];

		NV1_TransformPoint (v, out[i].xyz);

		out[i].s = v[3] * sw;
		out[i].t = v[4] * sh;

		if (lit)
		{
			int	beta = NV1_SampleLightmap (fa, v[5], v[6]);

			beta = NV1_AddDynamicLight (fa, v, beta);
			out[i].beta = beta;
		}
		else
			out[i].beta = NV1_BETAONE;
	}

	return n;
}

/*
================
R_RenderBrushPoly
================
*/
void R_RenderBrushPoly (msurface_t *fa)
{
	texture_t	*t;
	nv1texture_t	*tex;
	nv1vert_t	verts[MAX_SURFVERTS];
	nv1poly_t	*p;
	int		n;
	qboolean	lit;

	c_brush_polys++;

	if (fa->flags & SURF_DRAWSKY)
	{
		EmitBothSkyLayers (fa);
		return;
	}

	t = R_TextureAnimation (fa->texinfo->texture);

	if (t->gl_texturenum < 0 || t->gl_texturenum >= numnv1textures)
		return;
	tex = &nv1textures[t->gl_texturenum];

	if (fa->flags & SURF_DRAWTURB)
	{
		EmitWaterPolys (fa);
		return;
	}

	lit = (nv1_lightmap.value != 0) && !r_fullbright.value;

	for (p = (nv1poly_t *)fa->polys ; p ; p = p->next)
	{
		n = NV1_BuildSurfaceVerts (fa, p, tex, verts, MAX_SURFVERTS, lit);
		if (n >= 3)
			NV1_DrawPolygon (verts, n, tex, lit);
	}
}

/*
================
R_DrawWaterSurfaces

Water is drawn inline during the back to front walk, because with no depth
buffer the only thing that keeps it in the right place is submission order.
This is left as a hook so the shared code still has something to call.
================
*/
void R_DrawWaterSurfaces (void)
{
	waterchain = NULL;
}

void R_MirrorChain (msurface_t *s)
{
	(void)s;
}

void GL_DisableMultitexture (void)
{
}

void GL_EnableMultitexture (void)
{
}

void R_RenderDynamicLightmaps (msurface_t *fa)
{
	(void)fa;
}

/*
=============================================================================

  world

=============================================================================
*/

/*
=================
R_DrawBrushModel
=================
*/
void R_DrawBrushModel (entity_t *e)
{
	int		i, k;
	vec3_t		mins, maxs;
	msurface_t	*psurf;
	float		dot;
	mplane_t	*pplane;
	model_t		*clmodel;
	qboolean	rotated;

	currententity = e;

	clmodel = e->model;

	if (e->angles[0] || e->angles[1] || e->angles[2])
	{
		rotated = true;
		for (i = 0 ; i < 3 ; i++)
		{
			mins[i] = e->origin[i] - clmodel->radius;
			maxs[i] = e->origin[i] + clmodel->radius;
		}
	}
	else
	{
		rotated = false;
		VectorAdd (e->origin, clmodel->mins, mins);
		VectorAdd (e->origin, clmodel->maxs, maxs);
	}

	if (R_CullBox (mins, maxs))
		return;

	VectorSubtract (r_refdef.vieworg, e->origin, modelorg);
	if (rotated)
	{
		vec3_t	temp;
		vec3_t	forward, right, up;

		VectorCopy (modelorg, temp);
		AngleVectors (e->angles, forward, right, up);
		modelorg[0] = DotProduct (temp, forward);
		modelorg[1] = -DotProduct (temp, right);
		modelorg[2] = DotProduct (temp, up);
	}

	psurf = &clmodel->surfaces[clmodel->firstmodelsurface];

/* calculate dynamic lighting for bmodel if it's not an instanced model */
	if (clmodel->firstmodelsurface != 0 && !gl_flashblend.value)
	{
		for (k = 0 ; k < MAX_DLIGHTS ; k++)
		{
			if ((cl_dlights[k].die < cl.time) || (!cl_dlights[k].radius))
				continue;

			R_MarkLights (&cl_dlights[k], 1 << k,
				clmodel->nodes + clmodel->hulls[0].firstclipnode);
		}
	}

	NV1_PushEntityTransform (e);

/*
 * A brush model's own surfaces are not in the world BSP order, so they are
 * drawn in the order they appear.  These are small enough (doors, platforms)
 * that self-overlap is rare, and there is nothing better available without a
 * depth buffer.
 */
	for (i = 0 ; i < clmodel->nummodelsurfaces ; i++, psurf++)
	{
		pplane = psurf->plane;

		dot = DotProduct (modelorg, pplane->normal) - pplane->dist;

		if (((psurf->flags & SURF_PLANEBACK) && (dot < -BACKFACE_EPSILON)) ||
			(!(psurf->flags & SURF_PLANEBACK) && (dot > BACKFACE_EPSILON)))
		{
			R_RenderBrushPoly (psurf);
		}
	}

	NV1_PopEntityTransform ();
}

/*
================
R_MarkSurfaces

Pass one: walk the visible leaves and mark the surfaces they reference.

glquake did this inside the same traversal that draws, which works only
because it walks near side first: by the time a node's surfaces are drawn, the
near leaves that reference them have already been visited.  Painter's ordering
needs the opposite walk, far side first, which would reach every node's
surfaces before anything had marked them and draw nothing at all.  So marking
gets its own pass.
================
*/
static void R_MarkSurfaces (mnode_t *node)
{
	int		c;
	mleaf_t		*pleaf;
	msurface_t	**mark;

	if (node->contents == CONTENTS_SOLID)
		return;
	if (node->visframe != r_visframecount)
		return;
	if (R_CullBox (node->minmaxs, node->minmaxs + 3))
		return;

	if (node->contents < 0)
	{
		pleaf = (mleaf_t *)node;

		mark = pleaf->firstmarksurface;
		c = pleaf->nummarksurfaces;

		if (c)
		{
			do
			{
				(*mark)->visframe = r_framecount;
				mark++;
			} while (--c);
		}

	/* deal with model fragments in this leaf */
		if (pleaf->efrags)
			R_StoreEfrags (&pleaf->efrags);

		return;
	}

	R_MarkSurfaces (node->children[0]);
	R_MarkSurfaces (node->children[1]);
}

/*
================
R_RecursiveWorldNode

Pass two: draw, FAR CHILD FIRST.

Everything beyond a node's plane is further away than the surfaces lying on
that plane, which are in turn further than everything in front of it, so this
is an exact back to front ordering.  It is the painter's algorithm Quake's
software renderer used, and on a chip with no depth buffer it is not a
fallback, it is the only thing that works.

Each surface belongs to exactly one node, so nothing is drawn twice.
================
*/
void R_RecursiveWorldNode (mnode_t *node)
{
	int		c, side;
	mplane_t	*plane;
	msurface_t	*surf;
	double		dot;

	if (node->contents == CONTENTS_SOLID)
		return;
	if (node->visframe != r_visframecount)
		return;
	if (R_CullBox (node->minmaxs, node->minmaxs + 3))
		return;

/*
 * A leaf holds no surfaces of its own -- the marking pass dealt with those --
 * but it is the right moment to draw the entities that live in it, which is
 * how a door ends up behind the column in front of it.
 */
	if (node->contents < 0)
	{
		NV1_DrawLeafEntities ((int)((mleaf_t *)node - cl.worldmodel->leafs));
		return;
	}

	plane = node->plane;

	switch (plane->type)
	{
	case PLANE_X:
		dot = modelorg[0] - plane->dist;
		break;
	case PLANE_Y:
		dot = modelorg[1] - plane->dist;
		break;
	case PLANE_Z:
		dot = modelorg[2] - plane->dist;
		break;
	default:
		dot = DotProduct (modelorg, plane->normal) - plane->dist;
		break;
	}

/* children[0] is the front of the plane, children[1] the back */
	side = (dot >= 0) ? 0 : 1;

/* the half we are NOT in is further away, so it goes down first */
	R_RecursiveWorldNode (node->children[side ^ 1]);

/* then the surfaces on the plane itself */
	c = node->numsurfaces;
	surf = cl.worldmodel->surfaces + node->firstsurface;

	for ( ; c ; c--, surf++)
	{
		if (surf->visframe != r_framecount)
			continue;

	/* wrong side.  Underwater surfaces are exempt because they warp and
	   would pop at the edges, same as glquake. */
		if (!(surf->flags & SURF_UNDERWATER) &&
			((dot < 0) ^ !!(surf->flags & SURF_PLANEBACK)))
			continue;

		R_RenderBrushPoly (surf);
	}

/* and finally the half we are in, which is nearest */
	R_RecursiveWorldNode (node->children[side]);
}

/*
=============
R_DrawWorld
=============
*/
void R_DrawWorld (void)
{
	entity_t	ent;

	memset (&ent, 0, sizeof(ent));
	ent.model = cl.worldmodel;

	VectorCopy (r_refdef.vieworg, modelorg);

	currententity = &ent;

	NV1_PopEntityTransform ();

	NV1_LinkEntitiesToLeaves ();

/* anything with no place in the traversal goes down before the world does */
	NV1_DrawEarlyEntities ();

/* the marking pass must run even with the world hidden: entity fragments are
   stored from it, so skipping it would hide every entity too */
	R_MarkSurfaces (cl.worldmodel->nodes);

	if (!r_drawworld.value)
		return;

	R_RecursiveWorldNode (cl.worldmodel->nodes);
}

/*
===============
R_MarkLeaves
===============
*/
void R_MarkLeaves (void)
{
	byte	*vis;
	mnode_t	*node;
	int	i;
	byte	solid[4096];

	if (r_oldviewleaf == r_viewleaf && !r_novis.value)
		return;

	if (mirror)
		return;

	r_visframecount++;
	r_oldviewleaf = r_viewleaf;

	if (r_novis.value)
	{
		vis = solid;
		memset (solid, 0xff, (cl.worldmodel->numleafs + 7) >> 3);
	}
	else
		vis = Mod_LeafPVS (r_viewleaf, cl.worldmodel);

	for (i = 0 ; i < cl.worldmodel->numleafs ; i++)
	{
		if (vis[i >> 3] & (1 << (i & 7)))
		{
			node = (mnode_t *)&cl.worldmodel->leafs[i + 1];
			do
			{
				if (node->visframe == r_visframecount)
					break;
				node->visframe = r_visframecount;
				node = node->parent;
			} while (node);
		}
	}
}

/*
=============================================================================

  surface setup

=============================================================================
*/

/*
=============================================================================

  surface setup

=============================================================================
*/

/*
 * How finely world surfaces are chopped, in world units.  0 turns it off.
 *
 * This matters more here than it would on a card that rasterises triangles.
 * A quadratic patch has nine control points and interpolates linearly in
 * texture space between them, so one patch covering a whole 512 unit wall
 * both smears its texture and bows visibly under perspective.  And since
 * lighting is a beta value at the patch corners, a large polygon has nothing
 * to interpolate between and comes out flat -- which is why unsubdivided
 * ceilings render as solid black slabs.
 *
 * Chopping costs polygons, and on NV1 polygons cost texels.  128 is a
 * compromise: a patch spans eight lightmap samples, which is enough gradient
 * to read as lit, without multiplying the texel budget by four.
 */
cvar_t	nv1_worldsubdiv = {"nv1_worldsubdiv", "128", true};

static msurface_t	*subdiv_surf;

/*
================
NV1_EmitSubPoly

One finished polygon, with texture coordinates in texels and lightmap
coordinates in lightmap texels.
================
*/
static void NV1_EmitSubPoly (int numverts, float *verts)
{
	nv1poly_t	*poly;
	msurface_t	*fa = subdiv_surf;
	int		i;
	float		s, t;

	if (numverts < 3)
		return;

	poly = (nv1poly_t *)Hunk_Alloc (sizeof(nv1poly_t) +
		(numverts - 4) * 7 * sizeof(float));
	poly->next = (struct nv1poly_s *)fa->polys;
	fa->polys = (glpoly_t *)poly;
	poly->numverts = numverts;

	for (i = 0 ; i < numverts ; i++, verts += 3)
	{
		VectorCopy (verts, poly->verts[i]);

		s = DotProduct (verts, fa->texinfo->vecs[0]) + fa->texinfo->vecs[0][3];
		t = DotProduct (verts, fa->texinfo->vecs[1]) + fa->texinfo->vecs[1][3];

		poly->verts[i][3] = s;
		poly->verts[i][4] = t;

	/* lightmap coordinates, in lightmap texels */
		poly->verts[i][5] = (s - fa->texturemins[0]) / 16.0f;
		poly->verts[i][6] = (t - fa->texturemins[1]) / 16.0f;
	}
}

/*
================
NV1_SubdividePoly

Chop along axial boundaries, same shape of algorithm as the warp subdivider,
but emitting lit polygons instead of warp ones.
================
*/
static void NV1_SubdividePoly (int numverts, float *verts, float size)
{
	int	i, j, k;
	vec3_t	mins, maxs;
	float	m;
	float	*v;
	vec3_t	front[64], back[64];
	int	f, b;
	float	dist[64];
	float	frac;

	if (numverts > 60)
	{
	/* too complex to chop safely; take it whole */
		NV1_EmitSubPoly (numverts, verts);
		return;
	}

	BoundPoly (numverts, verts, mins, maxs);

	for (i = 0 ; i < 3 ; i++)
	{
		m = (mins[i] + maxs[i]) * 0.5f;
		m = size * (float)floor (m / size + 0.5);

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
				frac = dist[j] / (dist[j] - dist[j + 1]);
				for (k = 0 ; k < 3 ; k++)
					front[f][k] = back[b][k] =
						v[k] + frac * (v[3 + k] - v[k]);
				f++;
				b++;
			}
		}

		NV1_SubdividePoly (f, front[0], size);
		NV1_SubdividePoly (b, back[0], size);
		return;
	}

	NV1_EmitSubPoly (numverts, verts);
}

/*
================
BuildSurfaceDisplayList

glquake normalised texture and lightmap coordinates because that is what GL
wanted.  We keep both in texels, because the resampler indexes texels and the
lightmap sampler indexes lightmap texels, and converting every frame would be
work for nothing.
================
*/
void BuildSurfaceDisplayList (msurface_t *fa)
{
	int	i, lindex, lnumverts;
	medge_t	*pedges, *r_pedge;
	float	*vec;
	vec3_t	verts[64];
	float	size;

	pedges = loadmodel->edges;
	lnumverts = fa->numedges;

	if (lnumverts > 64)
		lnumverts = 64;

/* reconstruct the polygon */
	for (i = 0 ; i < lnumverts ; i++)
	{
		lindex = loadmodel->surfedges[fa->firstedge + i];

		if (lindex > 0)
		{
			r_pedge = &pedges[lindex];
			vec = loadmodel->vertexes[r_pedge->v[0]].position;
		}
		else
		{
			r_pedge = &pedges[-lindex];
			vec = loadmodel->vertexes[r_pedge->v[1]].position;
		}

		VectorCopy (vec, verts[i]);
	}

	subdiv_surf = fa;

	size = nv1_worldsubdiv.value;
	if (size >= 16)
		NV1_SubdividePoly (lnumverts, verts[0], size);
	else
		NV1_EmitSubPoly (lnumverts, verts[0]);
}

/*
==================
GL_CreateSurfaceLightmap

Nothing to create: the lightmap stays where the BSP put it and is sampled in
place.  Kept because the loader calls it.
==================
*/
void GL_CreateSurfaceLightmap (msurface_t *surf)
{
	(void)surf;
}

/*
==================
GL_BuildLightmaps

Builds the polygon data for every surface in every loaded model.
==================
*/
void GL_BuildLightmaps (void)
{
	int	i, j;
	model_t	*m;

	r_framecount = 1;		/* no dlightcache */

	for (j = 1 ; j < MAX_MODELS ; j++)
	{
		m = cl.model_precache[j];
		if (!m)
			break;
		if (m->name[0] == '*')
			continue;

		loadmodel = m;

		for (i = 0 ; i < m->numsurfaces ; i++)
		{
			if (m->surfaces[i].flags & (SURF_DRAWTURB | SURF_DRAWSKY))
				continue;	/* built by GL_SubdivideSurface */

			BuildSurfaceDisplayList (m->surfaces + i);
		}
	}
}
