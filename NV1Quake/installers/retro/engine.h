#ifndef RETRO_ENGINE_H
#define RETRO_ENGINE_H

#include <windows.h>

/* The standardized param contract already used by the modern Inno
   per-game installers (see CLAUDE_CODE_GAME_PROVISIONING.md) - so a
   client (WPF or retro) launching this installer automatically passes
   the same arguments either way. When /INSTALLDIR= is present, the
   installer runs fully unattended (no UI at all) - the caller already
   collected whatever preferences it has from the user. */
typedef struct {
    char installDir[MAX_PATH];
    int hasInstallDir;
    char playerName[64];
    int hasPlayerName;
    int resWidth;
    int resHeight;
    int hasRes;
    int invertMouse; /* 0/1, only meaningful if hasInvertMouse */
    int hasInvertMouse;
} ProvisioningArgs;

void parse_provisioning_args(const char *cmdLine, ProvisioningArgs *out);

/* Resource ids every per-game installer embeds via its own .rc file -
   fixed names so the engine can find them without per-game plumbing. */
#define IDR_MANIFEST 101
#define IDR_BLOB     102
#define IDR_ICONFILE 103
#define IDR_NOTICE   104
#define IDI_APPICON  1

/* Called once per file as extraction proceeds (before that file's bytes
   are written) - lets the UI show real progress instead of appearing to
   hang while a large archive (tens of MB, e.g. a GRP file) extracts.
   fileName is the relative path just being written, current is 1-based. */
typedef void (*ExtractProgressFn)(const char *fileName, unsigned long current, unsigned long total, void *ctx);

/* Extracts every file the manifest resource lists, reading its bytes from
   the blob resource, into installDir (creating subdirectories as
   needed). Returns 1 on success. onProgress may be NULL. */
int extract_archive(HINSTANCE hInst, const char *installDir, ExtractProgressFn onProgress, void *progressCtx);

/* The uninstall counterpart of extract_archive: deletes exactly the files the manifest resource
   lists (and any directories that become empty as a result), nothing else. Files the user added
   themselves (game data, saves, configs) or another program sharing the folder are left alone. */
void remove_archive_files(HINSTANCE hInst, const char *installDir);

/* ---- config-file editing primitives, shared across every game profile ---- */

/* Classic Windows .ini style: [Section]\r\nKey=Value\r\n. Creates the
   section/key if missing, replaces the value if present, preserves every
   other line untouched. */
int ini_set_value(const char *path, const char *section, const char *key, const char *value);

/* Flat "cvar dump" style config files (id-engine family: Quake/Quake3/
   etc.) - a line reading "cvarname value\n" (optionally quoted). Replaces
   the line if the cvar already appears, appends it otherwise. */
int cvar_set_line(const char *path, const char *cvarName, const char *value);

/* ---- registry ---- */

int write_registry_string(HKEY hive, const char *subkey, const char *valueName, const char *data);
int write_registry_dword(HKEY hive, const char *subkey, const char *valueName, DWORD data);

/* ---- shortcuts ---- */

int create_shortcut(const char *lnkPath, const char *targetPath, const char *workingDir,
                     const char *iconPath, const char *args, const char *description);
int get_shell_folder(const char *valueName, char *out, DWORD outSize);

/* ---- misc ---- */

void create_dirs_recursive(const char *path);
void delete_dir_recursive(const char *path);

#endif
