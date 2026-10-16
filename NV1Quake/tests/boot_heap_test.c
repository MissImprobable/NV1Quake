/*
Host-side test for Boot_PlanHeap (src/nv1_boot.c).  Not part of any game build.

    gcc -I src -o %TEMP%/boot_heap_test tests/boot_heap_test.c src/nv1_boot.c && %TEMP%/boot_heap_test
*/

#include <stdio.h>
#include "nv1_boot.h"

#define MB	(1024UL * 1024UL)

static int failures;

static void Check (const char *name, unsigned long largest, int user,
	int want_heap, int want_tex, int want_tight)
{
	nv1_heapplan_t	p;

	Boot_PlanHeap (largest, user, &p);
	if (p.heap_mb != want_heap || p.texture_mb != want_tex || p.tight != want_tight)
	{
		printf ("FAIL %s: heap %d (want %d), textures %d (want %d), tight %d (want %d)\n",
			name, p.heap_mb, want_heap, p.texture_mb, want_tex, p.tight, want_tight);
		failures++;
	}
	else
		printf ("ok   %s\n", name);
}

int main (void)
{
	Check ("plenty of RAM caps at the 16 MB default", 64 * MB, 0, 16, 48, 0);
	Check ("exactly enough for the cap plus reserve", 21 * MB, 0, 16, 5, 0);
	Check ("a 16 MB machine, 12 MB free", 12 * MB, 0, 7, 5, 0);
	Check ("11 MB free", 11 * MB, 0, 6, 5, 0);
	Check ("10 MB free: heap floors at 6, textures get 4", 10 * MB, 0, 6, 4, 0);
	Check ("9 MB free: tight", 9 * MB, 0, 6, 3, 1);
	Check ("5 MB free: heap cannot fit, still planned at the floor", 5 * MB, 0, 6, 0, 1);
	Check ("unknown RAM falls back to the default", BOOT_UNKNOWN_MEM, 0, 16, -1, 0);
	Check ("-mem 8 is honoured", 12 * MB, 8, 8, 4, 0);
	Check ("-mem 8 on 10 MB free is tight", 10 * MB, 8, 8, 2, 1);
	Check ("-mem bigger than free is clamped to free", 10 * MB, 20, 10, 0, 1);
	Check ("-mem 1 is raised to 4", 12 * MB, 1, 4, 8, 0);
	Check ("-mem with unknown RAM is passed through", BOOT_UNKNOWN_MEM, 12, 12, -1, 0);

	printf (failures ? "%d FAILED\n" : "all passed\n", failures);
	return failures != 0;
}
