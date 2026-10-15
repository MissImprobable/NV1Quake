/*
nv1_diag.c -- opt-in diagnostic logging, shared by the DOS, Windows 95 and
modern Windows builds.  See nv1_diag.h.

This file deliberately includes no Quake headers: it has to work before the
engine is up, and the Windows headers clash with Quake's typedefs.  The engine
tells it things through the DIAG_ macros instead.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <signal.h>
#include <malloc.h>

#ifdef _WIN32
#include <windows.h>
#endif

#ifdef DOS
#include <dos.h>
#include <direct.h>
#include <i86.h>
#endif

#include "nv1_diag.h"

int		diag_enabled;
int		diag_verbose;

#define DIAG_FILE		"NV1DIAG.LOG"
#define DIAG_MAX_START		(512L * 1024L)		/* start fresh above this */
#define DIAG_MAX_TOTAL		(4L * 1024L * 1024L)	/* stop logging at this */
#define DIAG_LINE		8192

static char		diag_buf[DIAG_LINE];
static long		diag_bytes;
static int		diag_full;
static int		diag_clean;
static int		diag_frames;
static long		diag_lastbeat;
static unsigned long	diag_start;

/* ---- clock ---------------------------------------------------------- */

static unsigned long Diag_Ticks (void)		/* milliseconds */
{
#ifdef _WIN32
	return GetTickCount ();
#else
	return (unsigned long)(((double)clock () * 1000.0) / (double)CLOCKS_PER_SEC);
#endif
}

/* ---- output --------------------------------------------------------- */

static void Diag_Write (const char *s)
{
	FILE	*f;

	if (diag_full)
		return;

	diag_bytes += (long)strlen (s);
	if (diag_bytes > DIAG_MAX_TOTAL)
	{
		diag_full = 1;
		s = "*** log size limit reached, no further output ***\n";
	}

	f = fopen (DIAG_FILE, "a");
	if (!f)
		return;
	fputs (s, f);
	fclose (f);
}

void Diag_Log (const char *fmt, ...)
{
	va_list		argptr;
	char		*p;
	unsigned long	ms;

	if (!diag_enabled)
		return;

	ms = Diag_Ticks () - diag_start;
	sprintf (diag_buf, "[%7lu.%03lu] ", ms / 1000UL, ms % 1000UL);
	p = diag_buf + strlen (diag_buf);

	va_start (argptr, fmt);
	vsprintf (p, fmt, argptr);
	va_end (argptr);

	if (strlen (diag_buf) > DIAG_LINE - 4)
		diag_buf[DIAG_LINE - 4] = 0;
	strcat (diag_buf, "\n");
	Diag_Write (diag_buf);
}

/* console text arrives with its own newlines, possibly several lines at once */
void Diag_Console (const char *text)
{
	char	line[512];
	int	n;

	if (!diag_enabled || !text)
		return;

	while (*text)
	{
		n = 0;
		while (*text && *text != '\n' && n < (int)sizeof(line) - 1)
			line[n++] = *text++;
		line[n] = 0;
		if (*text == '\n')
			text++;
		if (n)
			Diag_Log ("CON| %s", line);
	}
}

/* ---- memory --------------------------------------------------------- */

static void Diag_MemLine (void)
{
#ifdef DOS
	union REGS	r;
	struct SREGS	s;
	unsigned long	mi[12];

	memset (mi, 0xff, sizeof(mi));
	memset (&r, 0, sizeof(r));
	segread (&s);
	r.w.ax = 0x0500;
	r.x.edi = (unsigned)mi;
	int386x (0x31, &r, &r, &s);
	Diag_Log ("mem: dpmi largest free block %lu KB, free pages %lu, "
		"free linear %lu pages | C heap avail %u KB, max block %u KB | "
		"stack free %u",
		mi[0] == 0xfffffffful ? 0xfffffffful : mi[0] / 1024UL,
		mi[5], mi[7],
		(unsigned)(_memavl () / 1024U), (unsigned)(_memmax () / 1024U),
		(unsigned)stackavail ());
#endif
#ifdef _WIN32
	MEMORYSTATUS	ms;

	ms.dwLength = sizeof(ms);
	GlobalMemoryStatus (&ms);
	Diag_Log ("mem: phys %lu/%lu KB free, pagefile %lu/%lu KB free, "
		"virtual %lu/%lu KB free, load %lu%%",
		(unsigned long)(ms.dwAvailPhys / 1024), (unsigned long)(ms.dwTotalPhys / 1024),
		(unsigned long)(ms.dwAvailPageFile / 1024), (unsigned long)(ms.dwTotalPageFile / 1024),
		(unsigned long)(ms.dwAvailVirtual / 1024), (unsigned long)(ms.dwTotalVirtual / 1024),
		(unsigned long)ms.dwMemoryLoad);
#endif
}

/* The engine's own Hunk/Zone sanity checks (they call Sys_Error on damage). */
extern void Hunk_Check (void);
extern void Z_CheckHeap (void);
int		diag_memready;		/* set by Memory_Init once the hunk exists */

void Diag_Heap (const char *where)
{
	int	hc;

	if (!diag_enabled)
		return;

#if defined(__WATCOMC__) || defined(_MSC_VER)
	hc = _heapchk ();
	Diag_Log ("heapcheck(%s): C heap status %d (%s)", where, hc,
#ifdef _HEAPOK
		hc == _HEAPOK || hc == _HEAPEMPTY ? "ok" : "DAMAGED");
#else
		"?");
#endif
#endif
	if (diag_memready)
	{
		Hunk_Check ();
		Z_CheckHeap ();
		Diag_Log ("heapcheck(%s): hunk and zone consistent", where);
	}
	(void)hc;
}

void Diag_Checkpoint (const char *name)
{
	Diag_Log ("CHECKPOINT %s", name);
	Diag_MemLine ();
}

/* ---- per-frame heartbeat -------------------------------------------- */

void Diag_Frame (void)
{
	long	now;

	diag_frames++;
	now = (long)Diag_Ticks ();

	if (diag_frames <= 5 || diag_frames == 10 || diag_frames == 25 ||
		diag_frames == 100 || now - diag_lastbeat >= 5000L)
	{
		diag_lastbeat = now;
		Diag_Log ("frame %d", diag_frames);
		if (diag_frames <= 2 || diag_frames % 100 == 0)
			Diag_MemLine ();
	}
}

/* ---- exit ----------------------------------------------------------- */

void Diag_Exit (const char *how, const char *msg)
{
	if (!diag_enabled)
		return;

	diag_clean = 1;
	Diag_Log ("EXIT %s%s%s (after %d frames)", how, msg ? ": " : "",
		msg ? msg : "", diag_frames);
	Diag_MemLine ();
}

/*
 * The Win95 build forces cdecl (-ecc) for the NVIDIA libraries, but Watcom's
 * own atexit()/signal() still expect callbacks in Watcom's convention.
 */
#ifdef __WATCOMC__
#define DIAG_CALLBACK	__watcall
#else
#define DIAG_CALLBACK
#endif

static void DIAG_CALLBACK Diag_AtExit (void)
{
	if (diag_enabled && !diag_clean)
		Diag_Log ("EXIT via exit() without Sys_Quit/Sys_Error (after %d frames)",
			diag_frames);
}

static void DIAG_CALLBACK Diag_Signal (int sig)
{
	const char	*name = "?";

	switch (sig)
	{
	case SIGSEGV: name = "SIGSEGV (bad memory access)"; break;
	case SIGILL:  name = "SIGILL (illegal instruction)"; break;
	case SIGFPE:  name = "SIGFPE (floating point / divide)"; break;
	case SIGABRT: name = "SIGABRT (abort)"; break;
	}
	Diag_Log ("*** SIGNAL %d %s after %d frames", sig, name, diag_frames);
	Diag_MemLine ();
	diag_clean = 1;
	signal (sig, SIG_DFL);
	raise (sig);		/* let the extender / OS show its own crash report */
}

/* ---- platform report ------------------------------------------------- */

static void Diag_SelfTest (void)
{
	static const char	*pairs[][2] = {
		{ "gfx/pop.lmp", "sound/misc/basekey.wav" },
		{ "gfx/pop.lmp", "gfx/pop.lmp" },
		{ "a", "b" }, { "b", "a" }, { "", "x" }, { "x", "" },
	};
	struct { char name[56]; int pos; int len; }	dpack;
	struct { char name[64]; int pos; int len; }	pack;
	double	d;
	float	f;
	int	i, r, bad;
	void	*p;
	static const int	sizes[] = { 64, 48, 32, 24, 16, 12, 8, 6, 4 };

	Diag_Log ("sizes: char %d short %d int %d long %d ptr %d float %d double %d",
		(int)sizeof(char), (int)sizeof(short), (int)sizeof(int),
		(int)sizeof(long), (int)sizeof(void *), (int)sizeof(float),
		(int)sizeof(double));
	Diag_Log ("struct strides: dpackfile-like %d (want 64), packfile-like %d (want 72)",
		(int)sizeof(dpack), (int)sizeof(pack));

	bad = 0;
	for (i = 0 ; i < 6 ; i++)
	{
		r = strcmp (pairs[i][0], pairs[i][1]);
		Diag_Log ("strcmp(\"%s\",\"%s\") = %d (%s)", pairs[i][0], pairs[i][1], r,
			(i == 1 ? r == 0 : (i == 3 || i == 5) ? r > 0 : r < 0) ? "ok" : "WRONG");
		if (!(i == 1 ? r == 0 : (i == 3 || i == 5) ? r > 0 : r < 0))
			bad++;
	}
	Diag_Log ("strcmp self-test: %s", bad ? "FAILED" : "passed");

	f = 1.0f / 3.0f;
	d = 2.0;
	Diag_Log ("float: 1/3=%.7f  (int)(255.999f)=%d  (int)(-1.5f)=%d  7/2.0=%.2f  1e10 as int64-safe=%.0f",
		f, (int)255.999f, (int)-1.5f, 7.0 / d, 1e10);

	/* how much can the C heap actually give us? */
	for (i = 0 ; i < (int)(sizeof(sizes) / sizeof(sizes[0])) ; i++)
	{
		p = malloc ((size_t)sizes[i] * 1024U * 1024U);
		Diag_Log ("malloc probe %2d MB: %s", sizes[i], p ? "ok" : "FAILED");
		if (p)
			free (p);
	}
}

#ifdef DOS
static void Diag_Env (const char *name)
{
	const char	*v = getenv (name);

	Diag_Log ("env %s=%s", name, v ? v : "(not set)");
}

/*
 * Is an NV1 on the PCI bus at all?  NVIDIA's NV1 is 12D2:0008 (the STG2000
 * variant that Diamond used carries SGS-Thomson's 104A id).  Uses the PCI BIOS
 * (INT 1Ah, AH=B1h); the extender reflects it to real mode, so only the 16 bit
 * halves of registers survive, which is all this needs.
 */
static unsigned Diag_PciReadWord (unsigned bus, unsigned devfn, unsigned reg)
{
	union REGS	r;

	memset (&r, 0, sizeof(r));
	r.w.ax = 0xb109;
	r.h.bh = (unsigned char)bus;
	r.h.bl = (unsigned char)devfn;
	r.w.di = (unsigned short)reg;
	int386 (0x1a, &r, &r);
	return r.x.cflag ? 0xffffu : (unsigned)r.w.cx;
}

static void Diag_PciScan (void)
{
	static const unsigned ids[][2] = {
		{ 0x12d2, 0x0008 }, { 0x12d2, 0x0009 }, { 0x104a, 0x0008 }, { 0x104a, 0x0009 },
	};
	union REGS	r;
	int		i, found = 0;
	unsigned	bus, devfn;

	memset (&r, 0, sizeof(r));
	r.w.ax = 0xb101;
	int386 (0x1a, &r, &r);
	Diag_Log ("PCI BIOS (1Ah/B101h): AH=%02Xh carry=%d (AH=00h means present)", r.h.ah, r.x.cflag ? 1 : 0);

	for (i = 0 ; i < 4 ; i++)
	{
		memset (&r, 0, sizeof(r));
		r.w.ax = 0xb102;
		r.w.cx = (unsigned short)ids[i][1];
		r.w.dx = (unsigned short)ids[i][0];
		r.w.si = 0;
		int386 (0x1a, &r, &r);
		if (r.x.cflag || r.h.ah != 0)
			continue;

		found++;
		bus = r.h.bh;
		devfn = r.h.bl;
		Diag_Log ("PCI: found %04X:%04X at bus %u device %u function %u", ids[i][0], ids[i][1],
			bus, devfn >> 3, devfn & 7);
		Diag_Log ("PCI:   command %04Xh status %04Xh, BAR0 %04X%04Xh BAR1 %04X%04Xh, IRQ line %02Xh",
			Diag_PciReadWord (bus, devfn, 4), Diag_PciReadWord (bus, devfn, 6),
			Diag_PciReadWord (bus, devfn, 0x12), Diag_PciReadWord (bus, devfn, 0x10),
			Diag_PciReadWord (bus, devfn, 0x16), Diag_PciReadWord (bus, devfn, 0x14),
			Diag_PciReadWord (bus, devfn, 0x3c) & 0xff);
	}
	if (!found)
		Diag_Log ("PCI: no NV1 (12D2:0008/0009 or 104A:0008/0009) found by the PCI BIOS "
			"- fine on a non-PCI (VLB/ISA) board or when the BIOS call is unavailable");
}

static void Diag_PlatformDos (void)
{
	union REGS	r;
	struct SREGS	s;
	unsigned long	mi[12];

	memset (&r, 0, sizeof(r));
	r.h.ah = 0x30;
	int386 (0x21, &r, &r);
	Diag_Log ("DOS reports version %d.%d (OEM %02Xh)", r.h.al, r.h.ah, r.h.bh);

	memset (&r, 0, sizeof(r));
	r.w.ax = 0x3306;
	int386 (0x21, &r, &r);
	Diag_Log ("DOS true version (3306h): AL=%02Xh BL.BH=%d.%d rev %d flags %02Xh "
		"(AL=FFh means DOS < 5)", r.h.al, r.h.bl, r.h.bh, r.h.dl, r.h.dh);

	memset (&r, 0, sizeof(r));
	r.w.ax = 0x1600;
	int386 (0x2f, &r, &r);
	Diag_Log ("Windows check (2Fh/1600h): AL=%02Xh AH=%02Xh (00h/80h = not running under Windows)",
		r.h.al, r.h.ah);

	memset (&r, 0, sizeof(r));
	r.w.ax = 0x4300;
	int386 (0x2f, &r, &r);
	Diag_Log ("XMS driver (2Fh/4300h): AL=%02Xh (80h = HIMEM present)", r.h.al);

	memset (&r, 0, sizeof(r));
	r.w.ax = 0x0400;
	int386 (0x31, &r, &r);
	Diag_Log ("DPMI version (31h/0400h): carry=%d AX=%04Xh BX flags=%04Xh CPU type=%d (3=386 4=486 5=Pentium)",
		r.x.cflag ? 1 : 0, r.w.ax, r.w.bx, r.h.cl);

	memset (mi, 0xff, sizeof(mi));
	memset (&r, 0, sizeof(r));
	segread (&s);
	r.w.ax = 0x0500;
	r.x.edi = (unsigned)mi;
	int386x (0x31, &r, &r, &s);
	Diag_Log ("DPMI free memory (31h/0500h): carry=%d largest block %lu KB, max unlocked pages %lu, "
		"max locked pages %lu, linear space pages %lu, total unlocked %lu, free pages %lu, "
		"total phys %lu, free linear %lu, paging file pages %lu (FFFFFFFFh = unknown)",
		r.x.cflag ? 1 : 0, mi[0] == 0xfffffffful ? 0xfffffffful : mi[0] / 1024UL,
		mi[1], mi[2], mi[3], mi[4], mi[5], mi[6], mi[7], mi[8]);

	memset (&r, 0, sizeof(r));
	r.h.ah = 0x0f;
	int386 (0x10, &r, &r);
	Diag_Log ("BIOS video mode %02Xh, %d columns, page %d", r.h.al, r.h.ah, r.h.bh);

	Diag_Log ("Watcom C heap: avail %u KB, largest block %u KB, stack free %u bytes",
		(unsigned)(_memavl () / 1024U), (unsigned)(_memmax () / 1024U), (unsigned)stackavail ());

	Diag_PciScan ();

	Diag_Env ("DOS4G");
	Diag_Env ("DOS4GVM");
	Diag_Env ("DOS4GW");
	Diag_Env ("BLASTER");
	Diag_Env ("WINDIR");
	Diag_Env ("winbootdir");
	Diag_Env ("COMSPEC");
	Diag_Env ("PATH");
}
#endif

#ifdef _WIN32
static LONG WINAPI Diag_Filter (EXCEPTION_POINTERS *ep)
{
	EXCEPTION_RECORD	*er = ep->ExceptionRecord;
	CONTEXT			*c = ep->ContextRecord;
	unsigned long		*bp;
	unsigned long		*sp;
	int			i;

	diag_clean = 1;
	Diag_Log ("*** UNHANDLED EXCEPTION %08lX at EIP %08lX (flags %lX), image base %08lX, after %d frames",
		(unsigned long)er->ExceptionCode, (unsigned long)er->ExceptionAddress,
		(unsigned long)er->ExceptionFlags,
		(unsigned long)GetModuleHandle (NULL), diag_frames);
	if (er->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && er->NumberParameters >= 2)
		Diag_Log ("    access violation: %s address %08lX",
			er->ExceptionInformation[0] ? "WRITE to" : "READ from",
			(unsigned long)er->ExceptionInformation[1]);
	Diag_Log ("    EAX %08lX EBX %08lX ECX %08lX EDX %08lX",
		c->Eax, c->Ebx, c->Ecx, c->Edx);
	Diag_Log ("    ESI %08lX EDI %08lX EBP %08lX ESP %08lX",
		c->Esi, c->Edi, c->Ebp, c->Esp);
	Diag_Log ("    EFLAGS %08lX  CS %04lX DS %04lX SS %04lX",
		c->EFlags, c->SegCs, c->SegDs, c->SegSs);

	sp = (unsigned long *)c->Esp;
	for (i = 0 ; i < 24 ; i++)
	{
		if (IsBadReadPtr (sp + i, 4))
			break;
		if (i % 6 == 0)
			Diag_Log ("    stack+%02X: %08lX %08lX %08lX %08lX %08lX %08lX",
				i * 4, sp[i], (i + 1 < 24 && !IsBadReadPtr (sp + i + 1, 4)) ? sp[i + 1] : 0,
				(i + 2 < 24 && !IsBadReadPtr (sp + i + 2, 4)) ? sp[i + 2] : 0,
				(i + 3 < 24 && !IsBadReadPtr (sp + i + 3, 4)) ? sp[i + 3] : 0,
				(i + 4 < 24 && !IsBadReadPtr (sp + i + 4, 4)) ? sp[i + 4] : 0,
				(i + 5 < 24 && !IsBadReadPtr (sp + i + 5, 4)) ? sp[i + 5] : 0);
	}

	/* walk the EBP chain; look the return addresses up in the .map file */
	bp = (unsigned long *)c->Ebp;
	for (i = 0 ; i < 16 ; i++)
	{
		if (!bp || ((unsigned long)bp & 3) || IsBadReadPtr (bp, 8))
			break;
		Diag_Log ("    caller %2d: %08lX", i, bp[1]);
		if (bp[0] <= (unsigned long)bp)
			break;
		bp = (unsigned long *)bp[0];
	}

	return EXCEPTION_CONTINUE_SEARCH;
}

static void Diag_PlatformWin (void)
{
	OSVERSIONINFO	v;
	SYSTEM_INFO	si;
	char		path[260];
	HDC		dc;

	memset (&v, 0, sizeof(v));
	v.dwOSVersionInfoSize = sizeof(v);
	GetVersionEx (&v);
	Diag_Log ("Windows %lu.%lu build %lu platform %lu (1=Win9x 2=NT) '%s'",
		v.dwMajorVersion, v.dwMinorVersion, v.dwBuildNumber & 0xffff,
		v.dwPlatformId, v.szCSDVersion);

	GetSystemInfo (&si);
	Diag_Log ("CPU: type %lu, level %u, revision %04X, %lu processor(s), page size %lu",
		si.dwProcessorType, (unsigned)si.wProcessorLevel, (unsigned)si.wProcessorRevision,
		si.dwNumberOfProcessors, si.dwPageSize);

	if (GetModuleFileName (NULL, path, sizeof(path)))
		Diag_Log ("exe: %s", path);
	if (GetCurrentDirectory (sizeof(path), path))
		Diag_Log ("cwd: %s", path);
	Diag_Log ("command line: %s", GetCommandLine ());

	dc = GetDC (NULL);
	if (dc)
	{
		Diag_Log ("desktop: %d x %d, %d bpp, %d planes",
			GetSystemMetrics (SM_CXSCREEN), GetSystemMetrics (SM_CYSCREEN),
			GetDeviceCaps (dc, BITSPIXEL), GetDeviceCaps (dc, PLANES));
		ReleaseDC (NULL, dc);
	}

	SetUnhandledExceptionFilter (Diag_Filter);
}
#endif

/* ---- start ---------------------------------------------------------- */

static int Diag_Match (const char *a, const char *b)
{
	for ( ; *a && *b ; a++, b++)
	{
		char x = *a, y = *b;
		if (x >= 'A' && x <= 'Z') x += 32;
		if (y >= 'A' && y <= 'Z') y += 32;
		if (x != y)
			return 0;
	}
	return *a == *b;
}

void Diag_Early (int argc, char **argv)
{
	int	i;
	FILE	*f;
	long	len;
	char	cwd[260];

	for (i = 1 ; i < argc ; i++)
	{
		if (Diag_Match (argv[i], "-diag") || Diag_Match (argv[i], "+diag"))
			diag_enabled = 1;
		else if (Diag_Match (argv[i], "-diagv"))
			diag_enabled = diag_verbose = 1;
	}
	if (!diag_enabled)
		return;

	/* keep the log from growing without limit across runs */
	f = fopen (DIAG_FILE, "r");
	if (f)
	{
		fseek (f, 0, SEEK_END);
		len = ftell (f);
		fclose (f);
		if (len > DIAG_MAX_START)
			remove (DIAG_FILE);
	}

	diag_start = Diag_Ticks ();
	diag_lastbeat = (long)diag_start;

	Diag_Write ("\n=================================================================\n");
	Diag_Log ("nv1Quake diagnostic session start (%s)", diag_verbose ? "verbose" : "normal");
	Diag_Log ("build: %s %s, %s, %s", __DATE__, __TIME__,
#if defined(DOS)
		"DOS (DOS/4GW, NV1 hardware)",
#elif defined(NV1_HARDWARE)
		"Windows 95 (NV1 hardware)",
#elif defined(NV1_SOFT)
		"Windows (software NV1 emulation)",
#else
		"unknown platform",
#endif
#if defined(__WATCOMC__)
		"Open Watcom"
#elif defined(_MSC_VER)
		"Microsoft C"
#else
		"unknown compiler"
#endif
		);
	Diag_Log ("argc %d", argc);
	for (i = 0 ; i < argc ; i++)
		Diag_Log ("  argv[%d] = %s", i, argv[i]);
#ifndef _WIN32
	if (getcwd (cwd, sizeof(cwd)))
		Diag_Log ("cwd: %s", cwd);
#endif
	(void)cwd;
	Diag_Log ("log file: %s in the current directory; a session with no EXIT line did not shut down cleanly",
		DIAG_FILE);

	atexit (Diag_AtExit);
	signal (SIGSEGV, Diag_Signal);
	signal (SIGILL, Diag_Signal);
	signal (SIGFPE, Diag_Signal);
	signal (SIGABRT, Diag_Signal);

#ifdef DOS
	Diag_PlatformDos ();
#endif
#ifdef _WIN32
	Diag_PlatformWin ();
	Diag_MemLine ();
#endif
	Diag_SelfTest ();
	Diag_Log ("diagnostics ready");
	printf ("Diagnostics on: logging to %s\n", DIAG_FILE);
}
