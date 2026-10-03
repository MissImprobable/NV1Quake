#ifndef RETRO_INSTALLER_MAIN_H
#define RETRO_INSTALLER_MAIN_H

#include <windows.h>
#include "engine.h"

typedef struct {
    const char *appName;          /* e.g. "Duke Nukem 3D" - shown in UI/registry */
    const char *appNameSafe;      /* e.g. "DukeNukem3D" - no spaces, used for registry key/folder names */
    const char *defaultInstallDir; /* e.g. "C:\\Games\\DukeNukem3D" */
    const char *exeToLaunchRelative; /* relative to install dir, offered after a manual (non-silent) install; NULL = don't offer to launch */
    const char *shortcutIconRelative; /* relative to install dir; NULL = use exeToLaunchRelative's own icon (fine for a real Win32 exe, but DOS MZ executables have no icon resource at all) */
    /* Called once all files are extracted, with the real install dir and
       whatever provisioning args were passed (or all-zero if none) -
       this is where a game writes its own config file values. */
    void (*configure)(const char *installDir, const ProvisioningArgs *args);
} GameProfile;

/* Runs the whole installer (parses args, decides silent vs UI mode,
   extracts, configures, registers, creates shortcuts) or, if "/uninstall"
   is present, the uninstaller instead. This is the entire installer's
   entry point - a per-game main.c just fills in a GameProfile and calls
   this. */
int run_installer(HINSTANCE hInstance, LPSTR lpCmdLine, const GameProfile *profile);

#endif
