/*
nv1_viddos.c -- DOS video layer for nv1Quake.

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


This is the whole of nv1_vid.c's job, minus Windows.

There is no window, no message pump, no mode enumeration and no palette
device: the DOS resource manager owns the display, NvRequestVideoMode asks it
for a mode, and NVLIB draws into the framebuffer.  The sequence is the one
NVIDIA's own DOSDEMO uses, including the NvGetVideoMode check afterwards --
the request is only a request, and the driver is free to give you something
smaller.
*/

#include <stdio.h>
#include <string.h>
#include <dos.h>

#include "quakedef.h"
#include "nv1quake.h"

viddef_t	vid;			/* global video state */

unsigned short	d_8to16table[256];
unsigned	d_8to24table[256];

qboolean	scr_skipupdate;

unsigned char	vid_curpal[256 * 3];

int		glx, gly, glwidth, glheight;

void (*vid_menudrawfn)(void);
void (*vid_menukeyfn)(int key);

static qboolean	vid_initialized;

/*
 * How much framebuffer the card has, in megabytes.  A stock Edge 3D has 1MB,
 * which fits exactly one double-buffered 640x400 at 32K colours; the 2200 with
 * its expansion modules has 2MB and can manage 640x480 or 800x600.
 */
cvar_t	nv1_vidmem = {"nv1_vidmem", "1", true};

typedef struct
{
	int	mode;
	int	width;
	int	height;
} nv1vidmode_t;

static nv1vidmode_t	nv1_modes[] =
{
	{ NV_VIDMODE_640x400,	640,  400 },
	{ NV_VIDMODE_640x480,	640,  480 },
	{ NV_VIDMODE_800x600,	800,  600 },
	{ NV_VIDMODE_1024x768,	1024, 768 },
};

#define NUM_NV1_MODES	(sizeof(nv1_modes) / sizeof(nv1_modes[0]))

/*
================
NV1_RestoreTextMode

The DOS resource manager has NvRequestVideoMode and NvGetVideoMode but no
NvRestoreVideoMode -- that one is Windows only, and reasonably so: under DOS
there is no desktop setting to put back, you just ask the BIOS for text mode
again.  Checked against the library's symbol table, not assumed.
================
*/
static void NV1_RestoreTextMode (void)
{
	union REGS	regs;

	memset (&regs, 0, sizeof(regs));
	regs.w.ax = 0x0003;		/* 80x25 colour text */
	int386 (0x10, &regs, &regs);
}

/*
================
NV1_ModeFits

Two 16 bit buffers have to live in the card's memory.
================
*/
static qboolean NV1_ModeFits (int width, int height, long bytes)
{
	return ((long)width * (long)height * 2L * 2L <= bytes) ? true : false;
}

/*
=================
VID_Init
=================
*/
void VID_Init (unsigned char *palette)
{
	int		buffers, resolution, depth;
	int		want;
	int		wantw, wanth;
	long		fbbytes;
	unsigned int	i;
	int		mb;

	Cvar_RegisterVariable (&nv1_vidmem);

	if (COM_CheckParm ("-vidmem"))
		Cvar_SetValue ("nv1_vidmem",
			Q_atof (com_argv[COM_CheckParm ("-vidmem") + 1]));

	mb = (int)nv1_vidmem.value;
	if (mb < 1) mb = 1;
	if (mb > 4) mb = 4;
	fbbytes = (long)mb * 1024L * 1024L;

/* what was asked for, defaulting to the mode the card was built around */
	wantw = 640;
	wanth = 400;

	if (COM_CheckParm ("-width"))
		wantw = Q_atoi (com_argv[COM_CheckParm ("-width") + 1]);
	if (COM_CheckParm ("-height"))
		wanth = Q_atoi (com_argv[COM_CheckParm ("-height") + 1]);

	want = NV_VIDMODE_640x400;
	for (i = 0 ; i < NUM_NV1_MODES ; i++)
	{
		if (nv1_modes[i].width < wantw || nv1_modes[i].height < wanth)
			continue;
		if (!NV1_ModeFits (nv1_modes[i].width, nv1_modes[i].height, fbbytes))
			continue;

		want = nv1_modes[i].mode;
		break;
	}

/*
 * Ask the resource manager.  In DOS the first argument is 0, not an instance
 * handle.
 */
	if (!NvRequestVideoMode (0, NV_VIDMODE_DOUBLE_BUFFER, want,
		NV_VIDMODE_HIGH_COLOR))
	{
		Sys_Error ("nv1Quake needs an NV1 class accelerator (Diamond Edge "
			"3D / STG2000).\nThe display driver would not give us a "
			"double buffered 32K colour mode.");
	}

/*
 * The request is only a request.  Find out what we actually got -- this call
 * is DOS only, which is why the Windows build cannot do the same check.
 */
	buffers = resolution = depth = 0;
	NvGetVideoMode (0, &buffers, &resolution, &depth);

	if (depth != NV_VIDMODE_HIGH_COLOR)
	{
		NV1_RestoreTextMode ();
		Sys_Error ("nv1Quake needs a 32K colour mode; the driver gave us "
			"something else.");
	}

	if (buffers != NV_VIDMODE_DOUBLE_BUFFER)
	{
		NV1_RestoreTextMode ();
		Sys_Error ("nv1Quake needs a double buffered mode.  With 1MB on the "
			"card that means 640x400;\nif your board has the memory "
			"expansion fitted, try -vidmem 2.");
	}

	vid.width = 640;
	vid.height = 400;

	for (i = 0 ; i < NUM_NV1_MODES ; i++)
	{
		if (nv1_modes[i].mode == resolution)
		{
			vid.width = nv1_modes[i].width;
			vid.height = nv1_modes[i].height;
			break;
		}
	}

	vid.conwidth = vid.width;
	vid.conheight = vid.height;
	vid.aspect = ((float)vid.height / (float)vid.width) * (320.0 / 240.0);
	vid.numpages = 2;
	vid.colormap = host_colormap;
	vid.fullbright = 256 - LittleLong (*((int *)vid.colormap + 2048));
	vid.maxwarpwidth = vid.width;
	vid.maxwarpheight = vid.height;
	vid.recalc_refdef = 1;

	glx = 0;
	gly = 0;
	glwidth = vid.width;
	glheight = vid.height;

	VID_SetPalette (palette);

	NV1_DeviceInit ();

	vid_initialized = true;

	Con_Printf ("NV1: %d MB framebuffer, %d x %d, 32768 colours, "
		"double buffered\n", mb, vid.width, vid.height);
}

/*
=================
VID_Shutdown
=================
*/
void VID_Shutdown (void)
{
	if (!vid_initialized)
		return;

	vid_initialized = false;

	NVLIB_DestroyQTexQuad ();
	NVLIB_DestroyQTexQuadA ();
	NVLIB_DestroyBlendQTexQuad ();
	NVLIB_DestroyImage ();
	NVLIB_DestroyImageA ();
	NVLIB_DestroyRectangle ();
	NVLIB_DestroyBkGnd ();
	NVLIB_DestroyDBPipe ();

	NV1_RestoreTextMode ();
}

/*
=================
VID_SetPalette

Quake hands us 8-bit RGB.  The card is RGB555, so this is where the game's
colour range collapses to 32768.  Index 255 is Quake's transparent colour.
=================
*/
void VID_SetPalette (unsigned char *palette)
{
	byte		*pal;
	unsigned	r, g, b, v;
	int		i;
	unsigned	*table;

	pal = palette;
	table = d_8to24table;

	for (i = 0 ; i < 256 ; i++)
	{
		r = pal[0];
		g = pal[1];
		b = pal[2];
		pal += 3;

		v = (255 << 24) + (r << 0) + (g << 8) + (b << 16);
		*table++ = v;

		nv1_palette555[i] = (unsigned short)
			(((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3));
		nv1_palettealpha[i] = 255;
	}

	d_8to24table[255] &= 0xffffff;
	nv1_palette555[255] = 0;
	nv1_palettealpha[255] = 0;

	memcpy (vid_curpal, palette, sizeof(vid_curpal));
}

/*
=================
VID_ShiftPalette

On a paletted display this is where the bonus flash and the underwater tint
would happen for free.  In a 555 mode there is no palette to shift, so the
blend is drawn as a translucent rectangle over the scene instead; see
R_PolyBlend.
=================
*/
void VID_ShiftPalette (unsigned char *palette)
{
	(void)palette;
}

void VID_Update (vrect_t *rects)
{
	(void)rects;
}

int VID_SetMode (int modenum, unsigned char *palette)
{
	(void)modenum;
	(void)palette;

	return 1;
}

void VID_HandlePause (qboolean pause)
{
	(void)pause;
}

void VID_SetDefaultMode (void)
{
}

/*
 * VID_LockBuffer / VID_UnlockBuffer are not defined here on purpose: outside
 * _WIN32, quakedef.h already defines them as empty macros, so a definition
 * would expand to nonsense.
 */

int VID_ForceUnlockedAndReturnState (void)
{
	return 0;
}

void VID_ForceLockState (int lk)
{
	(void)lk;
}

/*
 * Quake draws the loading disc straight to the front buffer on hardware that
 * allows it.  The NV pipe has no way to reach the visible buffer without
 * flipping, so these do nothing rather than corrupt the frame in progress.
 */
void D_BeginDirectRect (int x, int y, byte *pbitmap, int width, int height)
{
	(void)x; (void)y; (void)pbitmap; (void)width; (void)height;
}

void D_EndDirectRect (int x, int y, int width, int height)
{
	(void)x; (void)y; (void)width; (void)height;
}

/*
===============
NV1_DeviceInit

Create the NVLIB objects we render through.  On the card each of these
allocates hardware objects and patchcords in the DMA channel and wires them
into the double buffer pipe, so it is done once and never per frame.
===============
*/
void NV1_DeviceInit (void)
{
/* The division table NVLIB uses to place perspective-correct control points.
   Without this every patch is drawn with alpha 0.5 everywhere. */
	NVLIB_InitDivLUT ();

	if (!NVLIB_InitDBPipe (0))
		Sys_Error ("NV1: NVLIB_InitDBPipe failed");

	NVLIB_InitBkGnd (RGB555);
	NVLIB_InitRectangle (RGB555);
	NVLIB_InitImage (RGB555);
	NVLIB_InitImageA (ARGB1555);
	NVLIB_InitQTexQuad (RGB555);
	NVLIB_InitQTexQuadA (ARGB1555);
	NVLIB_InitBlendQTexQuad (RGB555);

	NVLIB_SetClip (NVLIB_CLIP_OFF);
}

/*
=================
NV1_Set2D

With no transform hardware there is no projection to change; this only has to
drop the clip rectangle back to the full screen after the 3D view.
=================
*/
void NV1_Set2D (void)
{
	NVLIB_SetClip (NVLIB_CLIP_OFF);
}

/*
=================
NV1_BeginRendering / NV1_EndRendering
=================
*/
void NV1_BeginRendering (int *x, int *y, int *width, int *height)
{
	*x = 0;
	*y = 0;
	*width = vid.width;
	*height = vid.height;
}

void NV1_EndRendering (void)
{
	if (!scr_skipupdate || block_drawing)
		NVLIB_FlipDBPipe (true);

/*
 * Force the status bar to repaint next frame.  Sbar normally repaints only
 * for vid.numpages frames after something changes, assuming what it drew is
 * still in the buffer; here the view is cleared every frame, so the HUD would
 * vanish between changes.  This is what glquake's fullsbardraw does.
 */
	Sbar_Changed ();
}
