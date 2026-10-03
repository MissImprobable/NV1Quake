/* Build-time only tool (runs on the dev machine, never shipped/run on
   Win95) - walks a game's source directory and produces two files:
   <name>.manifest (binary: file count, then per-file relative path length
   + path bytes + file size) and <name>.blob (every file's raw bytes
   concatenated back to back, in manifest order). These get embedded as
   RCDATA resources in a per-game installer exe (see ../engine/) and
   extracted back out at install time.

   No compression - deliberately simple and safe rather than clever
   (matches the "don't worry about keeping it small" scope for this
   track). Ordinary CRT is fine here since this never ships.

   Usage: packer.exe <source-dir> <output-basename>
   Produces <output-basename>.manifest and <output-basename>.blob */
#include <windows.h>
#include <stdio.h>
#include <string.h>

static FILE *g_blob;
static FILE *g_manifest;
static unsigned long g_fileCount = 0;
static char g_manifestEntries[65536][300]; /* relative path, collected first pass */
static int g_entryCount = 0;

/* Names (files or directories) to skip entirely, case-insensitive exact
   match against just the leaf name - set by the caller per-game via
   set_exclude_list() before collect_dir() runs. */
static const char **g_excludeNames = NULL;
static int g_excludeCount = 0;

static void set_exclude_list(const char **names, int count)
{
    g_excludeNames = names;
    g_excludeCount = count;
}

static int is_excluded(const char *leafName)
{
    int i;
    for (i = 0; i < g_excludeCount; i++)
    {
        if (_stricmp(leafName, g_excludeNames[i]) == 0)
        {
            return 1;
        }
    }
    return 0;
}

static void collect_dir(const char *root, const char *rel)
{
    char searchPath[1024];
    char fullRel[600];
    WIN32_FIND_DATA fd;
    HANDLE hFind;

    _snprintf(searchPath, sizeof(searchPath), "%s\\%s\\*", root, rel);
    if (rel[0] == '\0')
    {
        _snprintf(searchPath, sizeof(searchPath), "%s\\*", root);
    }

    hFind = FindFirstFile(searchPath, &fd);
    if (hFind == INVALID_HANDLE_VALUE)
    {
        return;
    }

    do
    {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0)
        {
            continue;
        }

        if (is_excluded(fd.cFileName))
        {
            continue;
        }

        if (rel[0] == '\0')
        {
            _snprintf(fullRel, sizeof(fullRel), "%s", fd.cFileName);
        }
        else
        {
            _snprintf(fullRel, sizeof(fullRel), "%s\\%s", rel, fd.cFileName);
        }

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            collect_dir(root, fullRel);
        }
        else
        {
            strncpy(g_manifestEntries[g_entryCount], fullRel, sizeof(g_manifestEntries[0]) - 1);
            g_entryCount++;
        }
    } while (FindNextFile(hFind, &fd));

    FindClose(hFind);
}

static const char *g_excludeStorage[512];
static char g_excludeText[65536];

static int load_exclude_file(const char *path)
{
    FILE *f = fopen(path, "r");
    int count = 0;
    char *p;

    if (!f)
    {
        return 0;
    }

    fread(g_excludeText, 1, sizeof(g_excludeText) - 1, f);
    fclose(f);

    p = g_excludeText;
    while (*p && count < 512)
    {
        char *lineEnd = strchr(p, '\n');
        if (lineEnd)
        {
            *lineEnd = '\0';
        }
        if (*p && p[strlen(p) - 1] == '\r')
        {
            p[strlen(p) - 1] = '\0';
        }
        if (*p)
        {
            g_excludeStorage[count++] = p;
        }
        if (!lineEnd)
        {
            break;
        }
        p = lineEnd + 1;
    }

    return count;
}

int main(int argc, char **argv)
{
    char manifestPath[512];
    char blobPath[512];
    int i;
    unsigned long long totalBytes = 0;

    if (argc < 3)
    {
        printf("usage: packer <source-dir> <output-basename> [exclude-list-file]\n");
        return 1;
    }

    if (argc >= 4)
    {
        int n = load_exclude_file(argv[3]);
        set_exclude_list(g_excludeStorage, n);
        printf("Loaded %d exclusion(s) from %s\n", n, argv[3]);
    }

    collect_dir(argv[1], "");
    printf("Found %d files under %s\n", g_entryCount, argv[1]);

    _snprintf(manifestPath, sizeof(manifestPath), "%s.manifest", argv[2]);
    _snprintf(blobPath, sizeof(blobPath), "%s.blob", argv[2]);

    g_manifest = fopen(manifestPath, "wb");
    g_blob = fopen(blobPath, "wb");
    if (!g_manifest || !g_blob)
    {
        printf("Could not create output files.\n");
        return 1;
    }

    g_fileCount = (unsigned long)g_entryCount;
    fwrite(&g_fileCount, sizeof(g_fileCount), 1, g_manifest);

    for (i = 0; i < g_entryCount; i++)
    {
        char fullPath[1024];
        FILE *f;
        unsigned long pathLen;
        unsigned long fileSize;
        char buf[65536];
        size_t r;

        _snprintf(fullPath, sizeof(fullPath), "%s\\%s", argv[1], g_manifestEntries[i]);
        f = fopen(fullPath, "rb");
        if (!f)
        {
            printf("WARNING: could not open %s, skipping\n", fullPath);
            continue;
        }

        fseek(f, 0, SEEK_END);
        fileSize = (unsigned long)ftell(f);
        fseek(f, 0, SEEK_SET);

        pathLen = (unsigned long)strlen(g_manifestEntries[i]);
        fwrite(&pathLen, sizeof(pathLen), 1, g_manifest);
        fwrite(g_manifestEntries[i], 1, pathLen, g_manifest);
        fwrite(&fileSize, sizeof(fileSize), 1, g_manifest);

        while ((r = fread(buf, 1, sizeof(buf), f)) > 0)
        {
            fwrite(buf, 1, r, g_blob);
        }
        fclose(f);

        totalBytes += fileSize;
        if ((i % 50) == 0)
        {
            printf("  [%d/%d] %s (%lu bytes)\n", i + 1, g_entryCount, g_manifestEntries[i], fileSize);
        }
    }

    fclose(g_manifest);
    fclose(g_blob);

    printf("Done. %d files, %llu total bytes.\n", g_entryCount, totalBytes);
    printf("Wrote %s and %s\n", manifestPath, blobPath);
    return 0;
}
