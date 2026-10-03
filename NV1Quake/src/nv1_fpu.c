/*
nv1_fpu.c -- floating point control word helpers.

sys_win.c calls these four, and provides them itself only under #ifndef
_M_IX86; on x86 it expects the versions in sys_wina.s.  We do not assemble
sys_wina.s, so these are id's own non-x86 definitions, verbatim in behaviour:
empty.

What they did was set the x87 to single precision and mask exceptions, which
mattered when Quake's fixed point paths depended on the exact rounding of the
387 stack.  With id386 off, none of that code is compiled, and the C paths do
not care what the control word says.

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

void Sys_SetFPCW (void)
{
}

void Sys_PushFPCW_SetHigh (void)
{
}

void Sys_PopFPCW (void)
{
}

void MaskExceptions (void)
{
}
