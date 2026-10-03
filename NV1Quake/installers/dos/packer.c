/* Build-time only tool (runs on the dev machine, never shipped/run on
   DOS) - packages a compiled DOS installer stub together with a game's
   files into the final self-extracting installer exe.

   Manifest format (DOS-specific, distinct from retro-installers/packer's
   Win32 format): 4-byte fileCount, then per file: 1-byte pathLen + path
   bytes (no NUL, may now contain backslash-separated subdirectories,
   e.g. "DRAGON\DRAGONBU\SW_TD.EXE", max 63 - matching dosengine.c's
   char relPath[64] buffer) + 4-byte fileSize. Subdirectories ARE
   supported now (added for Shadow Warrior's build\/docs\/dragon\
   structure - Duke3D and Wolf3D both happened to be flat, which is why
   this was originally assumed to be a universal DOS-game-install
   property; it isn't) - each individual path COMPONENT (directory or
   filename) still must be a valid 8.3 name, checked separately per
   component, not against the whole combined relative path. Excluding a
   directory by its leaf name (exclude.txt) skips the whole subtree, same
   mechanism as excluding a file.

   Output = stub.exe bytes ++ manifest bytes ++ blob bytes (all files'
   raw bytes concatenated in manifest order) ++ 8-byte trailer
   (manifestOffset:4, manifestSize:4). This is the classic DOS
   self-extracting-archive technique: dosengine.c's extract_archive()
   opens its own argv[0] path at runtime, reads the trailer off the end,
   and locates the manifest/blob from there.

   Usage: dospacker <stub.exe> <source-dir> <output.exe> [exclude-list-file] */
#include <windows.h>
#include <stdio.h>
#include <string.h>

static char g_manifestEntries[8192][64];
static int g_entryCount = 0;

static const char *g_excludeStorage[512];
static char g_excludeText[65536];
static int g_excludeCount = 0;

static int is_excluded(const char *leafName)
{
    int i;
    for (i = 0; i < g_excludeCount; i++)
    {
        if (_stricmp(leafName, g_excludeStorage[i]) == 0)
        {
            return 1;
        }
    }
    return 0;
}

static int load_exclude_file(const char *path)
{
    FILE *f = fopen(path, "r");
    char *p;
    int count = 0;

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

/* root is the real filesystem path being scanned; relPrefix is the
   manifest-relative path built up so far (e.g. "" at the top, "DRAGON\"
   one level in, "DRAGON\DRAGONBU\" two levels in) - recurses into real
   subdirectories while building flat backslash-joined manifest paths. */
static int collect_dir(const char *root, const char *relPrefix)
{
    char searchPath[1024];
    WIN32_FIND_DATA fd;
    HANDLE hFind;

    _snprintf(searchPath, sizeof(searchPath), "%s\\*", root);

    hFind = FindFirstFile(searchPath, &fd);
    if (hFind == INVALID_HANDLE_VALUE)
    {
        return 1;
    }

    do
    {
        char childRoot[1024];
        char relPath[64];

        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0)
        {
            continue;
        }

        if (is_excluded(fd.cFileName))
        {
            continue;
        }

        if (strlen(fd.cFileName) > 12)
        {
            printf("ERROR: '%s' is not a valid 8.3 name (>12 chars)\n", fd.cFileName);
            FindClose(hFind);
            return 0;
        }

        if (strlen(relPrefix) + strlen(fd.cFileName) >= sizeof(relPath))
        {
            printf("ERROR: '%s%s' relative path too long (limit %d)\n", relPrefix, fd.cFileName, (int)sizeof(relPath) - 1);
            FindClose(hFind);
            return 0;
        }

        _snprintf(childRoot, sizeof(childRoot), "%s\\%s", root, fd.cFileName);

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            char childPrefix[64];
            _snprintf(childPrefix, sizeof(childPrefix), "%s%s\\", relPrefix, fd.cFileName);
            if (!collect_dir(childRoot, childPrefix))
            {
                FindClose(hFind);
                return 0;
            }
            continue;
        }

        if (g_entryCount >= 8192)
        {
            printf("ERROR: too many files (limit 8192)\n");
            FindClose(hFind);
            return 0;
        }

        _snprintf(relPath, sizeof(relPath), "%s%s", relPrefix, fd.cFileName);
        strncpy(g_manifestEntries[g_entryCount], relPath, sizeof(g_manifestEntries[0]) - 1);
        g_manifestEntries[g_entryCount][sizeof(g_manifestEntries[0]) - 1] = '\0';
        g_entryCount++;
    } while (FindNextFile(hFind, &fd));

    FindClose(hFind);
    return 1;
}

static int copy_file_bytes(FILE *dst, const char *srcPath, unsigned long *outSize)
{
    FILE *src = fopen(srcPath, "rb");
    char buf[65536];
    size_t r;
    unsigned long total = 0;

    if (!src)
    {
        return 0;
    }

    while ((r = fread(buf, 1, sizeof(buf), src)) > 0)
    {
        fwrite(buf, 1, r, dst);
        total += (unsigned long)r;
    }

    fclose(src);
    *outSize = total;
    return 1;
}

int main(int argc, char **argv)
{
    const char *stubPath, *sourceDir, *outputPath;
    FILE *stub, *out;
    char buf[65536];
    size_t r;
    unsigned long stubSize = 0;
    unsigned long manifestSize = 0;
    unsigned long fileCount;
    unsigned long trailer[2];
    int i;
    unsigned long long totalBytes = 0;

    if (argc < 4)
    {
        printf("usage: dospacker <stub.exe> <source-dir> <output.exe> [exclude-list-file]\n");
        return 1;
    }

    stubPath = argv[1];
    sourceDir = argv[2];
    outputPath = argv[3];

    if (argc >= 5)
    {
        g_excludeCount = load_exclude_file(argv[4]);
        printf("Loaded %d exclusion(s) from %s\n", g_excludeCount, argv[4]);
    }

    if (!collect_dir(sourceDir, ""))
    {
        return 1;
    }
    printf("Found %d files under %s\n", g_entryCount, sourceDir);

    out = fopen(outputPath, "wb");
    if (!out)
    {
        printf("Could not create %s\n", outputPath);
        return 1;
    }

    /* 1. Copy the compiled stub exe verbatim. */
    stub = fopen(stubPath, "rb");
    if (!stub)
    {
        printf("Could not open stub %s\n", stubPath);
        fclose(out);
        return 1;
    }
    while ((r = fread(buf, 1, sizeof(buf), stub)) > 0)
    {
        fwrite(buf, 1, r, out);
        stubSize += (unsigned long)r;
    }
    fclose(stub);

    /* 1b. Patch MaxAlloc (MZ header offset 0x0C, a 16-bit paragraph count)
       down from Watcom's default 0xFFFF ("grab every paragraph of
       conventional memory still free") to a fixed 4096 paragraphs (64KB) -
       comfortably more than this program ever actually needs (a few KB
       header + an 8KB streaming copy buffer during extraction), but no
       longer asking DOS for "everything that's left." MinAlloc (the real,
       hard requirement DOS won't launch without) is untouched - this only
       shrinks the greedy optional ceiling. Cheap, safe, and reversible:
       doesn't change what the program can actually do, since it never
       used more than a tiny fraction of what 0xFFFF was asking for. */
    {
        unsigned short maxAllocParagraphs = 4096;
        fseek(out, 0x0C, SEEK_SET);
        fwrite(&maxAllocParagraphs, 2, 1, out);
        fseek(out, 0, SEEK_END);
    }

    /* 2. Manifest: fileCount, then per-file pathLen+path+size. */
    fileCount = (unsigned long)g_entryCount;
    fwrite(&fileCount, 4, 1, out);
    manifestSize += 4;

    for (i = 0; i < g_entryCount; i++)
    {
        unsigned char pathLen = (unsigned char)strlen(g_manifestEntries[i]);
        char fullPath[1024];
        unsigned long fileSize;

        fwrite(&pathLen, 1, 1, out);
        fwrite(g_manifestEntries[i], 1, pathLen, out);
        manifestSize += 1 + pathLen;

        _snprintf(fullPath, sizeof(fullPath), "%s\\%s", sourceDir, g_manifestEntries[i]);
        stub = fopen(fullPath, "rb");
        if (!stub)
        {
            printf("Could not open %s to determine size\n", fullPath);
            fclose(out);
            return 1;
        }
        fseek(stub, 0, SEEK_END);
        fileSize = (unsigned long)ftell(stub);
        fclose(stub);

        fwrite(&fileSize, 4, 1, out);
        manifestSize += 4;
    }

    /* 3. Blob: every file's raw bytes, in the same manifest order. */
    for (i = 0; i < g_entryCount; i++)
    {
        char fullPath[1024];
        unsigned long fileSize = 0;

        _snprintf(fullPath, sizeof(fullPath), "%s\\%s", sourceDir, g_manifestEntries[i]);
        if (!copy_file_bytes(out, fullPath, &fileSize))
        {
            printf("Could not copy %s\n", fullPath);
            fclose(out);
            return 1;
        }
        totalBytes += fileSize;

        if ((i % 10) == 0)
        {
            printf("  [%d/%d] %s (%lu bytes)\n", i + 1, g_entryCount, g_manifestEntries[i], fileSize);
        }
    }

    /* 4. Trailer: manifestOffset (== stubSize), manifestSize. */
    trailer[0] = stubSize;
    trailer[1] = manifestSize;
    fwrite(trailer, 4, 2, out);

    fclose(out);

    printf("Done. %d files, %llu total bytes.\n", g_entryCount, totalBytes);
    printf("Wrote %s (stub %lu bytes + manifest %lu bytes + blob + trailer)\n", outputPath, stubSize, manifestSize);
    return 0;
}
