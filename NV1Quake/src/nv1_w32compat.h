/*
nv1_w32compat.h -- force-included when building sys_win.c with Open Watcom.

Sys_ConsoleInput declares its counts as plain `int` and passes their addresses
to Win32 calls that want LPDWORD:

	GetNumberOfConsoleInputEvents (hinput, &numevents);
	ReadConsoleInput (hinput, recs, 1, &numread);
	WriteFile (houtput, "\r\n", 2, &dummy, NULL);

MSVC of the period let that through with a warning.  Watcom makes it a hard
error (E1176, parameter pointer type mismatch), which is the more correct
reading of the language.

Both types are 32 bits and the sign is irrelevant for a count the API only
writes, so the fix is a cast.  Rather than edit id's file, the three calls are
shadowed by function-like macros of the same name.  A macro is not re-expanded
while its own replacement is being rescanned, so each of these expands to a
single ordinary call to the real function.
*/

#ifndef NV1_W32COMPAT_H
#define NV1_W32COMPAT_H

#include <windows.h>

#undef GetNumberOfConsoleInputEvents
#define GetNumberOfConsoleInputEvents(h, n) \
	GetNumberOfConsoleInputEvents ((h), (LPDWORD)(n))

#undef ReadConsoleInput
#define ReadConsoleInput(h, b, l, n) \
	ReadConsoleInputA ((h), (b), (l), (LPDWORD)(n))

#undef WriteFile
#define WriteFile(h, b, l, n, o) \
	WriteFile ((h), (b), (l), (LPDWORD)(n), (o))

#endif /* NV1_W32COMPAT_H */
