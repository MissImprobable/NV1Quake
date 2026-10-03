/*
nv1_indos.c -- DOS mouse input for nv1Quake, via INT 33h.

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


The keyboard lives in nv1_sysdos.c, because it is an interrupt handler and
belongs with the rest of the system layer.  This is just the mouse.

INT 33h function 0Bh returns motion counters since the last call and clears
them, which is exactly the relative-motion model Quake wants -- no cursor, no
clamping to a screen rectangle, no windowed-mouse problem.  DOS is much simpler
than Windows here.
*/

#include <dos.h>
#include <string.h>

#include "quakedef.h"
#include "nv1quake.h"

cvar_t	m_filter = {"m_filter", "0"};

static qboolean	mouse_avail;
static int	mouse_buttons;
static int	mouse_oldbuttonstate;

static float	mouse_x, mouse_y;
static float	old_mouse_x, old_mouse_y;

/*
===========
IN_Init
===========
*/
void IN_Init (void)
{
	union REGS	regs;

	Cvar_RegisterVariable (&m_filter);

	if (COM_CheckParm ("-nomouse"))
		return;

/* function 0: reset driver and read status */
	regs.w.ax = 0;
	int386 (0x33, &regs, &regs);

	if (regs.w.ax != 0xffff)
	{
		Con_Printf ("No mouse driver found\n");
		return;
	}

	mouse_buttons = regs.w.bx;
	if (mouse_buttons == 3 || mouse_buttons == 0xffff)
		mouse_buttons = 3;
	if (mouse_buttons > 3)
		mouse_buttons = 3;

	mouse_avail = true;

	Con_Printf ("Mouse: %d buttons\n", mouse_buttons);
}

void IN_Shutdown (void)
{
	mouse_avail = false;
}

/*
===========
IN_Commands

Button state.  INT 33h function 3 gives the current button mask.
===========
*/
void IN_Commands (void)
{
	union REGS	regs;
	int		buttonstate;
	int		i;

	if (!mouse_avail)
		return;

	regs.w.ax = 3;
	int386 (0x33, &regs, &regs);
	buttonstate = regs.w.bx;

	for (i = 0 ; i < mouse_buttons ; i++)
	{
		if ((buttonstate & (1 << i)) && !(mouse_oldbuttonstate & (1 << i)))
			Key_Event (K_MOUSE1 + i, true);

		if (!(buttonstate & (1 << i)) && (mouse_oldbuttonstate & (1 << i)))
			Key_Event (K_MOUSE1 + i, false);
	}

	mouse_oldbuttonstate = buttonstate;
}

/*
===========
IN_MouseMove
===========
*/
static void IN_MouseMove (usercmd_t *cmd)
{
	union REGS	regs;
	int		mx, my;

	if (!mouse_avail)
		return;

/* function 0Bh: read motion counters and clear them */
	regs.w.ax = 0x0b;
	int386 (0x33, &regs, &regs);

	mx = (short)regs.w.cx;
	my = (short)regs.w.dx;

	if (m_filter.value)
	{
		mouse_x = (mx + old_mouse_x) * 0.5;
		mouse_y = (my + old_mouse_y) * 0.5;
	}
	else
	{
		mouse_x = mx;
		mouse_y = my;
	}

	old_mouse_x = mx;
	old_mouse_y = my;

	mouse_x *= sensitivity.value;
	mouse_y *= sensitivity.value;

/* add mouse X/Y movement to cmd */
	if ((in_strafe.state & 1) || (lookstrafe.value && (in_mlook.state & 1)))
		cmd->sidemove += m_side.value * mouse_x;
	else
		cl.viewangles[YAW] -= m_yaw.value * mouse_x;

	if (in_mlook.state & 1)
		V_StopPitchDrift ();

	if ((in_mlook.state & 1) && !(in_strafe.state & 1))
	{
		cl.viewangles[PITCH] += m_pitch.value * mouse_y;

		if (cl.viewangles[PITCH] > 80)
			cl.viewangles[PITCH] = 80;
		if (cl.viewangles[PITCH] < -70)
			cl.viewangles[PITCH] = -70;
	}
	else
	{
		if ((in_strafe.state & 1) && noclip_anglehack)
			cmd->upmove -= m_forward.value * mouse_y;
		else
			cmd->forwardmove -= m_forward.value * mouse_y;
	}
}

/*
===========
IN_Move
===========
*/
void IN_Move (usercmd_t *cmd)
{
	IN_MouseMove (cmd);
}
