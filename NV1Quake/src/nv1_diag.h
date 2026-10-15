/*
nv1_diag.h -- opt-in diagnostic logging for every nv1Quake build.

Run with -diag to write NV1DIAG.LOG (in the current directory) or -diagv for
the verbose trace as well.  Without the flag nothing is logged and nothing
extra is touched, so the hooks can stay in the engine permanently.

The log is opened, appended to and closed again for every line.  That is slow
but it means everything up to the last line survives a crash, a hang or a
hard reset on real hardware.  A session that ends without an "EXIT" line did
not shut down cleanly: the last line says where it got to.
*/

#ifndef NV1_DIAG_H
#define NV1_DIAG_H

extern int	diag_enabled;
extern int	diag_verbose;
extern int	diag_memready;		/* set by Memory_Init once Hunk/Zone exist */

/* call first thing in the entry point, before COM_InitArgv */
void Diag_Early (int argc, char **argv);

void Diag_Log (const char *fmt, ...);
void Diag_Checkpoint (const char *name);
void Diag_Console (const char *text);		/* console / Sys_Printf text */
void Diag_Heap (const char *where);		/* C heap / Hunk / Zone checks */
void Diag_Frame (void);				/* once per main-loop frame */
void Diag_Exit (const char *how, const char *msg);

#ifdef NV1_DIAG
#define DIAG_LOG(args)		do { if (diag_enabled) Diag_Log args; } while (0)
#define DIAG_LOGV(args)		do { if (diag_verbose) Diag_Log args; } while (0)
#define DIAG_CP(name)		do { if (diag_enabled) Diag_Checkpoint (name); } while (0)
#define DIAG_HEAP(where)	do { if (diag_enabled) Diag_Heap (where); } while (0)
#define DIAG_CONSOLE(text)	do { if (diag_enabled) Diag_Console (text); } while (0)
#define DIAG_FRAME()		do { if (diag_enabled) Diag_Frame (); } while (0)
#define DIAG_EXIT(how, msg)	do { if (diag_enabled) Diag_Exit (how, msg); } while (0)
#else
#define DIAG_LOG(args)		((void)0)
#define DIAG_LOGV(args)		((void)0)
#define DIAG_CP(name)		((void)0)
#define DIAG_HEAP(where)	((void)0)
#define DIAG_CONSOLE(text)	((void)0)
#define DIAG_FRAME()		((void)0)
#define DIAG_EXIT(how, msg)	((void)0)
#endif

#endif
