/*
nv1_errno.h -- force-included when building net_wins.c and net_wipx.c only.

Those two files declare a local `int errno = pWSAGetLastError();`, which was
fine in 1996 when errno was a plain extern int.  In a modern CRT errno is a
macro expanding to (*_errno()), so the declaration becomes nonsense.

Pulling in the CRT headers here first, and only then undefining the macro,
gives those two files the 1996 meaning of the identifier without disturbing
anything else -- the inline CRT functions that use errno have already been
compiled by that point, and sys_win.c, which wants the real errno, is built
without this header.

This exists so that nothing under WinQuake has to be edited.
*/

#ifndef NV1_ERRNO_H
#define NV1_ERRNO_H

#include <windows.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <setjmp.h>
#include <time.h>

#undef errno

#endif /* NV1_ERRNO_H */
