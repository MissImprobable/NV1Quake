#ifndef DOS_MAIN_H
#define DOS_MAIN_H

#include "dosengine.h"

typedef struct {
    const char *appName;           /* e.g. "Duke Nukem 3D" */
    const char *defaultInstallDir; /* e.g. "C:\\GAMES\\DUKE3D" */
    const char *title;             /* banner text, e.g. "NV1 QUAKE - SETUP" */
    int showGameOptions;           /* 1 = player name / resolution / invert mouse fields; 0 = install dir + button only */
    /* Called once extraction is done, with the real install dir and
       whatever provisioning args were passed (or all-zero if none) -
       this is where a game writes its own config file values. */
    void (*configure)(const char *installDir, const ProvisioningArgs *args);
} GameProfile;

/* Entire installer entry point - a per-game main() just fills in a
   GameProfile and calls this. No uninstaller: genuine DOS shareware
   installers didn't have one either, "delete the directory" is the
   real-world uninstall procedure and needs no tooling. */
int run_installer(int argc, char **argv, const GameProfile *profile);

#endif
