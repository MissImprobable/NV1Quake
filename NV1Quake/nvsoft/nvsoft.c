/*
nvsoft.c -- software NV1.

Implements, in software, the subset of NVLIB 1.50 that nv1Quake uses, with the
real signatures from <nvlib.h>.  The point is not to be a pretty renderer: it
is to reproduce what an NV1 / STG2000 actually does, so that the renderer we
write against it is a renderer that will still be correct when it is pointed at
nvlib.lib and a real Diamond Edge 3D.

What that means concretely, all of it established by reading the SDK:

  - There is no depth buffer.  None.  Primitives land in submission order.
  - The native primitive is a quadratic patch with NINE integer screen-space
    control points, laid out
                        p0  p3  p6      (row0 = top edge 0-6)
                        p1  p4  p7
                        p2  p5  p8      (row2 = bottom edge 2-8)
    with p1 the midpoint of the LEFT edge 0-2, p3 the midpoint of the TOP edge
    0-6, p5 of the BOTTOM edge 2-8, p7 of the RIGHT edge 6-8, p4 the centre.
  - Each edge is an INTERPOLATING quadratic, not a Bezier: the middle control
    point lies on the curve.  Verified against SBQTM.C:NVMath_CalcQcurve, which
    builds P(t) = a.t^2 + b.t + c with P(0)=start, P(0.5)=middle, P(1)=end.
  - SubdivideIn packs HEIGHT_02_68 in bits 0-3 and WIDTH_06_28 in bits 4-7,
    each a power 2..8.  HEIGHT is the number of texel strips, running along the
    left/right edges; WIDTH is the number of texels in a strip, running along
    the top/bottom edges.
  - The texture is not sampled by the chip.  The CPU streams 2^HEIGHT * 2^WIDTH
    texels at it, two 16-bit texels per 32-bit write.  So the cost of a
    primitive is its texel count and has nothing to do with how big it lands on
    screen.  Everything about making NV1 fast is choosing that texel count.

Speed notes, since the whole point is that this has to run:
  - Surface points are advanced by second-order forward differences.  A
    quadratic needs two adds per step, never a polynomial evaluation.
  - All rasterisation is 16.16 fixed point.
  - Texel quads are filled with incremental edge functions, and the very common
    one- and few-pixel cases are short-circuited before any of that.
  - Solid runs are written as paired 32-bit stores, which is also exactly the
    format the real chip wants its texels in.
*/

#include <windows.h>
#include <string.h>
#include <stdlib.h>

#include <nv32.h>
#include <nvutypes.h>
#include <nvlib.h>
#include <nvvidmod.h>

#include "nvsoft.h"

/*
=============================================================================

  fixed point

=============================================================================
*/

#define FIXSHIFT	16
#define FIXONE		(1 << FIXSHIFT)
#define FLOAT_TO_FIX(f)	((long)((f) * (float)FIXONE))

/* Screen coordinates are clamped into this box before rasterising.  Control
   points arrive as S016 and a badly projected vertex can be enormous; keeping
   them inside +/-8192 guarantees the 16.16 intermediates cannot overflow. */
#define COORD_LIMIT	8192

#define CLAMP_COORD(v) \
	((v) < -COORD_LIMIT ? -COORD_LIMIT : ((v) > COORD_LIMIT ? COORD_LIMIT : (v)))

/*
=============================================================================

  emulated device state

=============================================================================
*/

#define MAX_FB_WIDTH	1280
#define MAX_FB_HEIGHT	1024

const NvuRect16	NVLIB_CLIP_OFF = { 0, 0, 0, 0 };

float	nvldiv[101][401];

U032	dbpipe_hidden_buffer;
BOOL	dbDisable;

static U016		*nvs_fb[2];
static int		nvs_width;
static int		nvs_height;
static int		nvs_hidden;		/* buffer being drawn into */
static int		nvs_doublebuffered;

static int		nvs_clipx0, nvs_clipy0;	/* inclusive */
static int		nvs_clipx1, nvs_clipy1;	/* exclusive */

static NVLIB_ColorFormat nvs_format = RGB555;

static long		nvs_patchcount;
static long		nvs_texelcount;

/* One resampling scratch grid.  Biggest legal patch is 256x256 texels. */
static U016		nvs_grid[256 * 256];

static HWND		nvs_hwnd;
static BITMAPINFO	*nvs_bmi;

static void NVS_ResetClip (void)
{
	nvs_clipx0 = 0;
	nvs_clipy0 = 0;
	nvs_clipx1 = nvs_width;
	nvs_clipy1 = nvs_height;
}

/*
=============================================================================

  the patch rasteriser

  This is the whole emulator.  Given nine control points, a subdivision, and a
  grid of texels, walk the patch in strips and stamp each texel down as a small
  quadrilateral.  That is what the hardware does; the visible consequences --
  the wobble on large patches, the seams that NV_QTM_GAP_FILLING exists to
  paper over -- fall out of it rather than being faked.

=============================================================================
*/

typedef struct
{
	long	x, y;			/* 16.16 */
} fixpt_t;

/* Second-order forward difference walker for an interpolating quadratic
   through three points at t = 0, 0.5, 1. */
typedef struct
{
	long	x, y;			/* current value, 16.16 */
	long	dx, dy;			/* first difference, 16.16 */
	long	ddx, ddy;		/* second difference, constant, 16.16 */
} qwalk_t;

/*
==============
NVS_QWalkInit

P(t) = A.t^2 + B.t + C through q0 at t=0, q1 at t=0.5, q2 at t=1:
	A =  2q0 - 4q1 + 2q2
	B = -3q0 + 4q1 -  q2
	C =   q0
Stepping by h = 1/n:
	d1 = A.h^2 + B.h		d2 = 2A.h^2
Two adds per step from there on.
==============
*/
static void NVS_QWalkInit (qwalk_t *w, float q0x, float q0y, float q1x,
	float q1y, float q2x, float q2y, int n)
{
	float	ax, ay, bx, by;
	float	h, hh;

	ax = 2.0f * q0x - 4.0f * q1x + 2.0f * q2x;
	ay = 2.0f * q0y - 4.0f * q1y + 2.0f * q2y;
	bx = -3.0f * q0x + 4.0f * q1x - q2x;
	by = -3.0f * q0y + 4.0f * q1y - q2y;

	h = 1.0f / (float)n;
	hh = h * h;

	w->x = FLOAT_TO_FIX(q0x);
	w->y = FLOAT_TO_FIX(q0y);
	w->dx = FLOAT_TO_FIX(ax * hh + bx * h);
	w->dy = FLOAT_TO_FIX(ay * hh + by * h);
	w->ddx = FLOAT_TO_FIX(2.0f * ax * hh);
	w->ddy = FLOAT_TO_FIX(2.0f * ay * hh);
}

#define QWALK_STEP(w) \
	{ (w).x += (w).dx; (w).y += (w).dy; (w).dx += (w).ddx; (w).dy += (w).ddy; }

/*
==============
NVS_FillTexelQuad

Stamp one texel over the quadrilateral a-b-c-d (screen order, either winding)
with a flat colour.  Small-quad cases are peeled off before the general path
because at a sane subdivision almost every texel quad is one or two pixels.
==============
*/
static void NVS_FillTexelQuad (const fixpt_t *a, const fixpt_t *b,
	const fixpt_t *c, const fixpt_t *d, U016 color)
{
	long	minx, miny, maxx, maxy;
	int	ix0, iy0, ix1, iy1;
	int	x, y;
	long	e0, e1, e2, e3;
	long	d0x, d0y, d1x, d1y, d2x, d2y, d3x, d3y;
	long	px, py;
	U016	*dest;
	int	area;

	minx = a->x; maxx = a->x;
	if (b->x < minx) minx = b->x; else if (b->x > maxx) maxx = b->x;
	if (c->x < minx) minx = c->x; else if (c->x > maxx) maxx = c->x;
	if (d->x < minx) minx = d->x; else if (d->x > maxx) maxx = d->x;

	miny = a->y; maxy = a->y;
	if (b->y < miny) miny = b->y; else if (b->y > maxy) maxy = b->y;
	if (c->y < miny) miny = c->y; else if (c->y > maxy) maxy = c->y;
	if (d->y < miny) miny = d->y; else if (d->y > maxy) maxy = d->y;

/*
 * Coverage rule: every pixel the quad touches, not every pixel whose centre
 * falls strictly inside it.
 *
 * The strict rule is the "correct" one and it is what leaves the texture full
 * of pinholes: forward mapping a grid of texels puts quad boundaries at
 * arbitrary sub-pixel positions, and a pixel centre landing in the sliver
 * between two quads is claimed by neither.  Rounding outward makes adjacent
 * quads overlap by at most a pixel instead, and the later texel simply
 * overwrites the earlier one -- which, since adjacent texels are adjacent in
 * the source texture, is nearly the same colour anyway.
 *
 * This is the software equivalent of NV_QTM_GAP_FILLING, which exists on the
 * chip for exactly this reason.
 */
	ix0 = (int)(minx >> FIXSHIFT);
	iy0 = (int)(miny >> FIXSHIFT);
	ix1 = (int)(maxx >> FIXSHIFT) + 1;
	iy1 = (int)(maxy >> FIXSHIFT) + 1;

	if (ix0 < nvs_clipx0) ix0 = nvs_clipx0;
	if (iy0 < nvs_clipy0) iy0 = nvs_clipy0;
	if (ix1 > nvs_clipx1) ix1 = nvs_clipx1;
	if (iy1 > nvs_clipy1) iy1 = nvs_clipy1;

	if (ix0 >= ix1 || iy0 >= iy1)
		return;

	area = (ix1 - ix0) * (iy1 - iy0);

/* The overwhelmingly common case at a well chosen subdivision. */
	if (area == 1)
	{
		nvs_fb[nvs_hidden][iy0 * nvs_width + ix0] = color;
		return;
	}

/*
 * Small enough that the bounding box IS the shape.  A texel quad only a few
 * pixels across is not meaningfully a quadrilateral once it has been sampled,
 * and testing four edges to decide two or three pixels costs more than the
 * pixels are worth.
 */
	if (area <= 64)
	{
		dest = nvs_fb[nvs_hidden] + iy0 * nvs_width;
		for (y = iy0 ; y < iy1 ; y++)
		{
			for (x = ix0 ; x < ix1 ; x++)
				dest[x] = color;
			dest += nvs_width;
		}
		return;
	}

/* General case: four incremental edge functions.  Edge (p,q) evaluated at r is
   (q.x-p.x)(r.y-p.y) - (q.y-p.y)(r.x-p.x); shifting the operands down by 8
   keeps the products inside 32 bits over our clamped coordinate range. */
	px = ((long)ix0 << FIXSHIFT) + (FIXONE / 2);
	py = ((long)iy0 << FIXSHIFT) + (FIXONE / 2);

#define EDGE_SETUP(ax0, ay0, ax1, ay1, ex, ey, ev)			\
	{								\
		long ux = ((ax1) - (ax0)) >> 8;				\
		long uy = ((ay1) - (ay0)) >> 8;				\
		(ex) = -uy;						\
		(ey) = ux;						\
		(ev) = ux * ((py - (ay0)) >> 8) - uy * ((px - (ax0)) >> 8); \
	}

	EDGE_SETUP(a->x, a->y, b->x, b->y, d0x, d0y, e0)
	EDGE_SETUP(b->x, b->y, c->x, c->y, d1x, d1y, e1)
	EDGE_SETUP(c->x, c->y, d->x, d->y, d2x, d2y, e2)
	EDGE_SETUP(d->x, d->y, a->x, a->y, d3x, d3y, e3)

#undef EDGE_SETUP

/* Step per whole pixel, in the same shifted units. */
	d0x *= (FIXONE >> 8); d0y *= (FIXONE >> 8);
	d1x *= (FIXONE >> 8); d1y *= (FIXONE >> 8);
	d2x *= (FIXONE >> 8); d2y *= (FIXONE >> 8);
	d3x *= (FIXONE >> 8); d3y *= (FIXONE >> 8);

	dest = nvs_fb[nvs_hidden] + iy0 * nvs_width;

	for (y = iy0 ; y < iy1 ; y++)
	{
		long	r0 = e0, r1 = e1, r2 = e2, r3 = e3;

		for (x = ix0 ; x < ix1 ; x++)
		{
		/* Accept either winding: inside means all four agree in sign. */
			if (((r0 | r1 | r2 | r3) >= 0) ||
			    ((r0 <= 0) && (r1 <= 0) && (r2 <= 0) && (r3 <= 0)))
				dest[x] = color;

			r0 += d0x; r1 += d1x; r2 += d2x; r3 += d3x;
		}

		e0 += d0y; e1 += d1y; e2 += d2y; e3 += d3y;
		dest += nvs_width;
	}
}

/*
==============
NVS_RenderPatch

The hardware entry point.  cp[9] are the control points in NV order, texels is
a 2^hpow by 2^wpow grid in strip-major order, exactly as it would have been
streamed into renderTextureQuadratic.Color[0].

betas, if non-NULL, are the four corner betas (at p0, p2, p6, p8) in 0..255 and
are interpolated bilinearly over the patch, which is what the BETA variants of
the class do.
==============
*/
static void NVS_RenderPatch (const Nvu1Pt16 *cp, int hpow, int wpow,
	const U016 *texels, const U016 *betas, int keyed)
{
	int		nmaj, nmin;
	int		j, i;
	float		g[3][3][2];		/* [s][t][xy] */
	qwalk_t		sw[3];			/* the three t-columns, walked in s */
	qwalk_t		tw;
	fixpt_t		*rowa, *rowb, *tmp;
	static fixpt_t	rowbuf0[257], rowbuf1[257];
	const U016	*src;
	int		b0, b1, b2, b3;

	if (hpow < 2 || hpow > 8 || wpow < 2 || wpow > 8)
		return;
	if (!texels || !nvs_fb[nvs_hidden])
		return;

	nmaj = 1 << hpow;
	nmin = 1 << wpow;

/* Unpack the nine control points into the [s][t] grid described at the top of
   this file, clamping so the fixed point maths cannot run away. */
	g[0][0][0] = (float)CLAMP_COORD(cp[0].x); g[0][0][1] = (float)CLAMP_COORD(cp[0].y);
	g[0][1][0] = (float)CLAMP_COORD(cp[3].x); g[0][1][1] = (float)CLAMP_COORD(cp[3].y);
	g[0][2][0] = (float)CLAMP_COORD(cp[6].x); g[0][2][1] = (float)CLAMP_COORD(cp[6].y);
	g[1][0][0] = (float)CLAMP_COORD(cp[1].x); g[1][0][1] = (float)CLAMP_COORD(cp[1].y);
	g[1][1][0] = (float)CLAMP_COORD(cp[4].x); g[1][1][1] = (float)CLAMP_COORD(cp[4].y);
	g[1][2][0] = (float)CLAMP_COORD(cp[7].x); g[1][2][1] = (float)CLAMP_COORD(cp[7].y);
	g[2][0][0] = (float)CLAMP_COORD(cp[2].x); g[2][0][1] = (float)CLAMP_COORD(cp[2].y);
	g[2][1][0] = (float)CLAMP_COORD(cp[5].x); g[2][1][1] = (float)CLAMP_COORD(cp[5].y);
	g[2][2][0] = (float)CLAMP_COORD(cp[8].x); g[2][2][1] = (float)CLAMP_COORD(cp[8].y);

/* Walk the three t-columns in s.  Each gives us, at every strip boundary, one
   of the three control points of that boundary's curve in t. */
	NVS_QWalkInit (&sw[0], g[0][0][0], g[0][0][1], g[1][0][0], g[1][0][1],
		g[2][0][0], g[2][0][1], nmaj);
	NVS_QWalkInit (&sw[1], g[0][1][0], g[0][1][1], g[1][1][0], g[1][1][1],
		g[2][1][0], g[2][1][1], nmaj);
	NVS_QWalkInit (&sw[2], g[0][2][0], g[0][2][1], g[1][2][0], g[1][2][1],
		g[2][2][0], g[2][2][1], nmaj);

	rowa = rowbuf0;
	rowb = rowbuf1;

/* Boundary of strip 0. */
	NVS_QWalkInit (&tw,
		(float)sw[0].x / (float)FIXONE, (float)sw[0].y / (float)FIXONE,
		(float)sw[1].x / (float)FIXONE, (float)sw[1].y / (float)FIXONE,
		(float)sw[2].x / (float)FIXONE, (float)sw[2].y / (float)FIXONE, nmin);
	for (i = 0 ; i <= nmin ; i++)
	{
		rowa[i].x = tw.x;
		rowa[i].y = tw.y;
		QWALK_STEP(tw)
	}

	src = texels;

/* Betas arrive as S1.15, which is what ControlBetaOut wants.  Drop them to
   0..255 once here rather than per texel. */
	b0 = b1 = b2 = b3 = 255;
	if (betas)
	{
		b0 = betas[0] >> 7; b1 = betas[1] >> 7;
		b2 = betas[2] >> 7; b3 = betas[3] >> 7;
		if (b0 > 255) b0 = 255;
		if (b1 > 255) b1 = 255;
		if (b2 > 255) b2 = 255;
		if (b3 > 255) b3 = 255;
	}

	for (j = 0 ; j < nmaj ; j++)
	{
		int	betaleft = 255, betaright = 255;

		QWALK_STEP(sw[0])
		QWALK_STEP(sw[1])
		QWALK_STEP(sw[2])

		NVS_QWalkInit (&tw,
			(float)sw[0].x / (float)FIXONE, (float)sw[0].y / (float)FIXONE,
			(float)sw[1].x / (float)FIXONE, (float)sw[1].y / (float)FIXONE,
			(float)sw[2].x / (float)FIXONE, (float)sw[2].y / (float)FIXONE,
			nmin);
		for (i = 0 ; i <= nmin ; i++)
		{
			rowb[i].x = tw.x;
			rowb[i].y = tw.y;
			QWALK_STEP(tw)
		}

		if (betas)
		{
		/* beta down the left edge (p0->p2) and the right edge (p6->p8),
		   sampled at the centre of this strip */
			int	f = ((j << 8) + 128) >> hpow;

			betaleft = b0 + (((b1 - b0) * f) >> 8);
			betaright = b2 + (((b3 - b2) * f) >> 8);
		}

		for (i = 0 ; i < nmin ; i++)
		{
			U016	texel = *src++;

		/*
		 * ARGB1555: bit 15 is the alpha bit.  A clear bit means the
		 * texel is not drawn at all, which is how the chip does the
		 * cut-out transparency that sprites and fence textures need.
		 */
			if (keyed)
			{
				if (!(texel & 0x8000))
					continue;
				texel &= 0x7fff;
			}

			if (betas)
			{
				int	f = ((i << 8) + 128) >> wpow;
				int	beta = betaleft +
					(((betaright - betaleft) * f) >> 8);
				int	r, gg, bb;

			/* RGB555 modulate */
				r = (((texel >> 10) & 0x1f) * beta) >> 8;
				gg = (((texel >> 5) & 0x1f) * beta) >> 8;
				bb = ((texel & 0x1f) * beta) >> 8;
				texel = (U016)((r << 10) | (gg << 5) | bb);
			}

			NVS_FillTexelQuad (&rowa[i], &rowa[i + 1],
				&rowb[i + 1], &rowb[i], texel);
		}

		tmp = rowa; rowa = rowb; rowb = tmp;
	}

	nvs_patchcount++;
	nvs_texelcount += nmaj * nmin;
}

/*
=============================================================================

  video mode / presentation

=============================================================================
*/

static void NVS_AllocFrameBuffers (int width, int height)
{
	int	i;

	for (i = 0 ; i < 2 ; i++)
	{
		if (nvs_fb[i])
			free (nvs_fb[i]);
		nvs_fb[i] = (U016 *)malloc (width * height * sizeof(U016));
		if (nvs_fb[i])
			memset (nvs_fb[i], 0, width * height * sizeof(U016));
	}

	nvs_width = width;
	nvs_height = height;
	nvs_hidden = 1;
	NVS_ResetClip ();

	if (!nvs_bmi)
		nvs_bmi = (BITMAPINFO *)malloc (sizeof(BITMAPINFOHEADER));

	if (nvs_bmi)
	{
	/* 5-5-5 is what BI_RGB means at 16bpp, so no colour masks are needed
	   and the DIB matches the emulated framebuffer byte for byte. */
		memset (nvs_bmi, 0, sizeof(BITMAPINFOHEADER));
		nvs_bmi->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		nvs_bmi->bmiHeader.biWidth = width;
		nvs_bmi->bmiHeader.biHeight = -height;	/* top down */
		nvs_bmi->bmiHeader.biPlanes = 1;
		nvs_bmi->bmiHeader.biBitCount = 16;
		nvs_bmi->bmiHeader.biCompression = BI_RGB;
	}
}

int __stdcall NvRequestVideoMode (void *hInst, int buffering, int resolution,
	int depth)
{
	int	width, height;

	(void)hInst;

	switch (resolution)
	{
	case NV_VIDMODE_640x400:	width = 640; height = 400; break;
	case NV_VIDMODE_640x480:	width = 640; height = 480; break;
	case NV_VIDMODE_800x600:	width = 800; height = 600; break;
	case NV_VIDMODE_1024x768:	width = 1024; height = 768; break;
	case NV_VIDMODE_1152x864:	width = 1152; height = 864; break;
	case NV_VIDMODE_1280x1024:	width = 1280; height = 1024; break;
	default:			width = 640; height = 400; break;
	}

	if (depth != NV_VIDMODE_HIGH_COLOR)
		return 0;		/* the emulator is 16bpp, like the card */

	if (width > MAX_FB_WIDTH || height > MAX_FB_HEIGHT)
		return 0;

	NVS_AllocFrameBuffers (width, height);
	nvs_doublebuffered = (buffering == NV_VIDMODE_DOUBLE_BUFFER);

	return 1;
}

int __stdcall NvGetVideoMode (void *hInst, int *buffering, int *resolution,
	int *depth)
{
	(void)hInst;

	if (buffering)
		*buffering = nvs_doublebuffered ?
			NV_VIDMODE_DOUBLE_BUFFER : NV_VIDMODE_SINGLE_BUFFER;
	if (resolution)
		*resolution = NV_VIDMODE_DEFAULT;
	if (depth)
		*depth = NV_VIDMODE_HIGH_COLOR;

	return 1;
}

void __stdcall NvRestoreVideoMode (void)
{
	int	i;

	for (i = 0 ; i < 2 ; i++)
	{
		if (nvs_fb[i])
		{
			free (nvs_fb[i]);
			nvs_fb[i] = NULL;
		}
	}
	if (nvs_bmi)
	{
		free (nvs_bmi);
		nvs_bmi = NULL;
	}
}

void NVSOFT_SetWindow (void *hWnd)
{
	nvs_hwnd = (HWND)hWnd;
}

int NVSOFT_GetWidth (void)
{
	return nvs_width;
}

int NVSOFT_GetHeight (void)
{
	return nvs_height;
}

void NVSOFT_ResetStats (void)
{
	nvs_patchcount = 0;
	nvs_texelcount = 0;
}

long NVSOFT_GetPatchCount (void)
{
	return nvs_patchcount;
}

long NVSOFT_GetTexelCount (void)
{
	return nvs_texelcount;
}

void NVSOFT_Present (void)
{
	HDC	hdc;
	int	visible;

	if (!nvs_hwnd || !nvs_bmi)
		return;

	visible = nvs_hidden;
	if (!nvs_fb[visible])
		return;

	hdc = GetDC (nvs_hwnd);
	if (!hdc)
		return;

	SetDIBitsToDevice (hdc, 0, 0, nvs_width, nvs_height, 0, 0, 0,
		nvs_height, nvs_fb[visible], nvs_bmi, DIB_RGB_COLORS);

	ReleaseDC (nvs_hwnd, hdc);
}

/*
=============================================================================

  NVLIB entry points

=============================================================================
*/

int NVLIB_InitDBPipe (int canvas)
{
	(void)canvas;

	if (!nvs_fb[0])
		NVS_AllocFrameBuffers (640, 400);

	nvs_hidden = 1;
	dbpipe_hidden_buffer = 1;
	return 1;
}

BOOL NVLIB_FlipDBPipe (BOOL wait)
{
	(void)wait;

	NVSOFT_Present ();

	if (nvs_doublebuffered && !dbDisable)
	{
		nvs_hidden ^= 1;
		dbpipe_hidden_buffer = nvs_hidden;
	}

	return TRUE;
}

void NVLIB_DisableDB (BOOL disable)
{
	dbDisable = disable;
}

BOOL NVLIB_SetDBPipeDrawBuffer (U032 buffer)
{
	if (buffer > 1)
		return FALSE;
	nvs_hidden = (int)buffer;
	dbpipe_hidden_buffer = buffer;
	return TRUE;
}

void NVLIB_DestroyDBPipe (void)
{
}

void NVLIB_SetClip (NvuRect16 rect)
{
	if (rect.w == 0 && rect.h == 0)
	{
		NVS_ResetClip ();
		return;
	}

	nvs_clipx0 = rect.x;
	nvs_clipy0 = rect.y;
	nvs_clipx1 = rect.x + rect.w;
	nvs_clipy1 = rect.y + rect.h;

	if (nvs_clipx0 < 0) nvs_clipx0 = 0;
	if (nvs_clipy0 < 0) nvs_clipy0 = 0;
	if (nvs_clipx1 > nvs_width) nvs_clipx1 = nvs_width;
	if (nvs_clipy1 > nvs_height) nvs_clipy1 = nvs_height;
}

void NVLIB_InitDivLUT (void)
{
	int	i, j;

	for (i = 0 ; i <= 100 ; i++)
		for (j = 1 ; j <= 400 ; j++)
			nvldiv[i][j] = (float)i / (float)j;

	for (j = 0 ; j <= 400 ; j++)
		nvldiv[0][j] = 0.5f;
}

/*
  Solid fills.  The card has separate objects for these; here they are all one
  span filler, written 32 bits at a time because that is the width of the write
  the real chip wants anyway.
*/
static void NVS_SolidRect (int x0, int y0, int x1, int y1, U016 color)
{
	U016	*dest;
	int	y;
	unsigned long pair;

	if (!nvs_fb[nvs_hidden])
		return;

	if (x0 < nvs_clipx0) x0 = nvs_clipx0;
	if (y0 < nvs_clipy0) y0 = nvs_clipy0;
	if (x1 > nvs_clipx1) x1 = nvs_clipx1;
	if (y1 > nvs_clipy1) y1 = nvs_clipy1;

	if (x0 >= x1 || y0 >= y1)
		return;

	pair = ((unsigned long)color << 16) | color;
	dest = nvs_fb[nvs_hidden] + y0 * nvs_width;

	for (y = y0 ; y < y1 ; y++)
	{
		int		x = x0;
		unsigned long	*d32;
		int		n;

		if (x & 1)
			dest[x++] = color;

		n = (x1 - x) >> 1;
		d32 = (unsigned long *)(dest + x);

		while (n >= 4)
		{
			d32[0] = pair; d32[1] = pair;
			d32[2] = pair; d32[3] = pair;
			d32 += 4;
			n -= 4;
		}
		while (n--)
			*d32++ = pair;

		x += (x1 - x) & ~1;

		while (x < x1)
			dest[x++] = color;

		dest += nvs_width;
	}
}

/* NVLIB takes colours as packed 32-bit values in the current format.  In the
   16-bit formats the low half is the colour. */
#define COLOR16(c)	((U016)((c) & 0xffff))

BOOL NVLIB_InitBkGnd (NVLIB_ColorFormat color_format)
{
	nvs_format = color_format;
	return TRUE;
}

void NVLIB_DrawBkGnd (U032 Color, NvuRect16 *rect)
{
	if (!rect || (rect->w == 0 && rect->h == 0))
		NVS_SolidRect (0, 0, nvs_width, nvs_height, COLOR16(Color));
	else
		NVS_SolidRect (rect->x, rect->y, rect->x + rect->w,
			rect->y + rect->h, COLOR16(Color));
}

void NVLIB_DestroyBkGnd (void)
{
}

BOOL NVLIB_InitRectangle (NVLIB_ColorFormat color_format)
{
	nvs_format = color_format;
	return TRUE;
}

void NVLIB_DrawRectangle (U032 Color, NvuRect16 *rect)
{
	if (!rect)
		return;
	NVS_SolidRect (rect->x, rect->y, rect->x + rect->w, rect->y + rect->h,
		COLOR16(Color));
}

void NVLIB_DestroyRectangle (void)
{
}

BOOL NVLIB_InitImage (NVLIB_ColorFormat color_format)
{
	nvs_format = color_format;
	return TRUE;
}

void NVLIB_DrawImage (void *data, NvuDim16 dataSize, NvuRect16 dest)
{
	const U016	*src = (const U016 *)data;
	U016		*dst;
	int		x, y;
	int		x0, y0, x1, y1;
	long		ustep, vstep, v;

	if (!src || !nvs_fb[nvs_hidden] || !dataSize.w || !dataSize.h)
		return;
	if (!dest.w || !dest.h)
		return;

	x0 = dest.x; y0 = dest.y;
	x1 = dest.x + dest.w; y1 = dest.y + dest.h;

	ustep = ((long)dataSize.w << FIXSHIFT) / dest.w;
	vstep = ((long)dataSize.h << FIXSHIFT) / dest.h;

	if (x0 < nvs_clipx0) x0 = nvs_clipx0;
	if (y0 < nvs_clipy0) y0 = nvs_clipy0;
	if (x1 > nvs_clipx1) x1 = nvs_clipx1;
	if (y1 > nvs_clipy1) y1 = nvs_clipy1;

	if (x0 >= x1 || y0 >= y1)
		return;

	v = (long)(y0 - dest.y) * vstep;
	dst = nvs_fb[nvs_hidden] + y0 * nvs_width;

	for (y = y0 ; y < y1 ; y++)
	{
		const U016	*srow = src + (v >> FIXSHIFT) * (long)dataSize.w;
		long		u = (long)(x0 - dest.x) * ustep;

		for (x = x0 ; x < x1 ; x++)
		{
			dst[x] = srow[u >> FIXSHIFT];
			u += ustep;
		}

		v += vstep;
		dst += nvs_width;
	}
}

void NVLIB_DestroyImage (void)
{
}

/*
  The alpha variants.  On the real chip these run the image through a stencil
  and the beta blender; here that is one branch on the top bit and one lerp.

  Beta clamp is the global blend weight, S0.16.  65535 means opaque, which is
  the case worth being fast, so it is peeled off.
*/

static U016	nvs_betaclamp = 0xffff;

void NVLIB_SetBetaClamp (U016 value)
{
	nvs_betaclamp = value;
}

/* Blend src over dst in RGB555 with an 0..255 weight. */
#define BLEND555(dst, src, w) 	(U016)(((((((src) >> 10) & 0x1f) * (w) +					   (((dst) >> 10) & 0x1f) * (255 - (w))) >> 8) << 10) |		       ((((((src) >> 5) & 0x1f) * (w) +						   (((dst) >> 5) & 0x1f) * (255 - (w))) >> 8) << 5) |		       (((((src) & 0x1f) * (w) +						   ((dst) & 0x1f) * (255 - (w))) >> 8)))

BOOL NVLIB_InitImageA (NVLIB_ColorFormat color_format)
{
	nvs_format = color_format;
	return TRUE;
}

void NVLIB_DrawImageA (void *data, NvuDim16 dataSize, NvuRect16 dest)
{
	const U016	*src = (const U016 *)data;
	U016		*dst;
	int		x, y;
	int		x0, y0, x1, y1;
	long		ustep, vstep, v;
	int		weight;

	if (!src || !nvs_fb[nvs_hidden] || !dataSize.w || !dataSize.h)
		return;
	if (!dest.w || !dest.h)
		return;

	x0 = dest.x; y0 = dest.y;
	x1 = dest.x + dest.w; y1 = dest.y + dest.h;

	ustep = ((long)dataSize.w << FIXSHIFT) / dest.w;
	vstep = ((long)dataSize.h << FIXSHIFT) / dest.h;

	if (x0 < nvs_clipx0) x0 = nvs_clipx0;
	if (y0 < nvs_clipy0) y0 = nvs_clipy0;
	if (x1 > nvs_clipx1) x1 = nvs_clipx1;
	if (y1 > nvs_clipy1) y1 = nvs_clipy1;

	if (x0 >= x1 || y0 >= y1)
		return;

	weight = nvs_betaclamp >> 8;

	v = (long)(y0 - dest.y) * vstep;
	dst = nvs_fb[nvs_hidden] + y0 * nvs_width;

	for (y = y0 ; y < y1 ; y++)
	{
		const U016	*srow = src + (v >> FIXSHIFT) * (long)dataSize.w;
		long		u = (long)(x0 - dest.x) * ustep;

		if (weight >= 255)
		{
		/* the common case: opaque blit with a transparency key */
			for (x = x0 ; x < x1 ; x++)
			{
				U016	texel = srow[u >> FIXSHIFT];

				if (texel & 0x8000)
					dst[x] = (U016)(texel & 0x7fff);
				u += ustep;
			}
		}
		else
		{
			for (x = x0 ; x < x1 ; x++)
			{
				U016	texel = srow[u >> FIXSHIFT];

				if (texel & 0x8000)
					dst[x] = BLEND555(dst[x], texel & 0x7fff, weight);
				u += ustep;
			}
		}

		v += vstep;
		dst += nvs_width;
	}
}

void NVLIB_DestroyImageA (void)
{
}

BOOL NVLIB_InitRectangleA (NVLIB_ColorFormat color_format)
{
	nvs_format = color_format;
	return TRUE;
}

void NVLIB_DrawRectangleA (U032 Color, NvuRect16 *rect)
{
	U016	*dest;
	U016	color;
	int	x, y, x0, y0, x1, y1;
	int	weight;

	if (!rect || !nvs_fb[nvs_hidden])
		return;

	weight = nvs_betaclamp >> 8;
	color = COLOR16(Color);

	if (weight >= 255)
	{
		NVS_SolidRect (rect->x, rect->y, rect->x + rect->w,
			rect->y + rect->h, color);
		return;
	}

	x0 = rect->x; y0 = rect->y;
	x1 = rect->x + rect->w; y1 = rect->y + rect->h;

	if (x0 < nvs_clipx0) x0 = nvs_clipx0;
	if (y0 < nvs_clipy0) y0 = nvs_clipy0;
	if (x1 > nvs_clipx1) x1 = nvs_clipx1;
	if (y1 > nvs_clipy1) y1 = nvs_clipy1;

	if (x0 >= x1 || y0 >= y1)
		return;

	dest = nvs_fb[nvs_hidden] + y0 * nvs_width;

	for (y = y0 ; y < y1 ; y++)
	{
		for (x = x0 ; x < x1 ; x++)
			dest[x] = BLEND555(dest[x], color, weight);
		dest += nvs_width;
	}
}

void NVLIB_DestroyRectangleA (void)
{
}

/*
  Quadratic texture mapping.  These are the calls nv1Quake renders through.
*/
BOOL NVLIB_InitQTexQuad (NVLIB_ColorFormat color_format)
{
	nvs_format = color_format;
	if (!nvs_fb[0])
		NVLIB_InitDBPipe (0);
	return TRUE;
}

void NVLIB_DrawQTexQuad (U032 *texture, U032 width, U032 height,
	Nvu1Pt16 *points)
{
	NVS_RenderPatch (points, (int)height, (int)width, (const U016 *)texture,
		NULL, 0);
}

void NVLIB_DestroyQTexQuad (void)
{
}

/*
  The alpha-transparent quad.  In ARGB1555 the top bit says whether the texel
  is drawn; on the chip that goes through the stencil block, here it is one
  branch in the texel loop.
*/
BOOL NVLIB_InitQTexQuadA (NVLIB_ColorFormat color_format)
{
	nvs_format = color_format;
	if (!nvs_fb[0])
		NVLIB_InitDBPipe (0);
	return TRUE;
}

void NVLIB_DrawQTexQuadA (U032 *texture, U032 width, U032 height,
	Nvu1Pt16 *points)
{
	NVS_RenderPatch (points, (int)height, (int)width, (const U016 *)texture,
		NULL, 1);
}

void NVLIB_DestroyQTexQuadA (void)
{
}

BOOL NVLIB_InitBlendQTexQuad (NVLIB_ColorFormat color_format)
{
	nvs_format = color_format;
	if (!nvs_fb[0])
		NVLIB_InitDBPipe (0);
	return TRUE;
}

void NVLIB_DrawBlendQTexQuad (U032 *texture, U032 width, U032 height,
	Nvu1Pt16 *points, U016 *betas)
{
	NVS_RenderPatch (points, (int)height, (int)width, (const U016 *)texture,
		betas, 0);
}

void NVLIB_DestroyBlendQTexQuad (void)
{
}

/*
==============
NVS_QTexTri

Reproduces what NVLIB does: reorder the three vertices into a degenerate
four-corner patch, derive the five interior control points by perspective
interpolation through the nvldiv table, resample the texture into a
2^height by 2^width grid, and hand the lot to the patch renderer.

Vertex w must be an integer in 0..100 -- that is not a style choice, it is the
extent of the nvldiv table, which NVLIB indexes unguarded.
==============
*/
static void NVS_QTexTri (U032 *texture, U032 inW, nvLib_3DV *vertices,
	U032 width, U032 height, int lit)
{
	int		pn[4];
	long		du01, du02, du12;
	long		dv01, dv02, dv12;
	long		a01, a02, a12;
	Nvu1Pt16	cp[9];
	U016		betas[4];
	int		w0, w1, w2, w3, w01, w23, wd;
	float		alpha;
	int		nmaj, nmin;
	int		j, i;
	const U016	*src = (const U016 *)texture;
	U016		*grid;
	long		uA, vA, uB, vB;
	long		duA, dvA, duB, dvB;

	if (!src)
		return;

	if (height < 2 || height > 8 || width < 2 || width > 8)
		return;

	nmaj = 1 << (int)height;
	nmin = 1 << (int)width;

/*
  Orientation.  NVLIB picks the minor axis to run from the leftmost point in
  texture space toward whichever of the other two is closest in v, so that
  resampling walks near-horizontal lines through the source and stays in cache.
  Same idea here, expressed less painfully than QTMT.C's goto ladder.
*/
	du01 = vertices[0].u - vertices[1].u;
	du02 = vertices[0].u - vertices[2].u;
	du12 = du02 - du01;
	dv01 = vertices[0].v - vertices[1].v;
	dv02 = vertices[0].v - vertices[2].v;
	dv12 = dv02 - dv01;

	a01 = dv01 < 0 ? -dv01 : dv01;
	a02 = dv02 < 0 ? -dv02 : dv02;
	a12 = dv12 < 0 ? -dv12 : dv12;

	if (du01 <= 0 && du02 <= 0)
	{	/* vertex 0 is leftmost in u */
		pn[0] = 0; pn[1] = (a01 <= a02) ? 1 : 2;
	}
	else if (du01 >= 0 && du12 <= 0)
	{	/* vertex 1 is leftmost */
		pn[0] = 1; pn[1] = (a01 <= a12) ? 0 : 2;
	}
	else
	{	/* vertex 2 is leftmost */
		pn[0] = 2; pn[1] = (a02 <= a12) ? 0 : 1;
	}
	pn[2] = 3 - pn[0] - pn[1];
	pn[3] = pn[2];

/* Corners: p0 and p2 are the ends of one major edge, p6 and p8 of the other.
   Two of them coincide, which is how a triangle becomes a patch. */
	cp[0].x = vertices[pn[0]].x; cp[0].y = vertices[pn[0]].y;
	cp[2].x = vertices[pn[1]].x; cp[2].y = vertices[pn[1]].y;
	cp[6].x = vertices[pn[2]].x; cp[6].y = vertices[pn[2]].y;
	cp[8].x = vertices[pn[3]].x; cp[8].y = vertices[pn[3]].y;

	w0 = vertices[pn[0]].w; w1 = vertices[pn[1]].w;
	w2 = vertices[pn[2]].w; w3 = vertices[pn[3]].w;

	if (w0 < 0) w0 = 0; else if (w0 > 100) w0 = 100;
	if (w1 < 0) w1 = 0; else if (w1 > 100) w1 = 100;
	if (w2 < 0) w2 = 0; else if (w2 > 100) w2 = 100;
	if (w3 < 0) w3 = 0; else if (w3 > 100) w3 = 100;

	w01 = w0 + w1;
	w23 = w2 + w3;
	if (w01 < 1) w01 = 1;
	if (w23 < 1) w23 = 1;

/* Interior control points, perspective correct via the division table.  This
   is the entire reason a patch tracks a projected triangle instead of bulging:
   the midpoint of each edge is placed where perspective puts it, and the
   interpolating quadratic then passes exactly through it. */
	alpha = nvldiv[w0][w01];
	cp[1].x = (short)(cp[2].x + alpha * (cp[0].x - cp[2].x));
	cp[1].y = (short)(cp[2].y + alpha * (cp[0].y - cp[2].y));

	wd = w0 + w2; if (wd < 1) wd = 1;
	alpha = nvldiv[w0][wd];
	cp[3].x = (short)(cp[6].x + alpha * (cp[0].x - cp[6].x));
	cp[3].y = (short)(cp[6].y + alpha * (cp[0].y - cp[6].y));

	wd = w1 + w3; if (wd < 1) wd = 1;
	alpha = nvldiv[w1][wd];
	cp[5].x = (short)(cp[8].x + alpha * (cp[2].x - cp[8].x));
	cp[5].y = (short)(cp[8].y + alpha * (cp[2].y - cp[8].y));

	alpha = nvldiv[w2][w23];
	cp[7].x = (short)(cp[8].x + alpha * (cp[6].x - cp[8].x));
	cp[7].y = (short)(cp[8].y + alpha * (cp[6].y - cp[8].y));

	wd = w01 + w23;
	alpha = nvldiv[w0][wd] + nvldiv[w1][wd];
	cp[4].x = (short)(cp[7].x + alpha * (cp[1].x - cp[7].x));
	cp[4].y = (short)(cp[7].y + alpha * (cp[1].y - cp[7].y));

/*
  Resample.  Texture space is walked linearly -- all the perspective lives in
  the screen-space control points.  That is the NV1 bargain, and it is why
  large patches swim.
*/
	grid = nvs_grid;

	uA = vertices[pn[0]].u << FIXSHIFT;
	vA = vertices[pn[0]].v << FIXSHIFT;
	uB = vertices[pn[2]].u << FIXSHIFT;
	vB = vertices[pn[2]].v << FIXSHIFT;

	duA = ((vertices[pn[1]].u << FIXSHIFT) - uA) / nmaj;
	dvA = ((vertices[pn[1]].v << FIXSHIFT) - vA) / nmaj;
	duB = ((vertices[pn[3]].u << FIXSHIFT) - uB) / nmaj;
	dvB = ((vertices[pn[3]].v << FIXSHIFT) - vB) / nmaj;

/* half-step, as NVLIB does, so texels sample strip centres */
	uA += duA >> 1; vA += dvA >> 1;
	uB += duB >> 1; vB += dvB >> 1;

	for (j = 0 ; j < nmaj ; j++)
	{
		long	du = (uB - uA) / nmin;
		long	dv = (vB - vA) / nmin;
		long	u = uA + (du >> 1);
		long	v = vA + (dv >> 1);

	/* unrolled by two, matching the two-texels-per-dword the card is fed */
		for (i = nmin >> 1 ; i ; i--)
		{
			*grid++ = src[(v >> FIXSHIFT) * (long)inW + (u >> FIXSHIFT)];
			u += du; v += dv;
			*grid++ = src[(v >> FIXSHIFT) * (long)inW + (u >> FIXSHIFT)];
			u += du; v += dv;
		}

		uA += duA; vA += dvA;
		uB += duB; vB += dvB;
	}

	if (lit)
	{
		betas[0] = (U016)vertices[pn[0]].beta;
		betas[1] = (U016)vertices[pn[1]].beta;
		betas[2] = (U016)vertices[pn[2]].beta;
		betas[3] = (U016)vertices[pn[3]].beta;
		NVS_RenderPatch (cp, (int)height, (int)width, nvs_grid, betas, 0);
	}
	else
		NVS_RenderPatch (cp, (int)height, (int)width, nvs_grid, NULL, 0);
}

BOOL NVLIB_InitQTexTri (NVLIB_ColorFormat color_format)
{
	return NVLIB_InitQTexQuad (color_format);
}

void NVLIB_DrawQTexTri (U032 *texture, U032 inW, nvLib_3DV vertex[3],
	U032 width, U032 height)
{
	NVS_QTexTri (texture, inW, vertex, width, height, 0);
}

void NVLIB_DestroyQTexTri (void)
{
}

BOOL NVLIB_InitLitQTexTri (NVLIB_ColorFormat color_format)
{
	return NVLIB_InitQTexQuad (color_format);
}

void NVLIB_DrawLitQTexTri (U032 *texture, U032 inW, nvLib_3DV vertex[3],
	U032 width, U032 height)
{
	NVS_QTexTri (texture, inW, vertex, width, height, 1);
}

void NVLIB_DestroyLitQTexTri (void)
{
}

/*
=============================================================================

  timing

=============================================================================
*/

static double		nvs_timerscale;
static LARGE_INTEGER	nvs_timerstart;
static int		nvs_haveperf;

BOOL NVLIB_InitTimer (void)
{
	LARGE_INTEGER	freq;

	nvs_haveperf = QueryPerformanceFrequency (&freq);
	if (nvs_haveperf)
		nvs_timerscale = 1.0 / (double)freq.LowPart;
	return TRUE;
}

void NVLIB_StartTimer (void)
{
	if (nvs_haveperf)
		QueryPerformanceCounter (&nvs_timerstart);
}

double NVLIB_ReadTimer (void)
{
	LARGE_INTEGER	now;

	if (!nvs_haveperf)
		return 0.0;

	QueryPerformanceCounter (&now);
	return (double)(now.LowPart - nvs_timerstart.LowPart) * nvs_timerscale;
}

void NVLIB_DestroyTimer (void)
{
}
