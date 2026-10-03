#ifndef DOS_ENGINE_H
#define DOS_ENGINE_H

/* Real-mode MS-DOS installer engine (Open Watcom, -bt=dos -ml target).
   Genuinely runs on any DOS-capable machine, not just ones with Windows
   installed - the whole point of this track vs. the Win32 "retro"
   installer engine in retro-installers/.

   Packaging: a small compiled stub .EXE has the manifest + concatenated
   file blob appended directly onto the end of the file by the packer
   tool (packer/packer.c), with an 8-byte trailer at the very end
   (stubSize:4, manifestSize:4) so the running program can find its own
   appended data by opening its own argv[0] path. This is the classic
   technique real DOS self-extracting archives used - no embedded-resource
   system exists in a real-mode DOS .EXE the way PE resources do. */

typedef struct {
    char installDir[64];
    int hasInstallDir;
    char playerName[32];
    int hasPlayerName;
    int resWidth;
    int resHeight;
    int hasRes;
    int invertMouse;
    int hasInvertMouse;
} ProvisioningArgs;

void parse_provisioning_args(int argc, char **argv, ProvisioningArgs *out);

/* Called once per file during extraction (before writing it) so the UI
   can show progress instead of appearing to hang on a large archive. */
typedef void (*ExtractProgressFn)(const char *fileName, unsigned int current, unsigned int total);

/* Extracts every file the self-appended manifest lists into installDir.
   selfPath is argv[0] (the running program's own path). Returns 1 on
   success. onProgress may be NULL. */
int extract_archive(const char *selfPath, const char *installDir, ExtractProgressFn onProgress);

/* Classic DOS/Windows .ini style: [Section]\r\nKey=Value\r\n. Creates the
   section/key if missing, replaces the value if present. Hand-rolled -
   DOS has no WritePrivateProfileString equivalent. */
int ini_set_value(const char *path, const char *section, const char *key, const char *value);

void print_banner(void);
void create_dir(const char *path);

#endif
