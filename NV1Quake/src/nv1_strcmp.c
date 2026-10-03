/*
nv1_strcmp.c -- DOS build only.

NVRM.LIB (the NVIDIA DOS resource manager) carries a module, osinit.c, that
defines its own strcmp_ for private use.  Because that module is pulled in to
satisfy the resource manager entry points, the linker binds EVERY strcmp in the
program to NVIDIA's version instead of Watcom's clib one, and that version does
not behave like the standard function: Quake's COM_FindFile matched
"gfx/pop.lmp" against the first entry of pak1.pak and died with "Corrupted data
file".

An object file named on the link line wins over a library definition, so
defining the standard function here makes the whole program use a correct one.
string.h is deliberately not included, so nothing can rename or inline it.
*/

int strcmp (const char *s1, const char *s2)
{
	while (*s1 && *s1 == *s2)
	{
		s1++;
		s2++;
	}
	return (int)((unsigned char)*s1) - (int)((unsigned char)*s2);
}
