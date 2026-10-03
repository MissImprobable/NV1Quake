/*
Copyright (C) 1996-1997 Id Software, Inc.

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
// r_misc.c

#include "quakedef.h"
#include "nv1quake.h"



/*
==================
R_InitTextures
==================
*/
void	R_InitTextures (void)
{
	int		x,y, m;
	byte	*dest;

// create a simple checkerboard texture for the default
	r_notexture_mip = Hunk_AllocName (sizeof(texture_t) + 16*16+8*8+4*4+2*2, "notexture");
	
	r_notexture_mip->width = r_notexture_mip->height = 16;
	r_notexture_mip->offsets[0] = sizeof(texture_t);
	r_notexture_mip->offsets[1] = r_notexture_mip->offsets[0] + 16*16;
	r_notexture_mip->offsets[2] = r_notexture_mip->offsets[1] + 8*8;
	r_notexture_mip->offsets[3] = r_notexture_mip->offsets[2] + 4*4;
	
	for (m=0 ; m<4 ; m++)
	{
		dest = (byte *)r_notexture_mip + r_notexture_mip->offsets[m];
		for (y=0 ; y< (16>>m) ; y++)
			for (x=0 ; x< (16>>m) ; x++)
			{
				if (  (y< (8>>m) ) ^ (x< (8>>m) ) )
					*dest++ = 0;
				else
					*dest++ = 0xff;
			}
	}	
}

byte	dottexture[8][8] =
{
	{0,1,1,0,0,0,0,0},
	{1,1,1,1,0,0,0,0},
	{1,1,1,1,0,0,0,0},
	{0,1,1,0,0,0,0,0},
	{0,0,0,0,0,0,0,0},
	{0,0,0,0,0,0,0,0},
	{0,0,0,0,0,0,0,0},
	{0,0,0,0,0,0,0,0},
};
void R_InitParticleTexture (void)
{
/*
 * GLQuake gives every particle an 8x8 alpha-blended dot.  On NV1 a textured
 * primitive costs its texel count no matter how small it lands, so an 8x8
 * patch per particle is 64 texels to draw what is usually one or two pixels.
 * Particles are drawn as solid rectangles instead -- see R_DrawParticles.
 */
	particletexture = -1;
}
/*
===============
R_Envmap_f

Grab six views for environment mapping tests
===============
*/
void R_Envmap_f (void)
{
/*
 * Needs to read the framebuffer back six times.  NVLIB 1.50 can do that
 * (NVLIB_GetSnapshot), but there is nothing in Quake that needs it, so it is
 * not worth the DMA setup.
 */
	Con_Printf ("envmap is not supported by the NV1 renderer\n");
}
/*
===============
R_Init
===============
*/
void R_Init (void)
{	
	extern byte *hunk_base;

	Cmd_AddCommand ("timerefresh", R_TimeRefresh_f);	
	Cmd_AddCommand ("envmap", R_Envmap_f);	
	Cmd_AddCommand ("pointfile", R_ReadPointFile_f);	

	Cvar_RegisterVariable (&r_norefresh);
	Cvar_RegisterVariable (&r_lightmap);
	Cvar_RegisterVariable (&r_fullbright);
	Cvar_RegisterVariable (&r_drawentities);
	Cvar_RegisterVariable (&r_drawworld);
	Cvar_RegisterVariable (&r_drawviewmodel);
	Cvar_RegisterVariable (&r_shadows);
	Cvar_RegisterVariable (&r_mirroralpha);
	Cvar_RegisterVariable (&r_wateralpha);
	Cvar_RegisterVariable (&r_dynamic);
	Cvar_RegisterVariable (&r_novis);
	Cvar_RegisterVariable (&r_speeds);

	Cvar_RegisterVariable (&gl_playermip);
	Cvar_RegisterVariable (&gl_nocolors);
	Cvar_RegisterVariable (&gl_keeptjunctions);
	Cvar_RegisterVariable (&gl_reporttjunctions);
	Cvar_RegisterVariable (&gl_doubleeyes);

/* the knobs that actually decide how fast this runs */
	Cvar_RegisterVariable (&nv1_clear);
	Cvar_RegisterVariable (&nv1_zslab);
	Cvar_RegisterVariable (&nv1_subdiv);
	Cvar_RegisterVariable (&nv1_maxsubdiv);
	Cvar_RegisterVariable (&nv1_minsubdiv);
	Cvar_RegisterVariable (&nv1_lightmap);
	Cvar_RegisterVariable (&nv1_showtexels);
	Cvar_RegisterVariable (&nv1_worldsubdiv);
	Cvar_RegisterVariable (&nv1_skylayers);

	R_InitParticles ();
	R_InitParticleTexture ();

#ifdef GLTEST
	Test_Init ();
#endif

/* reserve a block of texture slots for translated player skins */
	playertextures = numnv1textures;
	numnv1textures += 16;
}

/*
===============
R_TranslatePlayerSkin

Translates a skin texture by the per-player color lookup
===============
*/
void R_TranslatePlayerSkin (int playernum)
{
	int		top, bottom;
	byte		translate[256];
	int		i, j, s;
	model_t		*model;
	aliashdr_t	*paliashdr;
	byte		*original;
	static byte	translated[512 * 256];
	int		inwidth, inheight;
	int		scaled_width, scaled_height;
	byte		*out;

	top = cl.scores[playernum].colors & 0xf0;
	bottom = (cl.scores[playernum].colors & 15) << 4;

	for (i = 0 ; i < 256 ; i++)
		translate[i] = i;

	for (i = 0 ; i < 16 ; i++)
	{
		if (top < 128)		/* the artists made some backwards ranges */
			translate[TOP_RANGE + i] = top + i;
		else
			translate[TOP_RANGE + i] = top + 15 - i;

		if (bottom < 128)
			translate[BOTTOM_RANGE + i] = bottom + i;
		else
			translate[BOTTOM_RANGE + i] = bottom + 15 - i;
	}

/*
 * locate the original skin pixels
 */
	currententity = &cl_entities[1 + playernum];
	model = currententity->model;
	if (!model)
		return;		/* player doesn't have a model yet */
	if (model->type != mod_alias)
		return;		/* only translate skins on alias models */

	paliashdr = (aliashdr_t *)Mod_Extradata (model);
	s = paliashdr->skinwidth * paliashdr->skinheight;
	if (currententity->skinnum < 0 ||
		currententity->skinnum >= paliashdr->numskins)
	{
		Con_Printf ("(%d): Invalid player skin #%d\n", playernum,
			currententity->skinnum);
		original = (byte *)paliashdr + paliashdr->texels[0];
	}
	else
		original = (byte *)paliashdr + paliashdr->texels[currententity->skinnum];

	inwidth = paliashdr->skinwidth;
	inheight = paliashdr->skinheight;

	scaled_width = inwidth;
	scaled_height = inheight;
	if (scaled_width > 512) scaled_width = 512;
	if (scaled_height > 256) scaled_height = 256;

/* translate in place; the texture manager does the scaling to a power of two */
	out = translated;
	for (i = 0 ; i < scaled_height ; i++)
	{
		byte	*inrow = original + inwidth * (i * inheight / scaled_height);

		for (j = 0 ; j < scaled_width ; j++)
			*out++ = translate[inrow[j * inwidth / scaled_width]];
	}

	NV1_UploadIntoTexture (playertextures + playernum, scaled_width,
		scaled_height, translated, false, false);
}

/*
===============
R_NewMap
===============
*/
void R_NewMap (void)
{
	int		i;
	
	for (i=0 ; i<256 ; i++)
		d_lightstylevalue[i] = 264;		// normal light value

	memset (&r_worldentity, 0, sizeof(r_worldentity));
	r_worldentity.model = cl.worldmodel;

// clear out efrags in case the level hasn't been reloaded
// FIXME: is this one short?
	for (i=0 ; i<cl.worldmodel->numleafs ; i++)
		cl.worldmodel->leafs[i].efrags = NULL;
		 	
	r_viewleaf = NULL;
	R_ClearParticles ();

	GL_BuildLightmaps ();

	// identify sky texture
	skytexturenum = -1;
	mirrortexturenum = -1;
	for (i=0 ; i<cl.worldmodel->numtextures ; i++)
	{
		if (!cl.worldmodel->textures[i])
			continue;
		if (!Q_strncmp(cl.worldmodel->textures[i]->name,"sky",3) )
			skytexturenum = i;
		if (!Q_strncmp(cl.worldmodel->textures[i]->name,"window02_1",10) )
			mirrortexturenum = i;
 		cl.worldmodel->textures[i]->texturechain = NULL;
	}
#ifdef QUAKE2
	R_LoadSkys ();
#endif
}


/*
====================
R_TimeRefresh_f

For program optimization
====================
*/
void R_TimeRefresh_f (void)
{
	int		i;
	float		start, stop, time;

	start = Sys_FloatTime ();
	for (i = 0 ; i < 128 ; i++)
	{
		NV1_BeginRendering (&glx, &gly, &glwidth, &glheight);
		r_refdef.viewangles[1] = i / 128.0 * 360.0;
		R_RenderView ();
		NV1_EndRendering ();
	}
	stop = Sys_FloatTime ();

	time = stop - start;
	Con_Printf ("%f seconds (%f fps)\n", time, 128 / time);
}
void D_FlushCaches (void)
{
}


