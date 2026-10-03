/*
nv1_snd.c -- Quake sound output through the NV1's own audio engine.

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


The Diamond Edge 3D is not a graphics card with sound bolted on, it is a
multimedia accelerator: the NV1 has a wavetable audio engine and a Sega Saturn
game port on the same chip, and NVLIB drives all three.  A build that renders
through NVLIB and then plays its sound through DirectSound is using half the
card, so on real hardware this replaces snd_win.c.

  BUILD:  compiled only when NV1_HARDWARE is defined.  The phase 1 software
          build keeps snd_win.c, because nvsoft emulates the rasteriser, not
          the audio engine.

  STATUS: UNTESTED.  This is written against the NVLIB 1.50 audio API as
          documented in <nvlib.h> and as used by NVLIB's own AUDFILE.C, but it
          has never run against a card.  It is here so the hardware bring-up
          starts from something real rather than a blank file.

How it maps onto Quake:

Quake wants one linear ring buffer and a way to ask where the hardware is
reading from (SNDDMA_GetDMAPos), then mixes ahead of that point.  NVLIB
instead takes a list of buffers and calls back as each one drains.  So the ring
is allocated as one contiguous block, handed to NVLIB as N equal slices of it,
and the callback counts drained slices -- which gives the read position to a
granularity of one slice.  That is the same deal Quake's DOS and Linux drivers
make, and the mixer is built to tolerate it.
*/

#include "quakedef.h"
#include "nv1quake.h"
#include "winquake.h"

#ifdef NV1_HARDWARE

/*
 * snd_win.c is not in this build, but the shared engine still expects the
 * things it defined.  snd_dma.c and snd_mix.c reach for the DirectSound
 * buffer directly -- every use is guarded by `if (pDSBuf)`, so a null pointer
 * makes those paths inert -- and sys_win.c and the video code call
 * S_BlockSound around window activation.
 */
LPDIRECTSOUND		pDS;
LPDIRECTSOUNDBUFFER	pDSBuf;
DWORD			gSndBufSize;

static qboolean		nv1_snd_blocked;

/*
 * Slices of the ring.  More slices means a finer read position and less
 * latency, at the cost of more callbacks.  Eight at 512 frames each is about
 * 46ms per slice at 11kHz, comfortably inside what the mixer works ahead.
 */
#define NV1_SND_SLICES		8
#define NV1_SND_SLICEFRAMES	512

static int	nv1_snd_stream = -1;
static byte	*nv1_snd_ring;
static void	*nv1_snd_bufptr[NV1_SND_SLICES];
static int	nv1_snd_buflen[NV1_SND_SLICES];

static volatile long	nv1_snd_drained;	/* slices finished, ever */

static int	nv1_snd_slicebytes;
static int	nv1_snd_slicemono;		/* mono samples per slice */

/*
===============
NV1_SndBufCompleted

Called by NVLIB as each slice drains.  The stream loops over the same buffers,
so there is nothing to resubmit; all this has to do is advance our idea of
where the hardware is reading.

Runs at interrupt time on real hardware.  Touch nothing but the counter.
===============
*/
static int NV1_SndBufCompleted (const int audioStreamId, V032 clientData,
	int bufIndex)
{
	(void)audioStreamId;
	(void)clientData;
	(void)bufIndex;

	nv1_snd_drained++;

	return 0;
}

/*
==================
SNDDMA_Init
==================
*/
qboolean SNDDMA_Init (void)
{
	int	i;
	int	rate;
	int	channels;
	int	totalbytes;

	if (COM_CheckParm ("-nosound"))
		return false;

/*
 * Rate.  Quake defaults to 11025 and offers -sspeed; the NV1 audio engine
 * resamples in hardware, so anything reasonable is fine.
 */
	rate = 11025;
	if (COM_CheckParm ("-sspeed"))
		rate = Q_atoi (com_argv[COM_CheckParm ("-sspeed") + 1]);

	channels = 2;
	if (COM_CheckParm ("-mono"))
		channels = 1;

	if (!NVLIB_InitAudioPipe ())
	{
		Con_SafePrintf ("NV1: could not open the audio pipe\n");
		return false;
	}

	if (!NVLIB_InitAudio ())
	{
		Con_SafePrintf ("NV1: could not initialise audio\n");
		return false;
	}

	nv1_snd_stream = NVLIB_FindAudioStream (SAMPLE);
	if (nv1_snd_stream < 0)
	{
		Con_SafePrintf ("NV1: no free audio stream\n");
		return false;
	}

/* one contiguous ring, handed over as equal slices */
	nv1_snd_slicebytes = NV1_SND_SLICEFRAMES * channels * 2;
	nv1_snd_slicemono = NV1_SND_SLICEFRAMES * channels;
	totalbytes = nv1_snd_slicebytes * NV1_SND_SLICES;

	nv1_snd_ring = (byte *)malloc (totalbytes);
	if (!nv1_snd_ring)
	{
		Con_SafePrintf ("NV1: out of memory for the audio ring\n");
		return false;
	}
	memset (nv1_snd_ring, 0, totalbytes);

	for (i = 0 ; i < NV1_SND_SLICES ; i++)
	{
		nv1_snd_bufptr[i] = nv1_snd_ring + i * nv1_snd_slicebytes;
		nv1_snd_buflen[i] = nv1_snd_slicebytes;
	}

	nv1_snd_drained = 0;

/*
 * soundType is the channel count, as in NVLIB's own AUDFILE.C.  Signed 16 bit
 * little endian linear is what the mixer produces, and looping keeps the
 * engine cycling the same slices forever, which is exactly a ring buffer.
 */
	if (!NVLIB_SetAudioSample (nv1_snd_stream, channels, NV1_SND_SLICES,
		NV_AUDIO_FORMAT_LE_S016_LINEAR, (U032)rate, 0, TRUE,
		nv1_snd_bufptr, nv1_snd_buflen,
		(BufCompleted)NV1_SndBufCompleted, (V032)0))
	{
		Con_SafePrintf ("NV1: could not set up the audio stream\n");
		free (nv1_snd_ring);
		nv1_snd_ring = NULL;
		return false;
	}

	shm = &sn;
	memset ((void *)shm, 0, sizeof(*shm));

	shm->splitbuffer = false;
	shm->channels = channels;
	shm->samplebits = 16;
	shm->speed = rate;
	shm->samples = totalbytes / 2;		/* in mono samples */
	shm->submission_chunk = nv1_snd_slicemono;
	shm->buffer = nv1_snd_ring;
	shm->samplepos = 0;

	if (!NVLIB_PlayAudioStream (nv1_snd_stream))
	{
		Con_SafePrintf ("NV1: could not start the audio stream\n");
		SNDDMA_Shutdown ();
		return false;
	}

	Con_SafePrintf ("NV1 audio: %d Hz, %d channel%s, %d x %d byte slices\n",
		rate, channels, channels == 1 ? "" : "s",
		NV1_SND_SLICES, nv1_snd_slicebytes);

	return true;
}

/*
==============
SNDDMA_GetDMAPos

Where the engine is reading, in mono samples.  Resolution is one slice, which
is what the callback gives us.
==============
*/
int SNDDMA_GetDMAPos (void)
{
	long	drained;

	if (!nv1_snd_ring)
		return 0;

	drained = nv1_snd_drained;

	shm->samplepos = (int)((drained % NV1_SND_SLICES) * nv1_snd_slicemono);

	return shm->samplepos;
}

/*
==============
SNDDMA_Submit

Nothing to do: the engine DMAs straight out of the buffers we gave it, so the
mixer's writes are already where the hardware will look.
==============
*/
void SNDDMA_Submit (void)
{
}

/*
==============
S_BlockSound / S_UnblockSound

Called when the game loses and regains the foreground.  Pausing the stream is
the polite thing to do: the audio engine is on the card and would otherwise
keep looping the last buffer while another application has focus.
==============
*/
void S_BlockSound (void)
{
	if (nv1_snd_blocked || nv1_snd_stream < 0)
		return;

	nv1_snd_blocked = true;
	NVLIB_PauseAudioStream (nv1_snd_stream);
}

void S_UnblockSound (void)
{
	if (!nv1_snd_blocked || nv1_snd_stream < 0)
		return;

	nv1_snd_blocked = false;
	NVLIB_ResumeAudioStream (nv1_snd_stream);
}

/*
==============
SNDDMA_Shutdown
==============
*/
void SNDDMA_Shutdown (void)
{
	if (nv1_snd_stream >= 0)
	{
		NVLIB_StopAudioStream (nv1_snd_stream, TRUE);
		NVLIB_DestroyAudioStream (nv1_snd_stream);
		nv1_snd_stream = -1;
	}

	NVLIB_TerminateAudio ();

	if (nv1_snd_ring)
	{
		free (nv1_snd_ring);
		nv1_snd_ring = NULL;
	}

	shm = NULL;
}

#endif /* NV1_HARDWARE */
