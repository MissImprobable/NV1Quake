/*
nv1_rmain.c -- view setup, transform, and the patch submission core.

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


NV1 has no transform hardware, no clipper, and no depth buffer, so all three
live here.

The submission path is the interesting part.  A triangle becomes a quadratic
patch by collapsing one edge of the patch to a point: p0 and p2 are the base,
p6 and p8 are both the apex.  The five interior control points are placed by
perspective interpolation, and because each patch edge is an INTERPOLATING
quadratic, the curve then passes exactly through those perspective-correct
midpoints.  That is the whole trick -- the chip does no perspective division at
all, it just draws a curve that happens to bend the right way.

Then we resample the texture into a 2^HEIGHT by 2^WIDTH grid and stream it.
That grid is the entire cost of the primitive.  A patch covering four pixels
and a patch covering the whole screen cost the same if you ask for the same
subdivision, which is why choosing it from the projected size is the only
optimisation on this card that really matters.
*/

#include "quakedef.h"
#include "nv1quake.h"

entity_t	r_worldentity;

qboolean	r_cache_thrash;

vec3_t		modelorg, r_entorigin;
entity_t	*currententity;

int		r_visframecount;
int		r_framecount;

mplane_t	frustum[4];

int		c_brush_polys, c_alias_polys;
int		nv1_c_patches, nv1_c_texels;

qboolean	envmap;
int		particletexture;
int		playertextures;

int		skytexturenum;

int		mirrortexturenum;
qboolean	mirror;
mplane_t	*mirror_plane;

float		r_world_matrix[16];

/*
 * view origin
 */
vec3_t	vup;
vec3_t	vpn;
vec3_t	vright;
vec3_t	r_origin;

refdef_t	r_refdef;

mleaf_t		*r_viewleaf, *r_oldviewleaf;

texture_t	*r_notexture_mip;

int		d_lightstylevalue[256];

void R_MarkLeaves (void);

cvar_t	r_norefresh = {"r_norefresh","0"};
cvar_t	r_drawentities = {"r_drawentities","1"};
cvar_t	r_drawworld = {"r_drawworld","1"};
cvar_t	r_drawviewmodel = {"r_drawviewmodel","1"};
cvar_t	r_speeds = {"r_speeds","0"};
cvar_t	r_fullbright = {"r_fullbright","0"};
cvar_t	r_lightmap = {"r_lightmap","0"};
cvar_t	r_shadows = {"r_shadows","0"};
cvar_t	r_mirroralpha = {"r_mirroralpha","1"};
cvar_t	r_wateralpha = {"r_wateralpha","1"};
cvar_t	r_dynamic = {"r_dynamic","1"};
cvar_t	r_novis = {"r_novis","0"};

cvar_t	gl_playermip = {"gl_playermip","0"};
cvar_t	gl_nocolors = {"gl_nocolors","0"};
cvar_t	gl_doubleeyes = {"gl_doubleeys", "1"};
cvar_t	gl_keeptjunctions = {"gl_keeptjunctions","1"};
cvar_t	gl_reporttjunctions = {"gl_reporttjunctions","0"};
cvar_t	gl_polyblend = {"gl_polyblend","1"};
cvar_t	gl_flashblend = {"gl_flashblend","0"};

/*
 * The NV1 knobs.
 *
 * nv1_subdiv is texels per screen pixel.  1 means one texel per pixel, which
 * looks right and costs the most.  0.5 halves the linear resolution of every
 * patch and therefore quarters the texel traffic.
 *
 * nv1_maxsubdiv caps the subdivision power.  6 means no patch is ever more
 * than 64 texels on a side, i.e. 4096 texels, which is what stops one huge
 * near wall from eating an entire frame.
 */
cvar_t	nv1_subdiv = {"nv1_subdiv", "1", true};
cvar_t	nv1_maxsubdiv = {"nv1_maxsubdiv", "6", true};
cvar_t	nv1_minsubdiv = {"nv1_minsubdiv", "2", true};
cvar_t	nv1_lightmap = {"nv1_lightmap", "1", true};
cvar_t	nv1_showtexels = {"nv1_showtexels", "0"};
/*
 * On by default, unlike glquake's gl_clear.
 *
 * glquake could leave the colour buffer alone because the depth buffer plus
 * full world coverage meant every pixel got written every frame.  Here there
 * is no depth buffer, and anything the world fails to cover keeps whatever the
 * back buffer held two frames ago -- which reads as geometry smearing across
 * the screen and refusing to go away when you turn around.
 *
 * Set to 2 to clear magenta instead, which makes any remaining hole obvious
 * rather than looking like a dark corner.
 */
cvar_t	nv1_clear = {"nv1_clear", "1", true};

/*
 * Maximum far/near depth ratio a single patch may span before it is chopped
 * along z.  Lower is more accurate and more expensive; below 1.5 turns the
 * subdivision off entirely.
 */
cvar_t	nv1_zslab = {"nv1_zslab", "4", true};

/*
=============================================================================

  transform

  View space is x right, y up, z forward.  Projection is the usual divide by z
  scaled to pixels.

=============================================================================
*/

float	nv1_xscale, nv1_yscale;
float	nv1_xcenter, nv1_ycenter;
float	nv1_modelview[16];

/* entity transform, pushed around model drawing */
static vec3_t	ent_axis[3];
static vec3_t	ent_origin;
static qboolean	ent_transform;

/* view frustum in view space: x, z and y, z half-planes */
static float	clip_xz, clip_yz;

/*
================
NV1_SetupTransform
================
*/
void NV1_SetupTransform (void)
{
	float	fovx, fovy;

	nv1_xcenter = (float)glwidth * 0.5f + (float)glx;
	nv1_ycenter = (float)glheight * 0.5f + (float)gly;

	fovx = r_refdef.fov_x;
	fovy = r_refdef.fov_y;

	nv1_xscale = (float)glwidth * 0.5f / tan (fovx * (M_PI / 360.0));
	nv1_yscale = (float)glheight * 0.5f / tan (fovy * (M_PI / 360.0));

/* a point is inside the left/right planes when |x| * xscale <= z * width/2 */
	clip_xz = ((float)glwidth * 0.5f) / nv1_xscale;
	clip_yz = ((float)glheight * 0.5f) / nv1_yscale;

	ent_transform = false;
}

/*
================
NV1_PushEntityTransform
================
*/
void NV1_PushEntityTransform (entity_t *e)
{
	vec3_t	forward, right, up;

	AngleVectors (e->angles, forward, right, up);

	VectorCopy (forward, ent_axis[0]);
	VectorSubtract (vec3_origin, right, ent_axis[1]);
	VectorCopy (up, ent_axis[2]);
	VectorCopy (e->origin, ent_origin);

	ent_transform = true;
}

void NV1_PopEntityTransform (void)
{
	ent_transform = false;
}

/*
================
NV1_TransformPoint

Model or world space in, view space out.
================
*/
void NV1_TransformPoint (vec3_t in, vec3_t viewout)
{
	vec3_t	world, delta;

	if (ent_transform)
	{
		world[0] = ent_origin[0] + in[0] * ent_axis[0][0]
			 + in[1] * ent_axis[1][0] + in[2] * ent_axis[2][0];
		world[1] = ent_origin[1] + in[0] * ent_axis[0][1]
			 + in[1] * ent_axis[1][1] + in[2] * ent_axis[2][1];
		world[2] = ent_origin[2] + in[0] * ent_axis[0][2]
			 + in[1] * ent_axis[1][2] + in[2] * ent_axis[2][2];
	}
	else
		VectorCopy (in, world);

	VectorSubtract (world, r_origin, delta);

	viewout[0] = DotProduct (delta, vright);
	viewout[1] = DotProduct (delta, vup);
	viewout[2] = DotProduct (delta, vpn);
}

/*
=============================================================================

  clipping

  Everything is clipped in view space against five planes before projection.
  The near plane is mandatory -- there is no w divide in the hardware to save
  us from a vertex behind the eye.  The four side planes are not mandatory, but
  a patch that lands mostly off screen still costs its full texel count, so
  throwing the off-screen part away is free performance.

=============================================================================
*/

static void NV1_LerpVert (nv1vert_t *a, nv1vert_t *b, float f, nv1vert_t *out)
{
	out->xyz[0] = a->xyz[0] + f * (b->xyz[0] - a->xyz[0]);
	out->xyz[1] = a->xyz[1] + f * (b->xyz[1] - a->xyz[1]);
	out->xyz[2] = a->xyz[2] + f * (b->xyz[2] - a->xyz[2]);
	out->s = a->s + f * (b->s - a->s);
	out->t = a->t + f * (b->t - a->t);
	out->beta = a->beta + (int)(f * (float)(b->beta - a->beta));
}

/*
================
NV1_ClipToPlane

dist(v) is given by the caller through the plane selector.  Keeps the side
where dist >= 0.
================
*/
#define MAX_CLIPVERTS	32

static int NV1_ClipToPlane (nv1vert_t *in, int numin, nv1vert_t *out, int plane)
{
	float		dists[MAX_CLIPVERTS + 1];
	int		i, numout;
	nv1vert_t	*a, *b;
	float		f;

	if (numin < 3)
		return 0;

	for (i = 0 ; i < numin ; i++)
	{
		switch (plane)
		{
		case 0:	dists[i] = in[i].xyz[2] - NV1_NEARCLIP; break;
		case 1:	dists[i] = in[i].xyz[2] * clip_xz + in[i].xyz[0]; break;
		case 2:	dists[i] = in[i].xyz[2] * clip_xz - in[i].xyz[0]; break;
		case 3:	dists[i] = in[i].xyz[2] * clip_yz + in[i].xyz[1]; break;
		default: dists[i] = in[i].xyz[2] * clip_yz - in[i].xyz[1]; break;
		}
	}
	dists[numin] = dists[0];

	numout = 0;
	for (i = 0 ; i < numin ; i++)
	{
		a = &in[i];
		b = &in[(i + 1) % numin];

		if (dists[i] >= 0)
		{
			if (numout < MAX_CLIPVERTS)
				out[numout++] = *a;
		}

		if ((dists[i] >= 0) == (dists[i + 1] >= 0))
			continue;
		if (dists[i] == dists[i + 1])
			continue;

		f = dists[i] / (dists[i] - dists[i + 1]);
		if (numout < MAX_CLIPVERTS)
			NV1_LerpVert (a, b, f, &out[numout++]);
	}

	return numout;
}

/*
================
NV1_ClipZRange

Clip a polygon to a slab of view space depth, zlo <= z <= zhi.  Used to bound
how much perspective a single patch has to represent; see NV1_DrawPolygon.
================
*/
static int NV1_ClipZRange (nv1vert_t *in, int numin, nv1vert_t *out,
	float zlo, float zhi)
{
	static nv1vert_t	tmp[MAX_CLIPVERTS + 1];
	float			dists[MAX_CLIPVERTS + 1];
	int			pass, i, n, numout;
	nv1vert_t		*src, *dst;
	float			f;

	src = in;
	dst = tmp;
	n = numin;

	for (pass = 0 ; pass < 2 ; pass++)
	{
		if (n < 3)
			return 0;

		for (i = 0 ; i < n ; i++)
			dists[i] = pass ? (zhi - src[i].xyz[2]) : (src[i].xyz[2] - zlo);
		dists[n] = dists[0];

		numout = 0;
		for (i = 0 ; i < n ; i++)
		{
			nv1vert_t	*a = &src[i];
			nv1vert_t	*b = &src[(i + 1) % n];

			if (dists[i] >= 0 && numout < MAX_CLIPVERTS)
				dst[numout++] = *a;

			if ((dists[i] >= 0) == (dists[i + 1] >= 0))
				continue;
			if (dists[i] == dists[i + 1])
				continue;

			f = dists[i] / (dists[i] - dists[i + 1]);
			if (numout < MAX_CLIPVERTS)
				NV1_LerpVert (a, b, f, &dst[numout++]);
		}

		n = numout;

		if (pass == 0)
		{
			src = tmp;
			dst = out;
		}
	}

	return n;
}

/*
================
NV1_ClipPolygon
================
*/
int NV1_ClipPolygon (nv1vert_t *in, int numin, nv1vert_t *out)
{
	static nv1vert_t	tmp[MAX_CLIPVERTS + 1];
	int			n, plane;
	nv1vert_t		*src, *dst;

	if (numin > MAX_CLIPVERTS)
		numin = MAX_CLIPVERTS;

	memcpy (out, in, numin * sizeof(nv1vert_t));
	n = numin;

	src = out;
	dst = tmp;

	for (plane = 0 ; plane < 5 ; plane++)
	{
		n = NV1_ClipToPlane (src, n, dst, plane);
		if (n < 3)
			return 0;

		{
			nv1vert_t	*swap = src;
			src = dst;
			dst = swap;
		}
	}

	if (src != out)
		memcpy (out, src, n * sizeof(nv1vert_t));

	return n;
}

/*
=============================================================================

  patch submission

=============================================================================
*/

/* one texel grid, reused for every primitive; 256x256 is the legal maximum */
static unsigned short	nv1_grid[256 * 256];

typedef struct
{
	float	x, y;		/* screen */
	float	iz;		/* 1/z, the homogeneous w */
	float	s, t;		/* texels of mip 0 */
	int	beta;
} nv1proj_t;

/*
================
NV1_Project
================
*/
static void NV1_Project (nv1vert_t *v, nv1proj_t *p)
{
	float	iz;

	iz = 1.0f / v->xyz[2];

	p->x = nv1_xcenter + v->xyz[0] * nv1_xscale * iz;
	p->y = nv1_ycenter - v->xyz[1] * nv1_yscale * iz;
	p->iz = iz;
	p->s = v->s;
	p->t = v->t;
	p->beta = v->beta;
}

/*
================
NV1_SubdivPower

Pick the power of two subdivision for a screen-space extent.  Rounds up so we
never undersample, then obeys the cvar clamps.  Range is 2..8 because that is
what SubdivideIn can encode.
================
*/
static int NV1_SubdivPower (float extent)
{
	int	p;
	int	lo, hi;
	float	want;

	lo = (int)nv1_minsubdiv.value;
	hi = (int)nv1_maxsubdiv.value;
	if (lo < 2) lo = 2;
	if (hi > 8) hi = 8;
	if (hi < lo) hi = lo;

	want = extent * nv1_subdiv.value;

	p = lo;
	while (p < hi && (float)(1 << p) < want)
		p++;

	return p;
}

/*
================
NV1_SubmitPatch

The three projected vertices, with c as the apex whose corner is doubled.
================
*/
static void NV1_SubmitPatch (nv1proj_t *a, nv1proj_t *b, nv1proj_t *c,
	nv1texture_t *tex, qboolean lit)
{
	Nvu1Pt16	cp[9];
	U016		betas[4];
	int		hpow, wpow;
	int		nmaj, nmin;
	float		baselen, height;
	float		area2;
	float		alpha;
	int		mip, mipw, miph, mipwmask, miphmask, mipshift;
	unsigned short	*src;
	unsigned short	*grid;
	int		j, i;
	float		sA, tA, sB, tB, sC, tC;
	float		dsA, dtA, dsB, dtB;

/* screen extents drive both the subdivision and, through it, the cost */
	baselen = (float)fabs (b->x - a->x);
	{
		float	dy = (float)fabs (b->y - a->y);

		if (dy > baselen)
			baselen = dy;
	}

	area2 = (float)fabs ((b->x - a->x) * (c->y - a->y) -
			     (c->x - a->x) * (b->y - a->y));

/*
 * Truly degenerate only.  A higher threshold here drops the sliver triangles
 * that fan triangulation produces along polygon edges, and every one of those
 * is a hole in a floor.
 */
	if (area2 < 0.02f)
		return;

	if (baselen < 1.0f)
		baselen = 1.0f;
	height = area2 / baselen;

/* HEIGHT_02_68 runs along the base edge p0->p2, WIDTH_06_28 from base to apex */
	hpow = NV1_SubdivPower (baselen);
	wpow = NV1_SubdivPower (height);

	nmaj = 1 << hpow;
	nmin = 1 << wpow;

/*
 * Control points.  p0/p2 are the base, p6/p8 are both the apex.  The interior
 * points are placed by perspective interpolation: the screen position of the
 * 3D midpoint of an edge sits at weight iz1/(iz0+iz1) toward the first vertex,
 * which is exactly what NVLIB's nvldiv table computes.  We divide directly
 * instead of looking it up, because a table indexed by an integer 1..100 is
 * only there to avoid a divide on a 486 and it costs us real precision.
 */
	cp[0].x = (short)a->x;	cp[0].y = (short)a->y;
	cp[2].x = (short)b->x;	cp[2].y = (short)b->y;
	cp[6].x = (short)c->x;	cp[6].y = (short)c->y;
	cp[8].x = (short)c->x;	cp[8].y = (short)c->y;

	alpha = a->iz / (a->iz + b->iz);
	cp[1].x = (short)(b->x + alpha * (a->x - b->x));
	cp[1].y = (short)(b->y + alpha * (a->y - b->y));

	alpha = a->iz / (a->iz + c->iz);
	cp[3].x = (short)(c->x + alpha * (a->x - c->x));
	cp[3].y = (short)(c->y + alpha * (a->y - c->y));

	alpha = b->iz / (b->iz + c->iz);
	cp[5].x = (short)(c->x + alpha * (b->x - c->x));
	cp[5].y = (short)(c->y + alpha * (b->y - c->y));

/* edge 6-8 is the degenerate one, so its midpoint is the apex itself */
	cp[7].x = (short)c->x;	cp[7].y = (short)c->y;

/* centre: halfway along the curve from the base midpoint to the apex */
	alpha = (a->iz + b->iz) / (a->iz + b->iz + c->iz + c->iz);
	cp[4].x = (short)(cp[7].x + alpha * (cp[1].x - cp[7].x));
	cp[4].y = (short)(cp[7].y + alpha * (cp[1].y - cp[7].y));

/*
 * Mip selection.  The resampler strides through the source texture; if the
 * grid is much coarser than the texture region it covers, every step lands in
 * a different cache line.  Pick the level where roughly one source texel feeds
 * one grid cell.
 */
	if (!tex || !tex->mip[0])
		return;

	sA = a->s; tA = a->t;
	sB = b->s; tB = b->t;
	sC = c->s; tC = c->t;

	mip = 0;
	if (tex->nummips > 1)
	{
		float	texbase, texapex, ratio;

		texbase = (float)fabs (sB - sA);
		if ((float)fabs (tB - tA) > texbase)
			texbase = (float)fabs (tB - tA);
		texapex = (float)fabs (sC - sA);
		if ((float)fabs (tC - tA) > texapex)
			texapex = (float)fabs (tC - tA);

		ratio = texbase / (float)nmaj;
		if (texapex / (float)nmin < ratio)
			ratio = texapex / (float)nmin;

		while (mip < tex->nummips - 1 && ratio >= 2.0f)
		{
			ratio *= 0.5f;
			mip++;
		}
	}

	mipshift = mip;
	mipw = tex->width >> mip;
	miph = tex->height >> mip;
	if (mipw < 1) mipw = 1;
	if (miph < 1) miph = 1;
	mipwmask = mipw - 1;
	miphmask = miph - 1;
	src = tex->mip[mip];

/*
 * Resample.  Texture space is walked linearly, which is what makes NV1 what it
 * is: no perspective correction inside the patch, only at the control points.
 * u and v are masked rather than clamped, which is how a power of two texture
 * gets GL_REPEAT with no branch.
 */
	grid = nv1_grid;

	dsA = (sB - sA) / (float)nmaj;
	dtA = (tB - tA) / (float)nmaj;
	dsB = 0.0f;
	dtB = 0.0f;

	{
		float	s0 = sA + dsA * 0.5f;
		float	t0 = tA + dtA * 0.5f;

	/*
	 * NVLIB has a cut-out quad and a beta-lit quad, but not one that is
	 * both -- NVLIB_DrawBlendQTexQuadA does not exist in the shipped
	 * library.  Since we build the texel grid ourselves anyway, a lit
	 * cut-out folds its lighting into the texels here and then goes out
	 * through the plain cut-out entry point.
	 */
		int	foldlight = (tex->alpha && lit);
		int	bA = a->beta >> 7, bB = b->beta >> 7, bC = c->beta >> 7;

		for (j = 0 ; j < nmaj ; j++)
		{
			float	ds = (sC - s0) / (float)nmin;
			float	dt = (tC - t0) / (float)nmin;
			float	s = s0 + ds * 0.5f;
			float	t = t0 + dt * 0.5f;
			int	bbase = 255, bstep = 0;

			if (foldlight)
			{
			/* beta along the base edge at this strip, then toward
			   the apex across it */
				int	f = ((j << 8) + 128) >> hpow;

				bbase = bA + (((bB - bA) * f) >> 8);
				bstep = bC - bbase;
			}

			for (i = 0 ; i < nmin ; i++)
			{
				int		iu = ((int)s >> mipshift) & mipwmask;
				int		iv = ((int)t >> mipshift) & miphmask;
				unsigned short	texel = src[iv * mipw + iu];

				if (foldlight && (texel & 0x8000))
				{
					int	f = ((i << 8) + 128) >> wpow;
					int	beta = bbase + ((bstep * f) >> 8);
					int	r, g, bl;

					if (beta > 255) beta = 255;
					else if (beta < 0) beta = 0;

					r = (((texel >> 10) & 0x1f) * beta) >> 8;
					g = (((texel >> 5) & 0x1f) * beta) >> 8;
					bl = ((texel & 0x1f) * beta) >> 8;

					texel = (unsigned short)(0x8000 |
						(r << 10) | (g << 5) | bl);
				}

				*grid++ = texel;
				s += ds;
				t += dt;
			}

			s0 += dsA;
			t0 += dtA;
		}
	}

	(void)dsB; (void)dtB;

	nv1_c_patches++;
	nv1_c_texels += nmaj * nmin;

/*
 * Cut-out textures go through the ARGB1555 entry points, where bit 15 of each
 * texel says whether it is drawn at all.  Sprites -- explosions, bubbles, the
 * flame models -- are all cut-outs, and drawing them opaque puts a black box
 * around every one.
 */
	betas[0] = (U016)a->beta;
	betas[1] = (U016)b->beta;
	betas[2] = (U016)c->beta;
	betas[3] = (U016)c->beta;

	if (tex->alpha)
		NVLIB_DrawQTexQuadA ((U032 *)nv1_grid, wpow, hpow, cp);
	else if (lit)
		NVLIB_DrawBlendQTexQuad ((U032 *)nv1_grid, wpow, hpow, cp, betas);
	else
		NVLIB_DrawQTexQuad ((U032 *)nv1_grid, wpow, hpow, cp);
}

/*
================
NV1_SubmitTriangle

Pick which corner gets doubled, then submit.  The apex is the vertex opposite
the longest screen edge, so the strips run the long way and we need fewer of
them for the same fidelity.
================
*/
static void NV1_SubmitTriangle (nv1proj_t *v0, nv1proj_t *v1, nv1proj_t *v2,
	nv1texture_t *tex, qboolean lit)
{
	float	l0, l1, l2;
	float	dx, dy;

	dx = v1->x - v0->x; dy = v1->y - v0->y;
	l0 = dx * dx + dy * dy;			/* edge 0-1, opposite v2 */
	dx = v2->x - v1->x; dy = v2->y - v1->y;
	l1 = dx * dx + dy * dy;			/* edge 1-2, opposite v0 */
	dx = v0->x - v2->x; dy = v0->y - v2->y;
	l2 = dx * dx + dy * dy;			/* edge 2-0, opposite v1 */

	if (l0 >= l1 && l0 >= l2)
		NV1_SubmitPatch (v0, v1, v2, tex, lit);
	else if (l1 >= l2)
		NV1_SubmitPatch (v1, v2, v0, tex, lit);
	else
		NV1_SubmitPatch (v2, v0, v1, tex, lit);
}

/*
================
NV1_DrawPolygon

Clip, project, fan.  Convex polygons only, which is everything Quake has.
================
*/
void NV1_DrawPolygon (nv1vert_t *verts, int numverts, nv1texture_t *tex,
	qboolean lit)
{
	nv1vert_t	clipped[MAX_CLIPVERTS + 1];
	nv1vert_t	slab[MAX_CLIPVERTS + 1];
	nv1proj_t	proj[MAX_CLIPVERTS + 1];
	int		n, m, i;
	float		zmin, zmax;
	float		ratio, zhi, zlo;

	if (numverts < 3 || !tex)
		return;

	n = NV1_ClipPolygon (verts, numverts, clipped);
	if (n < 3)
		return;

/*
 * How much perspective is one patch being asked to carry?
 *
 * A quadratic patch walks texture space linearly and gets its perspective
 * purely from where the nine control points land.  That approximation is good
 * while the near and far ends of the polygon are within a few times each
 * other's depth, and falls apart badly beyond it: the interpolating quadratic
 * overshoots and the texture smears across the screen.  The view model is the
 * worst case in Quake -- it runs from the near plane to thirty units out --
 * and it is what made the weapon render as a grey smear in the corner.
 *
 * So chop the polygon into depth slabs, each spanning at most nv1_zslab to
 * one, and draw them far to near.  This is the perspective subdivision every
 * period engine did, and it is the same idea as subdividing the world
 * geometry, just applied along z instead of across the surface.
 */
	ratio = nv1_zslab.value;

	if (ratio < 1.5f)
	{
	/* subdivision disabled */
		for (i = 0 ; i < n ; i++)
			NV1_Project (&clipped[i], &proj[i]);
		for (i = 1 ; i < n - 1 ; i++)
			NV1_SubmitTriangle (&proj[0], &proj[i], &proj[i + 1], tex, lit);
		return;
	}

	zmin = zmax = clipped[0].xyz[2];
	for (i = 1 ; i < n ; i++)
	{
		if (clipped[i].xyz[2] < zmin) zmin = clipped[i].xyz[2];
		if (clipped[i].xyz[2] > zmax) zmax = clipped[i].xyz[2];
	}

	if (zmin < NV1_NEARCLIP)
		zmin = NV1_NEARCLIP;

	if (zmax <= zmin * ratio)
	{
	/* shallow enough to draw whole, which is the common case */
		for (i = 0 ; i < n ; i++)
			NV1_Project (&clipped[i], &proj[i]);
		for (i = 1 ; i < n - 1 ; i++)
			NV1_SubmitTriangle (&proj[0], &proj[i], &proj[i + 1], tex, lit);
		return;
	}

/* far slab first: with no depth buffer, order is everything */
	zhi = zmax * 1.01f;
	while (zhi > zmin)
	{
		zlo = zhi / ratio;
		if (zlo < zmin)
			zlo = zmin;

		m = NV1_ClipZRange (clipped, n, slab, zlo, zhi);

		if (m >= 3)
		{
			for (i = 0 ; i < m ; i++)
				NV1_Project (&slab[i], &proj[i]);
			for (i = 1 ; i < m - 1 ; i++)
				NV1_SubmitTriangle (&proj[0], &proj[i], &proj[i + 1],
					tex, lit);
		}

		if (zlo <= zmin)
			break;
		zhi = zlo;
	}
}

/*
================
NV1_DrawSolidPolygon

A flat colour still has to go through a patch, but a 4x4 grid is the smallest
one the hardware will take and sixteen texels is nothing.
================
*/
void NV1_DrawSolidPolygon (nv1vert_t *verts, int numverts, int color)
{
	static nv1texture_t	solid;
	static unsigned short	solidtexels[16];
	static qboolean		init;
	unsigned short		c;
	int			i;

	if (!init)
	{
		solid.width = solid.height = 4;
		solid.wpow = solid.hpow = 2;
		solid.nummips = 1;
		solid.used = true;
		solid.mip[0] = solidtexels;
		init = true;
	}

	c = nv1_palette555[color & 255];
	for (i = 0 ; i < 16 ; i++)
		solidtexels[i] = c;

	NV1_DrawPolygon (verts, numverts, &solid, false);
}

/*
=============================================================================

  culling

=============================================================================
*/

/*
=================
R_CullBox

Returns true if the box is completely outside the frustum
=================
*/
qboolean R_CullBox (vec3_t mins, vec3_t maxs)
{
	int	i;

	for (i = 0 ; i < 4 ; i++)
		if (BoxOnPlaneSide (mins, maxs, &frustum[i]) == 2)
			return true;

	return false;
}

/*
=============
R_RotateForEntity

Kept for source compatibility with the shared code; the transform is pushed
rather than multiplied into a matrix stack.
=============
*/
void R_RotateForEntity (entity_t *e)
{
	NV1_PushEntityTransform (e);
}

/*
=============================================================================

  sprites

=============================================================================
*/

/*
================
R_GetSpriteFrame
================
*/
mspriteframe_t *R_GetSpriteFrame (entity_t *currententity)
{
	msprite_t		*psprite;
	mspritegroup_t		*pspritegroup;
	mspriteframe_t		*pspriteframe;
	int			i, numframes, frame;
	float			*pintervals, fullinterval, targettime, time;

	psprite = currententity->model->cache.data;
	frame = currententity->frame;

	if ((frame >= psprite->numframes) || (frame < 0))
	{
		Con_Printf ("R_DrawSprite: no such frame %d\n", frame);
		frame = 0;
	}

	if (psprite->frames[frame].type == SPR_SINGLE)
	{
		pspriteframe = psprite->frames[frame].frameptr;
	}
	else
	{
		pspritegroup = (mspritegroup_t *)psprite->frames[frame].frameptr;
		pintervals = pspritegroup->intervals;
		numframes = pspritegroup->numframes;
		fullinterval = pintervals[numframes - 1];

		time = cl.time + currententity->syncbase;

		targettime = time - ((int)(time / fullinterval)) * fullinterval;

		for (i = 0 ; i < (numframes - 1) ; i++)
		{
			if (pintervals[i] > targettime)
				break;
		}

		pspriteframe = pspritegroup->frames[i];
	}

	return pspriteframe;
}

/*
=================
R_DrawSpriteModel
=================
*/
void R_DrawSpriteModel (entity_t *e)
{
	vec3_t		point;
	mspriteframe_t	*frame;
	float		*up, *right;
	vec3_t		v_forward, v_right, v_up;
	msprite_t	*psprite;
	nv1vert_t	verts[4];
	nv1texture_t	*tex;
	int		i;

	frame = R_GetSpriteFrame (e);
	psprite = e->model->cache.data;

	if (psprite->type == SPR_ORIENTED)
	{	/* bullet marks on walls */
		AngleVectors (currententity->angles, v_forward, v_right, v_up);
		up = v_up;
		right = v_right;
	}
	else
	{	/* normal sprite */
		up = vup;
		right = vright;
	}

	if (frame->gl_texturenum < 0 || frame->gl_texturenum >= numnv1textures)
		return;
	tex = &nv1textures[frame->gl_texturenum];

	NV1_PopEntityTransform ();

	VectorMA (e->origin, frame->down, up, point);
	VectorMA (point, frame->left, right, point);
	NV1_TransformPoint (point, verts[0].xyz);
	verts[0].s = 0;			verts[0].t = (float)tex->height;

	VectorMA (e->origin, frame->up, up, point);
	VectorMA (point, frame->left, right, point);
	NV1_TransformPoint (point, verts[1].xyz);
	verts[1].s = 0;			verts[1].t = 0;

	VectorMA (e->origin, frame->up, up, point);
	VectorMA (point, frame->right, right, point);
	NV1_TransformPoint (point, verts[2].xyz);
	verts[2].s = (float)tex->width;	verts[2].t = 0;

	VectorMA (e->origin, frame->down, up, point);
	VectorMA (point, frame->right, right, point);
	NV1_TransformPoint (point, verts[3].xyz);
	verts[3].s = (float)tex->width;	verts[3].t = (float)tex->height;

	for (i = 0 ; i < 4 ; i++)
		verts[i].beta = NV1_BETAONE;

	NV1_DrawPolygon (verts, 4, tex, false);
}

/*
=============================================================================

  alias models

=============================================================================
*/

#define NUMVERTEXNORMALS	162

float	r_avertexnormals[NUMVERTEXNORMALS][3] = {
#include "anorms.h"
};

vec3_t	shadevector;
float	shadelight, ambientlight;

/* precalculated dot products for quantized angles */
#define SHADEDOT_QUANT 16
float	r_avertexnormal_dots[SHADEDOT_QUANT][256] =
#include "anorm_dots.h"
;

float	*shadedots = r_avertexnormal_dots[0];

int	lastposenum;

/*
=============
NV1_DrawAliasFrame

Walks the strip and fan commands gl_mesh.c built.

Every command is emitted as INDIVIDUAL TRIANGLES, both strips and fans.  It is
tempting to hand a fan to the polygon path in one go, since a fan is a
polygon-shaped thing, but an alias model fan is neither convex nor planar --
it is just a run of triangles sharing a vertex.  The clipper is a
Sutherland-Hodgman convex clipper and quietly mangles it, which is what turned
the view model into a smooth grey cone while models built mostly from strips
came out fine.

Lighting collapses to a per-vertex beta, which is all the chip has.
=============
*/
void NV1_DrawAliasFrame (aliashdr_t *paliashdr, int posenum, nv1texture_t *tex,
	vec3_t scale, vec3_t scale_origin)
{
	float		l;
	trivertx_t	*verts;
	int		*order;
	int		count;
	nv1vert_t	v[3];
	nv1vert_t	first, prev;
	int		emitted;
	qboolean	isfan;
	vec3_t		local;

	lastposenum = posenum;

	verts = (trivertx_t *)((byte *)paliashdr + paliashdr->posedata);
	verts += posenum * paliashdr->poseverts;
	order = (int *)((byte *)paliashdr + paliashdr->commands);

	while (1)
	{
		nv1vert_t	cur;

		count = *order++;
		if (!count)
			break;

		if (count < 0)
		{
			count = -count;
			isfan = true;
		}
		else
			isfan = false;

		emitted = 0;

		while (count--)
		{
			float	s = ((float *)order)[0];
			float	t = ((float *)order)[1];

			order += 2;

		/*
		 * Alias vertices are byte packed and meaningless until scaled;
		 * glquake pushed scale and scale_origin into the modelview
		 * matrix and drew v[] raw.  We build our own transform, so the
		 * decompression happens here.
		 */
			local[0] = scale_origin[0] + verts->v[0] * scale[0];
			local[1] = scale_origin[1] + verts->v[1] * scale[1];
			local[2] = scale_origin[2] + verts->v[2] * scale[2];

			NV1_TransformPoint (local, cur.xyz);

			cur.s = s * tex->width;
			cur.t = t * tex->height;

			l = shadedots[verts->lightnormalindex];
			l = l * shadelight + ambientlight;

			cur.beta = (int)(l * NV1_BETAONE);
			if (cur.beta > NV1_BETAONE)
				cur.beta = NV1_BETAONE;
			else if (cur.beta < 0)
				cur.beta = 0;

			verts++;

			if (emitted == 0)
			{
				first = cur;
				emitted++;
				continue;
			}
			if (emitted == 1)
			{
				prev = cur;
				emitted++;
				continue;
			}

		/* we have three: emit one triangle */
			v[0] = first;
			v[1] = prev;
			v[2] = cur;

			NV1_DrawPolygon (v, 3, tex, true);

			if (isfan)
			{
			/* fan: keep the hub, advance the rim */
				prev = cur;
			}
			else
			{
			/* strip: slide the window along */
				first = prev;
				prev = cur;
			}

			emitted++;
		}
	}
}

/*
=================
R_SetupAliasFrame
=================
*/
void R_SetupAliasFrame (int frame, aliashdr_t *paliashdr, nv1texture_t *tex,
	vec3_t scale, vec3_t scale_origin)
{
	int	pose, numposes;
	float	interval;

	if ((frame >= paliashdr->numframes) || (frame < 0))
	{
		Con_DPrintf ("R_AliasSetupFrame: no such frame %d\n", frame);
		frame = 0;
	}

	pose = paliashdr->frames[frame].firstpose;
	numposes = paliashdr->frames[frame].numposes;

	if (numposes > 1)
	{
		interval = paliashdr->frames[frame].interval;
		pose += (int)(cl.time / interval) % numposes;
	}

	NV1_DrawAliasFrame (paliashdr, pose, tex, scale, scale_origin);
}

/*
=================
R_DrawAliasModel
=================
*/
void R_DrawAliasModel (entity_t *e)
{
	int		i;
	int		lnum;
	vec3_t		dist;
	float		add;
	model_t		*clmodel;
	vec3_t		mins, maxs;
	aliashdr_t	*paliashdr;
	float		an;
	int		anim;
	int		texnum;
	nv1texture_t	*tex;
	vec3_t		aliasscale, aliasorigin;

	clmodel = currententity->model;

	VectorAdd (currententity->origin, clmodel->mins, mins);
	VectorAdd (currententity->origin, clmodel->maxs, maxs);

	if (R_CullBox (mins, maxs))
		return;

	VectorCopy (currententity->origin, r_entorigin);
	VectorSubtract (r_origin, r_entorigin, modelorg);

/*
 * get lighting information
 */
	ambientlight = shadelight = R_LightPoint (currententity->origin);

/* always give the gun some light */
	if (e == &cl.viewent)
	{
		if (ambientlight < 24)
			ambientlight = shadelight = 24;
	}

	for (lnum = 0 ; lnum < MAX_DLIGHTS ; lnum++)
	{
		if (cl_dlights[lnum].die >= cl.time)
		{
			VectorSubtract (currententity->origin,
				cl_dlights[lnum].origin, dist);
			add = cl_dlights[lnum].radius - Length (dist);

			if (add > 0)
			{
				ambientlight += add;
				shadelight += add;
			}
		}
	}

/* clamp lighting so it doesn't overbright as much */
	if (ambientlight > 128)
		ambientlight = 128;
	if (ambientlight + shadelight > 192)
		shadelight = 192 - ambientlight;

/*
 * The eyes on the player model are drawn at double size in glquake because
 * they are too small to see otherwise.  Nothing changes here.
 */
	shadedots = r_avertexnormal_dots[((int)(e->angles[1] *
		(SHADEDOT_QUANT / 360.0))) & (SHADEDOT_QUANT - 1)];
	shadelight = shadelight / 200.0;
	ambientlight = ambientlight / 200.0;

	an = e->angles[1] / 180 * M_PI;
	shadevector[0] = cos (-an);
	shadevector[1] = sin (-an);
	shadevector[2] = 1;
	VectorNormalize (shadevector);

	paliashdr = (aliashdr_t *)Mod_Extradata (currententity->model);
	c_alias_polys += paliashdr->numtris;

/*
 * Decompression for the byte-packed vertices.  The eyes are drawn at double
 * size and lifted, exactly as glquake does, because at their real size they
 * are almost impossible to make out.
 */
	if (!strcmp (clmodel->name, "progs/eyes.mdl") && gl_doubleeyes.value)
	{
		VectorScale (paliashdr->scale, 2, aliasscale);
		VectorCopy (paliashdr->scale_origin, aliasorigin);
		aliasorigin[2] -= (22 + 8);
	}
	else
	{
		VectorCopy (paliashdr->scale, aliasscale);
		VectorCopy (paliashdr->scale_origin, aliasorigin);
	}

	NV1_PushEntityTransform (e);

	anim = (int)(cl.time * 10) & 3;
	texnum = paliashdr->gl_texturenum[currententity->skinnum][anim];

/* we can't dynamically colormap textures, so they are cached seperately for
   the players.  Heads are just uncolored. */
	if (currententity->colormap != vid.colormap && !gl_nocolors.value)
	{
		i = currententity - cl_entities;
		if (i >= 1 && i <= cl.maxclients)
			texnum = playertextures + i - 1;
	}

	if (texnum < 0 || texnum >= numnv1textures)
	{
		NV1_PopEntityTransform ();
		return;
	}
	tex = &nv1textures[texnum];

	R_SetupAliasFrame (currententity->frame, paliashdr, tex, aliasscale,
		aliasorigin);

	NV1_PopEntityTransform ();
}

/*
=============================================================================

  entity list

=============================================================================
*/

/*
=============================================================================

  entity ordering

  With no depth buffer, WHEN something is drawn is the only thing deciding
  whether it is in front.  glquake draws every entity after the whole world,
  which is fine when a z-buffer sorts it out afterwards and completely wrong
  here: a door behind a column is drawn after the column and paints straight
  over it.

  Quake's own software renderer did not do that -- it fed brush models into the
  same ordered traversal as the world.  Same idea here: each entity is filed
  under the leaf it sits in, and the back-to-front walk draws a leaf's entities
  as it reaches that leaf.  Anything whose leaf never got visited is drawn
  afterwards as a fallback, so nothing can silently vanish.

=============================================================================
*/

static int	*leafents;		/* per leaf, first entity index or -1 */
static int	leafents_count;		/* leaves the array is sized for */
static int	entnextinleaf[MAX_VISEDICTS];
static qboolean	entdrawn[MAX_VISEDICTS];
static int	entearly[MAX_VISEDICTS];	/* not in a visited leaf */
static int	numentearly;

/*
================
NV1_DrawOneEntity
================
*/
static void NV1_DrawOneEntity (int i)
{
	if (i < 0 || i >= cl_numvisedicts || entdrawn[i])
		return;

	entdrawn[i] = true;

	currententity = cl_visedicts[i];
	if (!currententity->model)
		return;

	switch (currententity->model->type)
	{
	case mod_alias:
		R_DrawAliasModel (currententity);
		break;

	case mod_brush:
		R_DrawBrushModel (currententity);
		break;

	case mod_sprite:
		R_DrawSpriteModel (currententity);
		break;

	default:
		break;
	}
}

/*
================
NV1_LinkEntitiesToLeaves

File every visible entity under a leaf.  Brush models are the reason this is
not simply Mod_PointInLeaf on the origin: a func_door or func_wall usually has
its origin at the world origin, nowhere near the geometry, so the centre of its
bounding box is used instead.
================
*/
void NV1_LinkEntitiesToLeaves (void)
{
	int		i;
	int		numleafs;
	mleaf_t		*leaf;
	vec3_t		centre;
	entity_t	*e;

	memset (entdrawn, 0, sizeof(entdrawn));
	numentearly = 0;

	if (!cl.worldmodel)
		return;

	numleafs = cl.worldmodel->numleafs + 1;

	if (numleafs > leafents_count)
	{
		if (leafents)
			free (leafents);
		leafents = (int *)malloc (numleafs * sizeof(int));
		if (!leafents)
		{
			leafents_count = 0;
			return;
		}
		leafents_count = numleafs;
	}

	for (i = 0 ; i < numleafs ; i++)
		leafents[i] = -1;

	for (i = 0 ; i < cl_numvisedicts ; i++)
	{
		entnextinleaf[i] = -1;

		e = cl_visedicts[i];
		if (!e->model)
			continue;

		if (e->model->type == mod_brush)
		{
			centre[0] = e->origin[0] +
				(e->model->mins[0] + e->model->maxs[0]) * 0.5f;
			centre[1] = e->origin[1] +
				(e->model->mins[1] + e->model->maxs[1]) * 0.5f;
			centre[2] = e->origin[2] +
				(e->model->mins[2] + e->model->maxs[2]) * 0.5f;
		}
		else
			VectorCopy (e->origin, centre);

		leaf = Mod_PointInLeaf (centre, cl.worldmodel);

	/*
	 * If the point lands somewhere the world walk will never reach -- solid
	 * space, or a leaf outside the PVS -- then filing it under that leaf
	 * would leave it to the end-of-frame fallback, which draws AFTER the
	 * whole world and therefore on top of it.  That is what put torches
	 * through floors and showed items sitting in sealed rooms.
	 *
	 * It happens easily: an entity spans several leaves, and the centre of
	 * a brush model's bounding box is frequently inside the geometry.
	 *
	 * Such entities are drawn FIRST instead, before any world surface, so
	 * anything in front of them paints over them.  Occluded things stay
	 * occluded, and nothing disappears.
	 */
		if (!leaf || leaf->contents == CONTENTS_SOLID ||
			leaf->visframe != r_visframecount)
		{
			entearly[numentearly++] = i;
			continue;
		}

		{
			int	n = leaf - cl.worldmodel->leafs;

			if (n < 0 || n >= numleafs)
			{
				entearly[numentearly++] = i;
				continue;
			}

			entnextinleaf[i] = leafents[n];
			leafents[n] = i;
		}
	}
}

/*
================
NV1_DrawEarlyEntities

The entities that have no place in the world traversal, drawn before it.
================
*/
void NV1_DrawEarlyEntities (void)
{
	int		i;
	vec3_t		saveorg;
	entity_t	*saveent;

	if (!numentearly || !r_drawentities.value)
		return;

	VectorCopy (modelorg, saveorg);
	saveent = currententity;

	for (i = 0 ; i < numentearly ; i++)
		NV1_DrawOneEntity (entearly[i]);

	VectorCopy (saveorg, modelorg);
	currententity = saveent;

	NV1_PopEntityTransform ();
}

/*
================
NV1_DrawLeafEntities

Called from the world walk as each leaf is reached.

The world traversal is mid-flight, and drawing an entity rewrites both
modelorg and currententity -- modelorg is what every remaining node plane test
in the walk is measured against, so both have to be put back.
================
*/
void NV1_DrawLeafEntities (int leafnum)
{
	int		i, next;
	vec3_t		saveorg;
	entity_t	*saveent;

	if (!leafents || leafnum < 0 || leafnum >= leafents_count)
		return;

	i = leafents[leafnum];
	if (i < 0)
		return;

	if (!r_drawentities.value)
		return;

	VectorCopy (modelorg, saveorg);
	saveent = currententity;

	while (i >= 0)
	{
		next = entnextinleaf[i];
		NV1_DrawOneEntity (i);
		i = next;
	}

	VectorCopy (saveorg, modelorg);
	currententity = saveent;

	NV1_PopEntityTransform ();
}

/*
=============
R_DrawEntitiesOnList

The fallback: anything the walk did not reach, drawn far to near so at least
the entities are ordered among themselves.
=============
*/
void R_DrawEntitiesOnList (void)
{
	int	i, j;
	float	dist[MAX_VISEDICTS];
	int	order[MAX_VISEDICTS];
	int	n;
	vec3_t	delta;

	if (!r_drawentities.value)
		return;

	n = 0;
	for (i = 0 ; i < cl_numvisedicts ; i++)
	{
		if (entdrawn[i])
			continue;

		VectorSubtract (cl_visedicts[i]->origin, r_origin, delta);
		dist[n] = DotProduct (delta, delta);
		order[n] = i;
		n++;
	}

	if (!n)
		return;

/* insertion sort, far first; the list is short and usually nearly sorted */
	for (i = 1 ; i < n ; i++)
	{
		int	o = order[i];
		float	d = dist[i];

		for (j = i - 1 ; j >= 0 && dist[j] < d ; j--)
		{
			dist[j + 1] = dist[j];
			order[j + 1] = order[j];
		}
		dist[j + 1] = d;
		order[j + 1] = o;
	}

	for (i = 0 ; i < n ; i++)
		NV1_DrawOneEntity (order[i]);

	NV1_PopEntityTransform ();
}

/*
=============
R_DrawViewModel
=============
*/
void R_DrawViewModel (void)
{
	float		ambient[4], diffuse[4];
	int		j;
	int		lnum;
	vec3_t		dist;
	float		add;
	dlight_t	*dl;
	int		ambientlight, shadelight;

	if (!r_drawviewmodel.value)
		return;

	if (chase_active.value)
		return;

	if (envmap)
		return;

	if (!r_drawentities.value)
		return;

	if (cl.items & IT_INVISIBILITY)
		return;

	if (cl.stats[STAT_HEALTH] <= 0)
		return;

	currententity = &cl.viewent;
	if (!currententity->model)
		return;

	j = R_LightPoint (currententity->origin);

	if (j < 24)
		j = 24;		/* always give some light on gun */
	ambientlight = j;
	shadelight = j;

/* add dynamic lights */
	for (lnum = 0 ; lnum < MAX_DLIGHTS ; lnum++)
	{
		dl = &cl_dlights[lnum];
		if (!dl->radius)
			continue;
		if (!dl->radius)
			continue;
		if (dl->die < cl.time)
			continue;

		VectorSubtract (currententity->origin, dl->origin, dist);
		add = dl->radius - Length (dist);
		if (add > 0)
			ambientlight += add;
	}

	(void)ambient; (void)diffuse;

/*
 * glquake squashes the depth range here so the weapon cannot poke into walls.
 * With no depth buffer there is nothing to squash: the view model is simply
 * drawn last, which is the same effect by other means.
 */
	R_DrawAliasModel (currententity);
}

/*
============
R_PolyBlend
============
*/
void R_PolyBlend (void)
{
	NvuRect16	rect;
	int		r, g, b;
	U016		color;

	if (!gl_polyblend.value)
		return;
	if (!v_blend[3])
		return;

	r = (int)(v_blend[0] * 31.0);
	g = (int)(v_blend[1] * 31.0);
	b = (int)(v_blend[2] * 31.0);

	if (r > 31) r = 31;	if (r < 0) r = 0;
	if (g > 31) g = 31;	if (g < 0) g = 0;
	if (b > 31) b = 31;	if (b < 0) b = 0;

	color = (U016)((r << 10) | (g << 5) | b);

	rect.x = (short)glx;
	rect.y = (short)gly;
	rect.w = (unsigned short)glwidth;
	rect.h = (unsigned short)glheight;

/*
 * On a paletted display this would have been a palette shift.  In 555 it is a
 * whole-screen beta blend, which is one pass over the view rectangle.
 */
	NVLIB_SetBetaClamp ((unsigned short)(v_blend[3] * 65535.0));
	NVLIB_DrawRectangleA (color, &rect);
	NVLIB_SetBetaClamp (65535);
}

/*
=============================================================================

  frame setup

=============================================================================
*/

int SignbitsForPlane (mplane_t *out)
{
	int	bits, j;

/* for fast box on planeside test */
	bits = 0;
	for (j = 0 ; j < 3 ; j++)
	{
		if (out->normal[j] < 0)
			bits |= 1 << j;
	}
	return bits;
}

void R_SetFrustum (void)
{
	int	i;

	if (r_refdef.fov_x == 90)
	{
	/* front side is visible */
		VectorAdd (vpn, vright, frustum[0].normal);
		VectorSubtract (vpn, vright, frustum[1].normal);

		VectorAdd (vpn, vup, frustum[2].normal);
		VectorSubtract (vpn, vup, frustum[3].normal);
	}
	else
	{
	/* rotate VPN right by FOV_X/2 degrees */
		RotatePointAroundVector (frustum[0].normal, vup, vpn,
			-(90 - r_refdef.fov_x / 2));
	/* rotate VPN left by FOV_X/2 degrees */
		RotatePointAroundVector (frustum[1].normal, vup, vpn,
			90 - r_refdef.fov_x / 2);
	/* rotate VPN up by FOV_X/2 degrees */
		RotatePointAroundVector (frustum[2].normal, vright, vpn,
			90 - r_refdef.fov_y / 2);
	/* rotate VPN down by FOV_X/2 degrees */
		RotatePointAroundVector (frustum[3].normal, vright, vpn,
			-(90 - r_refdef.fov_y / 2));
	}

	for (i = 0 ; i < 4 ; i++)
	{
		frustum[i].type = PLANE_ANYZ;
		frustum[i].dist = DotProduct (r_origin, frustum[i].normal);
		frustum[i].signbits = SignbitsForPlane (&frustum[i]);
	}
}

/*
===============
R_SetupFrame
===============
*/
void R_SetupFrame (void)
{
	int	edgecount;
	vrect_t	vrect;
	float	w, h;

/* don't allow cheats in multiplayer */
	if (cl.maxclients > 1)
		Cvar_Set ("r_fullbright", "0");

	R_AnimateLight ();

	r_framecount++;

/* build the transformation matrix for the given view angles */
	VectorCopy (r_refdef.vieworg, r_origin);

	AngleVectors (r_refdef.viewangles, vpn, vright, vup);

/* current viewleaf */
	r_oldviewleaf = r_viewleaf;
	r_viewleaf = Mod_PointInLeaf (r_origin, cl.worldmodel);

	V_SetContentsColor (r_viewleaf->contents);
	V_CalcBlend ();

	r_cache_thrash = false;

	c_brush_polys = 0;
	c_alias_polys = 0;
	nv1_c_patches = 0;
	nv1_c_texels = 0;

	(void)edgecount; (void)vrect; (void)w; (void)h;
}

/*
=============
R_SetupView

Establish the projection and the clip rectangle for the 3D view.
=============
*/
static void R_SetupView (void)
{
	NvuRect16	rect;

	NV1_SetupTransform ();

/*
 * The chip clips everything it draws to this rectangle, which is how a sized
 * down view stays inside its border without us testing anything per patch.
 */
	rect.x = (short)glx;
	rect.y = (short)gly;
	rect.w = (unsigned short)glwidth;
	rect.h = (unsigned short)glheight;
	NVLIB_SetClip (rect);
}

/*
================
R_RenderScene
================
*/
void R_RenderScene (void)
{
	R_SetupFrame ();

	R_SetFrustum ();

	R_SetupView ();

	R_MarkLeaves ();	/* done here so we know if we're in water */

	R_DrawWorld ();		/* adds static entities to the list */

	S_ExtraUpdate ();	/* don't let sound get messed up if going slow */

	R_DrawEntitiesOnList ();

	R_DrawWaterSurfaces ();
}

/*
=============
R_Clear
=============
*/
void R_Clear (void)
{
	NvuRect16	rect;

/*
 * With no depth buffer there is nothing that must be cleared for correctness,
 * only for appearance.  Every visible pixel is covered by the world, so this
 * is off by default; turn nv1_clear on to see holes in the level.
 */
	if (!nv1_clear.value)
		return;

	rect.x = (short)glx;
	rect.y = (short)gly;
	rect.w = (unsigned short)glwidth;
	rect.h = (unsigned short)glheight;

/* nv1_clear 2 paints magenta, so anything the world failed to cover is
   obvious rather than looking like a dark room */
	if (nv1_clear.value >= 2)
		NVLIB_DrawBkGnd (0x7c1f, &rect);
	else
		NVLIB_DrawBkGnd (0, &rect);
}

/*
================
R_RenderView

r_refdef must be set before the first call
================
*/
void R_RenderView (void)
{
	double	time1 = 0, time2;

	if (r_norefresh.value)
		return;

	if (!r_worldentity.model || !cl.worldmodel)
		Sys_Error ("R_RenderView: NULL worldmodel");

	if (r_speeds.value)
	{
		time1 = Sys_FloatTime ();
		c_brush_polys = 0;
		c_alias_polys = 0;
	}

	mirror = false;

	R_Clear ();

	R_RenderScene ();
	R_DrawViewModel ();

	R_PolyBlend ();

	NVLIB_SetClip (NVLIB_CLIP_OFF);

	if (r_speeds.value)
	{
		time2 = Sys_FloatTime ();
		Con_Printf ("%3i ms  %4i wpoly %4i epoly  %5i patches %7i texels\n",
			(int)((time2 - time1) * 1000), c_brush_polys, c_alias_polys,
			nv1_c_patches, nv1_c_texels);
	}
	else if (nv1_showtexels.value)
	{
		Con_Printf ("%5i patches %7i texels\n", nv1_c_patches, nv1_c_texels);
	}
}
