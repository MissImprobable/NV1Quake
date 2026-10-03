/* nv1Quake - Windows 95 retro installer. Engine only: Quake's game data is
   never included (see NOTICE.TXT). No per-game config file is written, so
   configure is a deliberate no-op. */
#define WIN32_LEAN_AND_MEAN
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0400
#endif
#include <windows.h>
#include "installer_main.h"

static void configure_nv1quake(const char *installDir, const ProvisioningArgs *args)
{
    (void)installDir;
    (void)args;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    static const GameProfile profile = {
        "nv1Quake",
        "nv1Quake",
        "C:\\Games\\nv1Quake",
        "NV1QUAKE.EXE",
        "nv1quake.ico",
        configure_nv1quake,
    };

    (void)hPrevInstance;
    (void)nCmdShow;

    return run_installer(hInstance, lpCmdLine, &profile);
}
