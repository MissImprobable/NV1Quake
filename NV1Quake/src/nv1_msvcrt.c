/*
nv1_msvcrt.c -- the handful of Microsoft C runtime symbols that nvlib.lib
needs, supplied for a build that is not using Microsoft C.

Copyright (C) 2026 the nv1Quake port.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
Public License for more details.


NVIDIA shipped nvlib.lib compiled with Microsoft C, and there is no source
level way around that: it is the library, and it carries references into the
Microsoft runtime.  Building nv1Quake with Open Watcom means satisfying those
references by hand.

Watcom's own headers declare the C library with explicit pragmas, so -ecc
changes the convention of OUR functions but leaves the runtime on Watcom's
register convention.  That is why the Microsoft-style names below are needed
only by nvlib and never collide with anything: Watcom's sprintf is `sprintf_`,
Microsoft's is `_sprintf`, and they can coexist.

`#pragma aux <name> "<literal>"` is what fixes each emitted symbol name.

  _sprintf, _vsprintf   forwarded to Watcom's.
  __mkdir               Microsoft spells it _mkdir, Watcom spells it mkdir.
  __fltused             A marker.  Microsoft C references it from any object
                        that touches floating point so the linker pulls in
                        float support; the value is never read.  Data symbols
                        get one leading underscore from Watcom, so the C name
                        here is one underscore shorter than the symbol.
  __adjust_fdiv         The Pentium FDIV workaround flag.  Microsoft code
                        tests it and takes a corrected path when non-zero.
                        Zero means "this CPU divides correctly", true of
                        everything except the recalled 60 and 66 MHz Pentiums.
  __adj_fdiv_r          The corrected division helpers, reached only when
  __adj_fdivr_m32       __adjust_fdiv is non-zero, so with the flag at zero
                        they are referenced and never called.  If this ever
                        runs on an actual FDIV-bug Pentium, revisit these.
  __ftol                Microsoft's double-to-long.  Genuinely called, so it
                        has to be real.  Its argument arrives on the x87 stack
                        rather than the C stack and the result comes back in
                        EDX:EAX -- which `parm [8087] value [edx eax]`
                        describes exactly, so no assembly is needed.
*/

#ifdef NV1_HARDWARE
#ifdef __WATCOMC__

#include <stdio.h>
#include <stdarg.h>
#include <direct.h>

/*-------------------------------------------------------------- markers ---*/

/* emitted as __fltused and __adjust_fdiv; Watcom prefixes data with '_' */
int	_fltused = 0x9875;
int	_adjust_fdiv = 0;

/*------------------------------------------------------------- printf -----*/

int msvc_vsprintf (char *buf, const char *fmt, va_list ap);
#pragma aux msvc_vsprintf "_vsprintf";

int msvc_vsprintf (char *buf, const char *fmt, va_list ap)
{
	return vsprintf (buf, fmt, ap);
}

int msvc_sprintf (char *buf, const char *fmt, ...);
#pragma aux msvc_sprintf "_sprintf";

int msvc_sprintf (char *buf, const char *fmt, ...)
{
	va_list	ap;
	int	ret;

	va_start (ap, fmt);
	ret = vsprintf (buf, fmt, ap);
	va_end (ap);

	return ret;
}

/*-------------------------------------------------------------- mkdir -----*/

int msvc_mkdir (const char *path);
#pragma aux msvc_mkdir "__mkdir";

int msvc_mkdir (const char *path)
{
	return mkdir (path);
}

/*--------------------------------------------------------------- ftol -----*/

__int64 msvc_ftol (double x);
#pragma aux msvc_ftol "__ftol" parm [8087] value [edx eax] modify [8087];

__int64 msvc_ftol (double x)
{
	return (__int64)x;
}

/*----------------------------------------------------------- fdiv patch ---*/

void msvc_adj_fdiv_r (void);
#pragma aux msvc_adj_fdiv_r "__adj_fdiv_r";

void msvc_adj_fdiv_r (void)
{
}

void msvc_adj_fdivr_m32 (void);
#pragma aux msvc_adj_fdivr_m32 "__adj_fdivr_m32";

void msvc_adj_fdivr_m32 (void)
{
}

#endif	/* __WATCOMC__ */
#endif	/* NV1_HARDWARE */
