#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <direct.h>
#include "dosengine.h"

/* ---- ASCII banner ---- */

void print_banner(void)
{
    printf("+---------------------------------------------------------+\n");
    printf("|                                                           |\n");
    printf("| #####  ####   ###   ####   ####  #####  ####            |\n");
    printf("| #      #   # #   # #      #      #      #   #           |\n");
    printf("| ####   ####  ##### #  ##  #  ##  ####   #   #           |\n");
    printf("| #      #  #  #   # #   # #   # #      #   #             |\n");
    printf("| #      #   # #   #  ####   ####  ##### ####             |\n");
    printf("|                                                           |\n");
    printf("|              L A N   M A N A G E R  -  R E T R O          |\n");
    printf("|                                                           |\n");
    printf("+---------------------------------------------------------+\n");
    printf("\n");
}

/* ---- provisioning args (same contract as the Win32 installers - see
   CLAUDE_CODE_GAME_PROVISIONING.md) ---- */

static int find_arg_value(const char *arg, const char *name, char *out, int outSize)
{
    int nameLen = (int)strlen(name);
    int i;

    if (strncmp(arg, name, nameLen) != 0)
    {
        return 0;
    }

    for (i = 0; arg[nameLen + i] && i < outSize - 1; i++)
    {
        out[i] = arg[nameLen + i];
    }
    out[i] = '\0';
    return 1;
}

void parse_provisioning_args(int argc, char **argv, ProvisioningArgs *out)
{
    int i;
    char tmp[16];

    memset(out, 0, sizeof(*out));

    for (i = 1; i < argc; i++)
    {
        if (find_arg_value(argv[i], "/INSTALLDIR=", out->installDir, sizeof(out->installDir)))
        {
            out->hasInstallDir = 1;
        }
        else if (find_arg_value(argv[i], "/PLAYERNAME=", out->playerName, sizeof(out->playerName)))
        {
            out->hasPlayerName = 1;
        }
        else if (find_arg_value(argv[i], "/RESWIDTH=", tmp, sizeof(tmp)))
        {
            out->resWidth = atoi(tmp);
        }
        else if (find_arg_value(argv[i], "/RESHEIGHT=", tmp, sizeof(tmp)))
        {
            out->resHeight = atoi(tmp);
            if (out->resWidth > 0)
            {
                out->hasRes = 1;
            }
        }
        else if (find_arg_value(argv[i], "/INVERTMOUSE=", tmp, sizeof(tmp)))
        {
            out->invertMouse = atoi(tmp);
            out->hasInvertMouse = 1;
        }
    }
}

/* ---- filesystem ---- */

void create_dir(const char *path)
{
    mkdir(path);
}

/* Creates every intermediate directory level in path (which may contain
   backslash-separated subdirectories, e.g. "C:\WOLF3D\DRAGON\DRAGONBU") -
   needed once the manifest can carry subdirectory entries (see
   packer.c's header comment). mkdir()'ing an already-existing directory
   is harmless (just fails silently), so this is safe to call
   unconditionally for every file's containing directory during
   extraction, not just once per new subdirectory. */
static void create_dirs_recursive(const char *path)
{
    char buf[80];
    char *p;

    strncpy(buf, path, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    for (p = buf; *p; p++)
    {
        if (*p == '\\' && p != buf)
        {
            *p = '\0';
            mkdir(buf);
            *p = '\\';
        }
    }
}

/* ---- archive extraction ---- */

int extract_archive(const char *selfPath, const char *installDir, ExtractProgressFn onProgress)
{
    FILE *self;
    unsigned long trailer[2]; /* [0] = stubSize, [1] = manifestSize */
    unsigned long manifestOffset, blobOffset;
    unsigned char *manifest;
    unsigned long manifestSize;
    /* unsigned long, not unsigned int - in 16-bit DOS compilation "int" is
       only 2 bytes, but the manifest's fileCount field is written as 4
       bytes by the packer (matching a normal 32-bit host build's "long").
       memcpy'ing 4 bytes into a 2-byte int would silently corrupt 2 bytes
       of adjacent stack memory - caught this before it ever ran. */
    unsigned long fileCount;
    unsigned int i;
    unsigned long runningBlobOffset;
    char *buf;
    const unsigned int bufSize = 8192;

    buf = (char *)malloc(bufSize);
    if (!buf)
    {
        printf("ERROR: out of memory\n");
        return 0;
    }

    self = fopen(selfPath, "rb");
    if (!self)
    {
        printf("ERROR: could not open %s\n", selfPath);
        free(buf);
        return 0;
    }

    /* Trailer lives in the last 8 bytes: two 4-byte little-endian values. */
    fseek(self, -8L, SEEK_END);
    if (fread(trailer, 4, 2, self) != 2)
    {
        printf("ERROR: could not read trailer\n");
        fclose(self);
        free(buf);
        return 0;
    }

    manifestOffset = trailer[0];
    manifestSize = trailer[1];
    blobOffset = manifestOffset + manifestSize;

    manifest = (unsigned char *)malloc(manifestSize);
    if (!manifest)
    {
        printf("ERROR: out of memory reading manifest\n");
        fclose(self);
        free(buf);
        return 0;
    }

    fseek(self, (long)manifestOffset, SEEK_SET);
    if (fread(manifest, 1, manifestSize, self) != manifestSize)
    {
        printf("ERROR: could not read manifest\n");
        free(manifest);
        fclose(self);
        free(buf);
        return 0;
    }

    create_dir(installDir);

    {
        unsigned char *p = manifest;
        memcpy(&fileCount, p, 4);
        p += 4;
        runningBlobOffset = blobOffset;

        for (i = 0; i < fileCount; i++)
        {
            unsigned char pathLen;
            char relPath[64]; /* may now contain backslash-separated
                                  subdirectories, e.g. "DRAGON\DRAGONBU\SW_TD.EXE" -
                                  see packer.c's header comment */
            char fullPath[160];
            unsigned long fileSize;
            unsigned long remaining;
            FILE *out;

            pathLen = *p++;
            memcpy(relPath, p, pathLen);
            relPath[pathLen] = '\0';
            p += pathLen;

            memcpy(&fileSize, p, 4);
            p += 4;

            if (onProgress)
            {
                onProgress(relPath, i + 1, (unsigned int)fileCount);
            }

            sprintf(fullPath, "%s\\%s", installDir, relPath);
            create_dirs_recursive(fullPath);

            out = fopen(fullPath, "wb");
            if (!out)
            {
                printf("ERROR: could not create %s\n", fullPath);
                free(manifest);
                fclose(self);
                free(buf);
                return 0;
            }

            /* Close and reopen (append mode) every 64KB rather than
               holding one continuous handle open for the whole write -
               confirmed via real-hardware testing (Windows 95/86Box) that
               a long-held-open handle across a large sequential write is
               what actually triggers "Program too big to fit in memory"
               on real DOS, not the final on-disk file size or total exe
               size (both independently ruled out). Also gives a genuinely
               more granular progress readout as a side benefit, since a
               large file no longer extracts as one silent multi-second
               block between progress-callback ticks. */
            fseek(self, (long)runningBlobOffset, SEEK_SET);
            remaining = fileSize;
            {
                unsigned long sinceReopen = 0;
                const unsigned long reopenEvery = 65536UL;
                while (remaining > 0)
                {
                    unsigned int want = (remaining > bufSize) ? bufSize : (unsigned int)remaining;
                    unsigned int got = (unsigned int)fread(buf, 1, want, self);
                    if (got == 0)
                    {
                        break;
                    }
                    fwrite(buf, 1, got, out);
                    remaining -= got;
                    sinceReopen += got;

                    if (sinceReopen >= reopenEvery && remaining > 0)
                    {
                        fclose(out);
                        out = fopen(fullPath, "ab");
                        if (!out)
                        {
                            printf("ERROR: could not reopen %s\n", fullPath);
                            free(manifest);
                            fclose(self);
                            free(buf);
                            return 0;
                        }
                        sinceReopen = 0;
                    }
                }
            }
            fclose(out);

            runningBlobOffset += fileSize;
        }
    }

    free(manifest);
    fclose(self);
    free(buf);
    return 1;
}

/* ---- hand-rolled INI editing (DOS has no WritePrivateProfileString) ---- */

int ini_set_value(const char *path, const char *section, const char *key, const char *value)
{
    FILE *in;
    char *orig;
    long origSize = 0;
    char *out;
    long outLen = 0;
    char line[256];
    int inTargetSection = 0;
    int matchedSection = 0;
    int foundKey = 0;
    long keyLen = (long)strlen(key);
    long sectionLen = (long)strlen(section);
    char *lineStart;
    FILE *outFile;

    in = fopen(path, "rb");
    orig = (char *)malloc(32768);
    out = (char *)malloc(40960);
    if (!orig || !out)
    {
        printf("ERROR: out of memory editing %s\n", path);
        if (in)
        {
            fclose(in);
        }
        free(orig);
        free(out);
        return 0;
    }
    if (in)
    {
        origSize = (long)fread(orig, 1, 32767, in);
        fclose(in);
    }
    orig[origSize] = '\0';

    lineStart = orig;
    while (lineStart && *lineStart)
    {
        char *lineEnd = strchr(lineStart, '\n');
        long lineLen = lineEnd ? (long)(lineEnd - lineStart) : (long)strlen(lineStart);
        int i;

        /* Copy the line content (trim trailing \r) into a scratch buffer for comparisons. */
        long trimLen = lineLen;
        if (trimLen > 0 && lineStart[trimLen - 1] == '\r')
        {
            trimLen--;
        }
        for (i = 0; i < trimLen && i < (long)sizeof(line) - 1; i++)
        {
            line[i] = lineStart[i];
        }
        line[i] = '\0';

        if (line[0] == '[' && line[i - 1] == ']')
        {
            if (inTargetSection && !foundKey)
            {
                outLen += sprintf(out + outLen, "%s=%s\r\n", key, value);
                foundKey = 1;
            }
            inTargetSection = (strncmp(line + 1, section, sectionLen) == 0 && line[1 + sectionLen] == ']');
            if (inTargetSection)
            {
                matchedSection = 1;
            }
        }
        else if (inTargetSection && !foundKey &&
                 strncmp(line, key, keyLen) == 0 &&
                 (line[keyLen] == '=' || line[keyLen] == ' ' || line[keyLen] == '\t'))
        {
            outLen += sprintf(out + outLen, "%s=%s\r\n", key, value);
            foundKey = 1;
            lineStart = lineEnd ? lineEnd + 1 : NULL;
            continue;
        }

        outLen += sprintf(out + outLen, "%s\r\n", line);
        lineStart = lineEnd ? lineEnd + 1 : NULL;
    }

    if (matchedSection && inTargetSection && !foundKey)
    {
        outLen += sprintf(out + outLen, "%s=%s\r\n", key, value);
    }
    else if (!matchedSection)
    {
        outLen += sprintf(out + outLen, "\r\n[%s]\r\n%s=%s\r\n", section, key, value);
    }

    free(orig);

    outFile = fopen(path, "wb");
    if (!outFile)
    {
        free(out);
        return 0;
    }
    fwrite(out, 1, outLen, outFile);
    fclose(outFile);
    free(out);
    return 1;
}
