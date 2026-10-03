#ifndef FRAGGED_NOCRT_H
#define FRAGGED_NOCRT_H

/* Zero-CRT-dependency replacements for the handful of libc functions this
   project used. Root cause this exists at all: a stock Windows 95 install
   does not ship MSVCRT.DLL (it only became commonly present via IE4+/
   Win98 redistributing it) - any MinGW-built exe that calls ordinary CRT
   functions (malloc, strlen, sprintf, fopen, ...) dynamically links
   against msvcrt.dll and fails to even load on a genuinely fresh Win95
   machine with "A required DLL was not found". Every function here is
   implemented purely against kernel32 (HeapAlloc/CreateFile/etc.), which
   is guaranteed present since Win95's original release - see also
   crt_entry.c, which replaces the CRT startup stub itself (the other,
   less obvious source of a msvcrt.dll dependency). */

#include <windows.h>
#include <stdarg.h>

void *nc_alloc(size_t n);
void *nc_alloc_zeroed(size_t n);
void *nc_realloc(void *p, size_t newSize);
void nc_free(void *p);

size_t nc_strlen(const char *s);
void nc_strcpy(char *dst, const char *src);
void nc_strncpy(char *dst, const char *src, size_t n); /* always NUL-terminates dst, unlike CRT strncpy */
int nc_strcmp(const char *a, const char *b);
int nc_strncmp(const char *a, const char *b, size_t n);
char *nc_strchr(const char *s, int c);
char *nc_strrchr(const char *s, int c);
char *nc_strstr(const char *hay, const char *needle);
void *nc_memcpy(void *dst, const void *src, size_t n);
void *nc_memset(void *dst, int val, size_t n);

long nc_atol(const char *s);
long nc_hextol(const char *s); /* parses a leading hex run, e.g. an HTTP chunk-size line */

int nc_isdigit(int c);
int nc_tolower(int c);

/* Supports only %s %d %ld %u %c %% - the exact subset this project's
   format strings actually use (verified by grepping every call site
   before writing this). No width/precision/padding. */
int nc_vsnprintf(char *buf, size_t bufSize, const char *fmt, va_list args);
int nc_snprintf(char *buf, size_t bufSize, const char *fmt, ...);

typedef struct { HANDLE h; } NCFILE;
NCFILE *nc_fopen_read(const char *path);
NCFILE *nc_fopen_write(const char *path);
long nc_fread(void *buf, long size, NCFILE *f);
int nc_fwrite(const void *buf, long size, NCFILE *f);
void nc_fclose(NCFILE *f);

#endif
