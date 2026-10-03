/* nv1Quake - genuine MS-DOS installer. Engine only: no Quake game data
   (see NOTICE.TXT). Install directory + button only; no per-game config. */
#include <stdio.h>
#include <string.h>
#include "dosmain.h"

static void configure_nv1quake(const char *installDir, const ProvisioningArgs *args)
{
    (void)installDir;
    (void)args;
}

int main(int argc, char **argv)
{
    static const GameProfile profile = {
        "nv1Quake",
        "C:\\NV1QUAKE",
        "NV1 QUAKE - SETUP",
        0,
        configure_nv1quake,
    };

    return run_installer(argc, argv, &profile);
}
