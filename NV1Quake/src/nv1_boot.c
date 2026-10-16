/*
nv1_boot.c -- start-up screen and memory planning for the DOS build.

Why the NV1 is found with a PCI scan and not by asking the driver: the driver
call (NvRequestVideoMode) switches the screen to graphics mode, so a DONE or
FAILED printed after it would never be seen.  The PCI BIOS leaves the screen
alone.  A miss is only a warning, because a board on a non-PCI bus will not
show up there; the driver is still asked later, and it has the last word.

Why the heap is sized from free memory: the engine heap used to grab 16 MB, or
whatever was left if that failed, and textures (which are malloc'd on their own,
outside the heap) then had nothing to live in.  On a 16 MB machine that ran out
of memory part way through loading the first map.  Now a texture reserve is held
back before the heap is sized.

Colour is written with the BIOS (int 10h, function 09h) so it works in any
text mode and needs no video memory pointer.
*/

#include <stdio.h>
#include <string.h>

#include "nv1_boot.h"
#include "nv1_diag.h"

#define MB	(1024UL * 1024UL)

/*
=================
Boot_PlanHeap
=================
*/
void Boot_PlanHeap (unsigned long largest_bytes, int user_mb, nv1_heapplan_t *plan)
{
	int	largest_mb;
	int	heap;

	plan->tight = 0;
	plan->from_user = (user_mb > 0);
	plan->texture_mb = -1;

	if (largest_bytes == BOOT_UNKNOWN_MEM)
	{
	/* no figure to plan from: ask for the old default and let malloc decide */
		plan->heap_mb = user_mb > 0 ? user_mb : BOOT_HEAP_MAX_MB;
		return;
	}

	largest_mb = (int)(largest_bytes / MB);

	if (user_mb > 0)
	{
		heap = user_mb;
		if (heap < 4)
			heap = 4;
		if (heap > largest_mb)
			heap = largest_mb;
	}
	else
	{
		heap = largest_mb - BOOT_TEXTURE_MB;
		if (heap > BOOT_HEAP_MAX_MB)
			heap = BOOT_HEAP_MAX_MB;
		if (heap < BOOT_HEAP_MIN_MB)
			heap = BOOT_HEAP_MIN_MB;
	}

	plan->heap_mb = heap;
	plan->texture_mb = largest_mb - heap;
	if (plan->texture_mb < 0)
		plan->texture_mb = 0;
	plan->tight = (plan->texture_mb < BOOT_TEXTURE_TIGHT_MB);
}

#ifdef DOS

#include <stdlib.h>
#include <malloc.h>
#include <dos.h>

#define ATTR_GREY	0x07
#define ATTR_WHITE	0x0f
#define ATTR_GREEN	0x0a
#define ATTR_CYAN	0x0b
#define ATTR_RED	0x0c
#define ATTR_YELLOW	0x0e

#define BANNER_W	44	/* inside the box */
#define LABEL_W		28	/* label plus dots, so the tags line up */

static int Boot_Page (void)
{
	union REGS	r;

	memset (&r, 0, sizeof(r));
	r.h.ah = 0x0f;
	int386 (0x10, &r, &r);
	return r.h.bh;
}

/* one character in a colour at the cursor, then the cursor moves on */
static void Boot_PutChar (unsigned char ch, int attr)
{
	union REGS	r;
	int		page = Boot_Page ();
	int		row, col;

	fflush (stdout);

	memset (&r, 0, sizeof(r));
	r.h.ah = 0x03;
	r.h.bh = (unsigned char)page;
	int386 (0x10, &r, &r);
	row = r.h.dh;
	col = r.h.dl;

	memset (&r, 0, sizeof(r));
	r.h.ah = 0x09;
	r.h.al = ch;
	r.h.bh = (unsigned char)page;
	r.h.bl = (unsigned char)attr;
	r.w.cx = 1;
	int386 (0x10, &r, &r);

	memset (&r, 0, sizeof(r));
	r.h.ah = 0x02;
	r.h.bh = (unsigned char)page;
	r.h.dh = (unsigned char)row;
	r.h.dl = (unsigned char)(col + 1);
	int386 (0x10, &r, &r);
}

static void Boot_PutStr (const char *s, int attr)
{
	while (*s)
		Boot_PutChar ((unsigned char)*s++, attr);
}

static void Boot_BoxLine (unsigned char left, const char *text, unsigned char right)
{
	int	len, pad, i;

	printf ("  ");
	Boot_PutChar (left, ATTR_CYAN);
	if (text)
	{
		len = (int)strlen (text);
		pad = (BANNER_W - len) / 2;
		for (i = 0 ; i < pad ; i++)
			Boot_PutChar (' ', ATTR_GREY);
		Boot_PutStr (text, ATTR_WHITE);
		for (i = 0 ; i < BANNER_W - pad - len ; i++)
			Boot_PutChar (' ', ATTR_GREY);
	}
	else
	{
		for (i = 0 ; i < BANNER_W ; i++)
			Boot_PutChar (0xcd, ATTR_CYAN);
	}
	Boot_PutChar (right, ATTR_CYAN);
	printf ("\n");
}

void Boot_Banner (double version)
{
	char	title[BANNER_W + 1];

	sprintf (title, "nv1Quake %.2f", version);

	printf ("\n");
	Boot_BoxLine (0xc9, NULL, 0xbb);
	Boot_BoxLine (0xba, title, 0xba);
	Boot_BoxLine (0xba, "NVIDIA NV1 / Diamond Edge 3D / STG2000", 0xba);
	Boot_BoxLine (0xc8, NULL, 0xbc);
	printf ("\n");

	DIAG_LOG (("boot: nv1Quake %.2f", version));
}

static const char	*boot_label = "";

void Boot_Step (const char *label)
{
	int	n = (int)strlen (label);

	boot_label = label;
	printf ("  %s ", label);
	for (n += 1 ; n < LABEL_W ; n++)
		putchar ('.');
	putchar (' ');
	fflush (stdout);
}

static void Boot_Tag (int status)
{
	switch (status)
	{
	case BOOT_DONE:
		Boot_PutStr ("[ DONE ]", ATTR_GREEN);
		break;
	case BOOT_WARN:
		Boot_PutStr ("[ WARN ]", ATTR_YELLOW);
		break;
	default:
		Boot_PutStr ("[FAILED]", ATTR_RED);
		break;
	}
}

void Boot_Done (int status, const char *detail)
{
	static const char *names[] = { "DONE", "WARN", "FAILED" };

	Boot_Tag (status);
	if (detail && detail[0])
		printf ("  %s", detail);
	printf ("\n");
	fflush (stdout);

	DIAG_LOG (("boot: %s -> %s %s", boot_label, names[status > 2 ? 2 : status],
		detail ? detail : ""));
}

void Boot_Note (int status, const char *text)
{
	printf ("    ");
	Boot_PutStr (text, status == BOOT_FAILED ? ATTR_RED : ATTR_YELLOW);
	printf ("\n");
	fflush (stdout);

	DIAG_LOG (("boot: note: %s", text));
}

void Boot_Launch (void)
{
	printf ("\n  ");
	Boot_PutStr ("LAUNCHING!", ATTR_GREEN);
	printf ("\n\n");
	fflush (stdout);

	DIAG_LOG (("boot: LAUNCHING"));
}

/*
=================
Boot_DetectNV1

PCI BIOS (int 1Ah, AH=B1h).  NVIDIA's NV1 is 12D2:0008; the STG2000 Diamond
used carries SGS-Thomson's 104A id.  Same ids as the diagnostic log scans for.
=================
*/
int Boot_DetectNV1 (char *desc, int desclen)
{
	static const struct
	{
		unsigned	vendor;
		unsigned	device;
		const char	*name;
	} ids[] = {
		{ 0x12d2, 0x0008, "NVIDIA NV1" },
		{ 0x12d2, 0x0009, "NVIDIA NV1" },
		{ 0x104a, 0x0008, "SGS-Thomson STG2000" },
		{ 0x104a, 0x0009, "SGS-Thomson STG2000" },
	};
	union REGS	r;
	int		i;
	char		buf[96];

	memset (&r, 0, sizeof(r));
	r.w.ax = 0xb101;
	int386 (0x1a, &r, &r);
	if (r.x.cflag || r.h.ah != 0)
		return -1;

	for (i = 0 ; i < 4 ; i++)
	{
		memset (&r, 0, sizeof(r));
		r.w.ax = 0xb102;
		r.w.cx = (unsigned short)ids[i].device;
		r.w.dx = (unsigned short)ids[i].vendor;
		r.w.si = 0;
		int386 (0x1a, &r, &r);
		if (r.x.cflag || r.h.ah != 0)
			continue;

		sprintf (buf, "%s %04X:%04X @ PCI %d:%d", ids[i].name,
			ids[i].vendor, ids[i].device, r.h.bh, r.h.bl >> 3);
		strncpy (desc, buf, desclen - 1);
		desc[desclen - 1] = 0;
		return 1;
	}
	return 0;
}

/*
=================
Boot_QueryRam

DPMI 0500h.  The first dword is the largest free block, which is the number the
heap has to fit inside.  It is 0xFFFFFFFF when the host will not say.
=================
*/
unsigned long Boot_QueryRam (void)
{
	union REGS	r;
	struct SREGS	s;
	unsigned long	mi[12];

	memset (mi, 0xff, sizeof(mi));
	memset (&r, 0, sizeof(r));
	segread (&s);
	r.w.ax = 0x0500;
	r.x.edi = (unsigned)mi;
	int386x (0x31, &r, &r, &s);
	if (r.x.cflag || mi[0] == BOOT_UNKNOWN_MEM)
		return BOOT_UNKNOWN_MEM;

/*
 * Memory the C library already holds but is not using is just as available to
 * malloc as memory still with DPMI (the -diag malloc probe leaves about 8 MB
 * there), so count the largest free block in it too.  Without this the figure
 * would change just because -diag was on.
 */
	return mi[0] + (unsigned long)_memmax ();
}

#endif	/* DOS */
