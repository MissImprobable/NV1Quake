/*
nv1_sndc.c -- C versions of the two sound mixer routines Quake keeps in
assembly, for builds that do not assemble snd_mixa.s.

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


These are id's own `#if !id386` implementations from snd_mix.c, verbatim in
behaviour.  They live here rather than being compiled from snd_mix.c because
of an awkward interaction:

  - Quake sets id386 from __i386__, which quakedef.h defines itself whenever
    _M_IX86 is set.  On a 32-bit build that compiles these two out, expecting
    snd_mixa.s to supply them, and we do not assemble it.
  - The switch that stops quakedef.h defining __i386__ is WINDED, which is
    what mathlib.c and world.c are compiled with for exactly this reason.
  - But WINDED also turns VID_LockBuffer and VID_UnlockBuffer into empty
    macros, and snd_mix.c is the one file of the three that includes
    winquake.h, where both are *declared*.  `void VID_LockBuffer (void);` with
    the macro in scope is not a declaration, it is a syntax error.

So snd_mix.c is compiled normally and simply loses these two, and they are
supplied here instead.
*/

#include "quakedef.h"
#include "nv1quake.h"

#if id386

/* only needed when the assembly would have been used and is not present */

extern portable_samplepair_t	paintbuffer[];
extern int			snd_scaletable[32][256];
extern int			*snd_p, snd_linear_count, snd_vol;
extern short			*snd_out;

void Snd_WriteLinearBlastStereo16 (void)
{
	int	i;
	int	val;

	for (i = 0 ; i < snd_linear_count ; i += 2)
	{
		val = (snd_p[i] * snd_vol) >> 8;
		if (val > 0x7fff)
			snd_out[i] = 0x7fff;
		else if (val < (short)0x8000)
			snd_out[i] = (short)0x8000;
		else
			snd_out[i] = val;

		val = (snd_p[i + 1] * snd_vol) >> 8;
		if (val > 0x7fff)
			snd_out[i + 1] = 0x7fff;
		else if (val < (short)0x8000)
			snd_out[i + 1] = (short)0x8000;
		else
			snd_out[i + 1] = val;
	}
}

void SND_PaintChannelFrom8 (channel_t *ch, sfxcache_t *sc, int count)
{
	int		data;
	int		*lscale, *rscale;
	unsigned char	*sfx;
	int		i;

	if (ch->leftvol > 255)
		ch->leftvol = 255;
	if (ch->rightvol > 255)
		ch->rightvol = 255;

	lscale = snd_scaletable[ch->leftvol >> 3];
	rscale = snd_scaletable[ch->rightvol >> 3];
	sfx = (signed char *)sc->data + ch->pos;

	for (i = 0 ; i < count ; i++)
	{
		data = sfx[i];
		paintbuffer[i].left += lscale[data];
		paintbuffer[i].right += rscale[data];
	}

	ch->pos += count;
}

#endif	/* id386 */
