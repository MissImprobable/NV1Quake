/*
nv1_sysdos.c -- DOS system layer for nv1Quake, written for Open Watcom / DOS4GW.

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


Why this exists rather than using id's sys_dos.c:

id built DOS Quake with DJGPP.  sys_dos.c opens with <dpmi.h>, <sys/nearptr.h>
and <dir.h>, and dosasm.s is GAS syntax -- none of which Watcom has.  And we
cannot simply use DJGPP instead, because NVIDIA built nvlibdos.lib with Watcom
10.5 and it is an OMF library; DJGPP emits COFF and cannot link it.

So the toolchain is forced: NVLIB for DOS means Watcom, and Watcom means a new
system layer.  This is that layer, and it is deliberately small -- everything
interesting is in the renderer, and this only has to open files, tell the time,
and read the keyboard.

The keyboard handler follows id's own TrapKey: take the scancode, acknowledge
the interrupt, and do not chain.  That means the BIOS never sees the keys,
which is what a game wants (it needs make AND break codes, and the BIOS gives
neither), and it is why the original vector must be put back on the way out.
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <fcntl.h>
#include <io.h>
#include <conio.h>
#include <dos.h>
#include <direct.h>

#include "quakedef.h"
#include "nv1quake.h"

qboolean		isDedicated;

static quakeparms_t	parms;
static char		cwd[1024];

/*
=============================================================================

  file i/o

=============================================================================
*/

#define MAX_HANDLES	10

static FILE	*sys_handles[MAX_HANDLES];

static int findhandle (void)
{
	int	i;

	for (i = 1 ; i < MAX_HANDLES ; i++)
		if (!sys_handles[i])
			return i;

	Sys_Error ("out of handles");
	return -1;
}

static int Sys_FileLength (FILE *f)
{
	int	pos, end;

	pos = ftell (f);
	fseek (f, 0, SEEK_END);
	end = ftell (f);
	fseek (f, pos, SEEK_SET);

	return end;
}

int Sys_FileOpenRead (char *path, int *hndl)
{
	FILE	*f;
	int	i;

	i = findhandle ();

	f = fopen (path, "rb");
	if (!f)
	{
		*hndl = -1;
		return -1;
	}

	sys_handles[i] = f;
	*hndl = i;

	return Sys_FileLength (f);
}

int Sys_FileOpenWrite (char *path)
{
	FILE	*f;
	int	i;

	i = findhandle ();

	f = fopen (path, "wb");
	if (!f)
		Sys_Error ("Error opening %s", path);

	sys_handles[i] = f;

	return i;
}

void Sys_FileClose (int handle)
{
	fclose (sys_handles[handle]);
	sys_handles[handle] = NULL;
}

void Sys_FileSeek (int handle, int position)
{
	fseek (sys_handles[handle], position, SEEK_SET);
}

int Sys_FileRead (int handle, void *dest, int count)
{
	return fread (dest, 1, count, sys_handles[handle]);
}

int Sys_FileWrite (int handle, void *data, int count)
{
	return fwrite (data, 1, count, sys_handles[handle]);
}

int Sys_FileTime (char *path)
{
	FILE	*f;

	f = fopen (path, "rb");
	if (f)
	{
		fclose (f);
		return 1;
	}

	return -1;
}

void Sys_mkdir (char *path)
{
	mkdir (path);
}

/*
=============================================================================

  timing

  DOS gives you an 18.2 Hz tick, which is nowhere near enough for a game.  The
  8254 channel 0 counter runs underneath it at 1.193182 MHz and wraps every
  tick, so combining the two gives sub-microsecond resolution without
  reprogramming anything or stealing the timer interrupt.

  The tick is read either side of the counter: if it moved, the counter might
  belong to either tick, so try again.

  Under DOS/4GW the flat data selector is zero based and covers the low
  megabyte, so the BIOS tick counter at 0000:046C is directly addressable.

=============================================================================
*/

#define PIT_FREQ	1193182.0
#define BIOS_TICKS	(*(volatile unsigned long *)0x46cL)

static double	sys_basetime;
static int	sys_timeinited;

static double Sys_RawTime (void)
{
	unsigned long	tick0, tick1;
	unsigned int	counter;
	int		tries;

	for (tries = 0 ; tries < 4 ; tries++)
	{
		tick0 = BIOS_TICKS;

	/* latch channel 0 and read it back, low byte first */
		outp (0x43, 0x00);
		counter = inp (0x40);
		counter |= inp (0x40) << 8;

		tick1 = BIOS_TICKS;

		if (tick0 == tick1)
			break;
	}

/* the counter counts DOWN from 65536 across one tick */
	return (double)tick0 * (65536.0 / PIT_FREQ) +
		(double)(65536 - counter) / PIT_FREQ;
}

double Sys_FloatTime (void)
{
	double	now;

	now = Sys_RawTime ();

	if (!sys_timeinited)
	{
		sys_timeinited = 1;
		sys_basetime = now;
		return 0.0;
	}

/*
 * The BIOS tick rolls over at midnight.  Quake only ever looks at differences,
 * so all that matters is that time never goes backwards.
 */
	if (now < sys_basetime)
		sys_basetime = now;

	return now - sys_basetime;
}

/*
=============================================================================

  keyboard

=============================================================================
*/

#define KEYBUF_SIZE	256

static byte		keybuf[KEYBUF_SIZE];
static volatile int	keybuf_head;
static int		keybuf_tail;

static void (__interrupt __far *old_keyhandler)(void);
static qboolean	keyboard_hooked;

/* id's table, from sys_dos.c */
static byte	scantokey[128] =
{
/*  0        1     2     3     4     5     6     7
    8        9     A     B     C     D     E     F   */
	0,     27,   '1',  '2',  '3',  '4',  '5',  '6',
	'7',   '8',  '9',  '0',  '-',  '=',  K_BACKSPACE, 9,	/* 0 */
	'q',   'w',  'e',  'r',  't',  'y',  'u',  'i',
	'o',   'p',  '[',  ']',  13,   K_CTRL, 'a', 's',	/* 1 */
	'd',   'f',  'g',  'h',  'j',  'k',  'l',  ';',
	'\'',  '`',  K_SHIFT, '\\', 'z', 'x', 'c', 'v',		/* 2 */
	'b',   'n',  'm',  ',',  '.',  '/',  K_SHIFT, '*',
	K_ALT, ' ',  0,    K_F1, K_F2, K_F3, K_F4, K_F5,	/* 3 */
	K_F6,  K_F7, K_F8, K_F9, K_F10, 0,   0,    K_HOME,
	K_UPARROW, K_PGUP, '-', K_LEFTARROW, '5', K_RIGHTARROW, '+', K_END, /* 4 */
	K_DOWNARROW, K_PGDN, K_INS, K_DEL, 0, 0, 0, K_F11,
	K_F12, 0,    0,    0,    0,    0,    0,    0,		/* 5 */
	0,     0,    0,    0,    0,    0,    0,    0,
	0,     0,    0,    0,    0,    0,    0,    0,		/* 6 */
	0,     0,    0,    0,    0,    0,    0,    0,
	0,     0,    0,    0,    0,    0,    0,    0		/* 7 */
};

/*
================
Sys_KeyHandler

Take the scancode, acknowledge the 8259, return.  Deliberately does not chain:
the BIOS handler would consume the code and hand back an ASCII character with
no key-up, which is useless for a game.
================
*/
static void __interrupt __far Sys_KeyHandler (void)
{
	keybuf[keybuf_head] = inp (0x60);
	keybuf_head = (keybuf_head + 1) & (KEYBUF_SIZE - 1);

	outp (0x20, 0x20);
}

static void Sys_HookKeyboard (void)
{
	if (keyboard_hooked)
		return;

	keybuf_head = keybuf_tail = 0;

	old_keyhandler = _dos_getvect (9);
	_dos_setvect (9, Sys_KeyHandler);

	keyboard_hooked = true;
}

static void Sys_UnhookKeyboard (void)
{
	if (!keyboard_hooked)
		return;

	_dos_setvect (9, old_keyhandler);
	keyboard_hooked = false;
}

/*
================
Sys_SendKeyEvents

Drain the scancode ring into Quake's key system.  The awkward cases -- the
0xe0 extended prefix, the 0xe1 pause sequence, and the fake shifts the
keyboard sends around the extended cursor keys -- are handled the way id's
sys_dos.c handled them.
================
*/
void Sys_SendKeyEvents (void)
{
	int	k, next;
	int	outkey;

	while (keybuf_head != keybuf_tail)
	{
		k = keybuf[keybuf_tail++];
		keybuf_tail &= (KEYBUF_SIZE - 1);

		if (k == 0xe0)
			continue;		/* extended prefix */

		next = keybuf[(keybuf_tail - 2) & (KEYBUF_SIZE - 1)];

		if (next == 0xe1)
			continue;		/* pause sequence */

		if (k == 0xc5 && next == 0x9d)
		{
			Key_Event (K_PAUSE, true);
			continue;
		}

	/* the fake shifts around the extended keypad */
		if ((k & 0x7f) == 0x2a || (k & 0x7f) == 0x36)
		{
			if (next == 0xe0)
				continue;
			k &= 0x80;
			k |= 0x36;
		}

		outkey = scantokey[k & 0x7f];
		if (!outkey)
			continue;

		Key_Event (outkey, (k & 0x80) ? false : true);
	}
}

/*
=============================================================================

  general

=============================================================================
*/

void Sys_MakeCodeWriteable (unsigned long startaddr, unsigned long length)
{
/* DOS/4GW runs flat with everything writeable; nothing to do. */
	(void)startaddr;
	(void)length;
}

void Sys_Error (char *error, ...)
{
	va_list	argptr;

	VID_Shutdown ();
	Sys_UnhookKeyboard ();

	printf ("Sys_Error: ");
	va_start (argptr, error);
	vprintf (error, argptr);
	va_end (argptr);
	printf ("\n");

	exit (1);
}

void Sys_Printf (char *fmt, ...)
{
	va_list	argptr;

	va_start (argptr, fmt);
	vprintf (fmt, argptr);
	va_end (argptr);
}

void Sys_Quit (void)
{
	Host_Shutdown ();
	VID_Shutdown ();
	Sys_UnhookKeyboard ();

	exit (0);
}

void Sys_Sleep (void)
{
}

char *Sys_ConsoleInput (void)
{
	return NULL;
}

void Sys_DebugLog (char *file, char *fmt, ...)
{
	va_list	argptr;
	static char	data[1024];
	FILE		*fp;

	va_start (argptr, fmt);
	vsprintf (data, fmt, argptr);
	va_end (argptr);

	fp = fopen (file, "at");
	if (fp)
	{
		fwrite (data, strlen(data), 1, fp);
		fclose (fp);
	}
}

void Sys_LowFPPrecision (void)
{
}

void Sys_HighFPPrecision (void)
{
}

void Sys_Init (void)
{
}

/*
=============================================================================

  entry point

=============================================================================
*/

int main (int argc, char **argv)
{
	double	time, oldtime, newtime;
	int	memsize;
	void	*membase;

	printf ("nv1Quake %2.2f (Quake %4.2f) -- NVIDIA NV1 / Diamond Edge 3D\n",
		(float)NV1QUAKE_VERSION, (float)VERSION);

	COM_InitArgv (argc, argv);

	parms.argc = com_argc;
	parms.argv = com_argv;

	getcwd (cwd, sizeof(cwd) - 1);
	if (cwd[strlen(cwd) - 1] == '\\' || cwd[strlen(cwd) - 1] == '/')
		cwd[strlen(cwd) - 1] = 0;
	parms.basedir = cwd;

	if (COM_CheckParm ("-basedir"))
		parms.basedir = com_argv[COM_CheckParm ("-basedir") + 1];

/*
 * Heap.  DOS/4GW hands out extended memory, so this is just a malloc; 16MB is
 * comfortable and Quake will run in 8.
 */
	memsize = 16 * 1024 * 1024;
	if (COM_CheckParm ("-mem"))
		memsize = Q_atoi (com_argv[COM_CheckParm ("-mem") + 1]) * 1024 * 1024;

	membase = malloc (memsize);
	while (!membase && memsize > 6 * 1024 * 1024)
	{
		memsize -= 1024 * 1024;
		membase = malloc (memsize);
	}
	if (!membase)
		Sys_Error ("Not enough memory -- nv1Quake needs at least 6MB free");

	parms.membase = membase;
	parms.memsize = memsize;

	printf ("%d MB heap\n", memsize / (1024 * 1024));

	isDedicated = (COM_CheckParm ("-dedicated") != 0);

	Sys_FloatTime ();		/* establish the time base */

	if (!isDedicated)
		Sys_HookKeyboard ();

	Host_Init (&parms);

	oldtime = Sys_FloatTime ();

	while (1)
	{
		newtime = Sys_FloatTime ();
		time = newtime - oldtime;

		if (cls.state == ca_dedicated && (time < sys_ticrate.value))
			continue;

		Host_Frame (time);

		oldtime = newtime;
	}

	return 0;
}
