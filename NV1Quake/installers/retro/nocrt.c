#include "nocrt.h"

void *nc_alloc(size_t n)
{
    return HeapAlloc(GetProcessHeap(), 0, n);
}

void *nc_alloc_zeroed(size_t n)
{
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, n);
}

void *nc_realloc(void *p, size_t newSize)
{
    if (!p)
    {
        return nc_alloc(newSize);
    }
    return HeapReAlloc(GetProcessHeap(), 0, p, newSize);
}

void nc_free(void *p)
{
    if (p)
    {
        HeapFree(GetProcessHeap(), 0, p);
    }
}

size_t nc_strlen(const char *s)
{
    size_t n = 0;
    while (s[n])
    {
        n++;
    }
    return n;
}

void nc_strcpy(char *dst, const char *src)
{
    while ((*dst++ = *src++) != '\0')
    {
    }
}

void nc_strncpy(char *dst, const char *src, size_t n)
{
    size_t i = 0;
    if (n == 0)
    {
        return;
    }
    for (; i < n - 1 && src[i]; i++)
    {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

int nc_strcmp(const char *a, const char *b)
{
    while (*a && (*a == *b))
    {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

int nc_strncmp(const char *a, const char *b, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++)
    {
        if (a[i] != b[i] || a[i] == '\0')
        {
            return (unsigned char)a[i] - (unsigned char)b[i];
        }
    }
    return 0;
}

char *nc_strchr(const char *s, int c)
{
    for (; *s; s++)
    {
        if (*s == (char)c)
        {
            return (char *)s;
        }
    }
    return (c == '\0') ? (char *)s : NULL;
}

char *nc_strrchr(const char *s, int c)
{
    const char *last = NULL;
    for (; *s; s++)
    {
        if (*s == (char)c)
        {
            last = s;
        }
    }
    if (c == '\0')
    {
        return (char *)s;
    }
    return (char *)last;
}

char *nc_strstr(const char *hay, const char *needle)
{
    size_t needleLen = nc_strlen(needle);

    if (needleLen == 0)
    {
        return (char *)hay;
    }

    for (; *hay; hay++)
    {
        if (nc_strncmp(hay, needle, needleLen) == 0)
        {
            return (char *)hay;
        }
    }
    return NULL;
}

void *nc_memcpy(void *dst, const void *src, size_t n)
{
    char *d = (char *)dst;
    const char *s = (const char *)src;
    while (n--)
    {
        *d++ = *s++;
    }
    return dst;
}

void *nc_memset(void *dst, int val, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    while (n--)
    {
        *d++ = (unsigned char)val;
    }
    return dst;
}

long nc_atol(const char *s)
{
    long result = 0;
    int neg = 0;

    while (*s == ' ' || *s == '\t')
    {
        s++;
    }
    if (*s == '-')
    {
        neg = 1;
        s++;
    }
    else if (*s == '+')
    {
        s++;
    }

    while (nc_isdigit((unsigned char)*s))
    {
        result = result * 10 + (*s - '0');
        s++;
    }

    return neg ? -result : result;
}

long nc_hextol(const char *s)
{
    long result = 0;

    while (*s)
    {
        int digit;
        char c = *s;

        if (c >= '0' && c <= '9')
        {
            digit = c - '0';
        }
        else if (c >= 'a' && c <= 'f')
        {
            digit = c - 'a' + 10;
        }
        else if (c >= 'A' && c <= 'F')
        {
            digit = c - 'A' + 10;
        }
        else
        {
            break;
        }

        result = (result << 4) | digit;
        s++;
    }

    return result;
}

int nc_isdigit(int c)
{
    return c >= '0' && c <= '9';
}

int nc_tolower(int c)
{
    if (c >= 'A' && c <= 'Z')
    {
        return c - 'A' + 'a';
    }
    return c;
}

static size_t append_str(char *buf, size_t bufSize, size_t pos, const char *s)
{
    if (!s)
    {
        s = "";
    }
    while (*s && pos + 1 < bufSize)
    {
        buf[pos++] = *s++;
    }
    return pos;
}

static void long_to_dec(char *out, long v)
{
    char tmp[24];
    int i = 0;
    int neg = 0;
    unsigned long uv;
    int j;

    if (v < 0)
    {
        neg = 1;
        uv = (unsigned long)(-(v + 1)) + 1; /* avoids overflow at LONG_MIN */
    }
    else
    {
        uv = (unsigned long)v;
    }

    if (uv == 0)
    {
        tmp[i++] = '0';
    }
    while (uv > 0)
    {
        tmp[i++] = (char)('0' + (uv % 10));
        uv /= 10;
    }
    if (neg)
    {
        tmp[i++] = '-';
    }

    for (j = 0; j < i; j++)
    {
        out[j] = tmp[i - 1 - j];
    }
    out[i] = '\0';
}

static void ulong_to_dec(char *out, unsigned long uv)
{
    char tmp[24];
    int i = 0;
    int j;

    if (uv == 0)
    {
        tmp[i++] = '0';
    }
    while (uv > 0)
    {
        tmp[i++] = (char)('0' + (uv % 10));
        uv /= 10;
    }

    for (j = 0; j < i; j++)
    {
        out[j] = tmp[i - 1 - j];
    }
    out[i] = '\0';
}

int nc_vsnprintf(char *buf, size_t bufSize, const char *fmt, va_list args)
{
    size_t pos = 0;
    const char *p = fmt;
    char tmp[24];

    if (bufSize == 0)
    {
        return 0;
    }

    while (*p && pos + 1 < bufSize)
    {
        if (*p != '%')
        {
            buf[pos++] = *p++;
            continue;
        }

        p++;
        if (p[0] == 'l' && p[1] == 'd')
        {
            long_to_dec(tmp, va_arg(args, long));
            pos = append_str(buf, bufSize, pos, tmp);
            p += 2;
        }
        else if (*p == 'd')
        {
            long_to_dec(tmp, (long)va_arg(args, int));
            pos = append_str(buf, bufSize, pos, tmp);
            p++;
        }
        else if (*p == 'u')
        {
            ulong_to_dec(tmp, (unsigned long)va_arg(args, unsigned int));
            pos = append_str(buf, bufSize, pos, tmp);
            p++;
        }
        else if (*p == 's')
        {
            pos = append_str(buf, bufSize, pos, va_arg(args, const char *));
            p++;
        }
        else if (*p == 'c')
        {
            char c = (char)va_arg(args, int);
            if (pos + 1 < bufSize)
            {
                buf[pos++] = c;
            }
            p++;
        }
        else if (*p == '%')
        {
            if (pos + 1 < bufSize)
            {
                buf[pos++] = '%';
            }
            p++;
        }
        else
        {
            /* Unsupported specifier - emit it literally rather than
               silently dropping data, so a mistake here is obvious. */
            if (pos + 1 < bufSize)
            {
                buf[pos++] = '%';
            }
            if (*p && pos + 1 < bufSize)
            {
                buf[pos++] = *p;
            }
            if (*p)
            {
                p++;
            }
        }
    }

    buf[pos] = '\0';
    return (int)pos;
}

int nc_snprintf(char *buf, size_t bufSize, const char *fmt, ...)
{
    va_list args;
    int result;

    va_start(args, fmt);
    result = nc_vsnprintf(buf, bufSize, fmt, args);
    va_end(args);

    return result;
}

NCFILE *nc_fopen_read(const char *path)
{
    HANDLE h;
    NCFILE *f;

    h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
    {
        return NULL;
    }

    f = (NCFILE *)nc_alloc(sizeof(NCFILE));
    f->h = h;
    return f;
}

NCFILE *nc_fopen_write(const char *path)
{
    HANDLE h;
    NCFILE *f;

    h = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
    {
        return NULL;
    }

    f = (NCFILE *)nc_alloc(sizeof(NCFILE));
    f->h = h;
    return f;
}

long nc_fread(void *buf, long size, NCFILE *f)
{
    DWORD read = 0;
    if (!ReadFile(f->h, buf, (DWORD)size, &read, NULL))
    {
        return 0;
    }
    return (long)read;
}

int nc_fwrite(const void *buf, long size, NCFILE *f)
{
    DWORD written = 0;
    if (!WriteFile(f->h, buf, (DWORD)size, &written, NULL))
    {
        return 0;
    }
    return written == (DWORD)size;
}

void nc_fclose(NCFILE *f)
{
    if (!f)
    {
        return;
    }
    CloseHandle(f->h);
    nc_free(f);
}
