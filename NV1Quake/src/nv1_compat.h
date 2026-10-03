/*
nv1_compat.h -- force-included into every translation unit.

quakedef.h decides whether Quake's hand-written assembly is in play with

	#if defined __i386__
	#define id386	1

and everything guarded by `#if !id386` is the C equivalent of a .s file.  Those
.s files are GAS syntax, meant for the DOS and Linux builds; the Win32 build
assembled them through gas2masm and we do not, so nv1Quake wants the C paths.

The catch is that this compiler still predefines __i386__ in a 32 bit build, so
id386 comes out 1 and the C fallbacks vanish -- BoxOnPlaneSide,
SV_HullPointContents, Snd_WriteLinearBlastStereo16 and friends all become
undefined at link time.  /U cannot remove a compiler-predefined macro, but an
undef at the top of the translation unit can, and that is all this file is.

Nothing in the Windows or CRT headers looks at __i386__ (they use _M_IX86), so
removing it changes only Quake's own choice.
*/

#ifndef NV1_COMPAT_H
#define NV1_COMPAT_H

#undef __i386__

#endif /* NV1_COMPAT_H */
