/*
nv1_boot.h -- start-up screen and memory planning for the DOS build.

The start-up steps print a labelled line each ("Detecting NV1 hardware ..... [ DONE ]")
so it is obvious what the program is doing, and where it stopped if it stops.
Every step is mirrored into NV1DIAG.LOG when -diag is on.

Boot_PlanHeap is plain arithmetic with no DOS calls, so it is unit tested on the
host (tests/boot_heap_test.c).  Everything else is DOS only.
*/

#ifndef NV1_BOOT_H
#define NV1_BOOT_H

#define BOOT_UNKNOWN_MEM	0xffffffffUL	/* DPMI would not say */

#define BOOT_HEAP_MAX_MB	16	/* what the engine uses given the room */
#define BOOT_HEAP_MIN_MB	6	/* below this Quake will not start */
#define BOOT_TEXTURE_MB		5	/* kept free for textures, which are
					   malloc'd outside the engine heap */
#define BOOT_TEXTURE_TIGHT_MB	4	/* less than this and big maps will fail */

typedef struct
{
	int	heap_mb;	/* size to ask for */
	int	texture_mb;	/* what that leaves for textures, -1 if unknown */
	int	tight;		/* 1 if the texture room is likely too small */
	int	from_user;	/* 1 if -mem chose the size */
} nv1_heapplan_t;

/*
 * largest_bytes is the largest free block DPMI reported (or BOOT_UNKNOWN_MEM).
 * user_mb is the -mem argument, or 0 for automatic.
 */
void Boot_PlanHeap (unsigned long largest_bytes, int user_mb, nv1_heapplan_t *plan);

#ifdef DOS

#define BOOT_DONE	0
#define BOOT_WARN	1
#define BOOT_FAILED	2

void		Boot_Banner (double version);
void		Boot_Step (const char *label);		/* "label ....." with no newline */
void		Boot_Done (int status, const char *detail);	/* "[ DONE ] detail\n" */
void		Boot_Note (int status, const char *text);	/* a coloured line on its own */
void		Boot_Launch (void);

/* 1 found (desc filled in), 0 not on the PCI bus, -1 no PCI BIOS */
int		Boot_DetectNV1 (char *desc, int desclen);

/* largest free block in bytes, or BOOT_UNKNOWN_MEM */
unsigned long	Boot_QueryRam (void);

#endif

#endif
