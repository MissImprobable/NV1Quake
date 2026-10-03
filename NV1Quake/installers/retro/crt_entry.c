/* Replaces MinGW's normal CRT startup stub (crt2.o's mainCRTStartup),
   which itself calls into msvcrt.dll (__getmainargs, _amsg_exit, _cexit,
   __set_app_type, _initterm, ...) purely to build a C-style argc/argv
   before calling WinMain - even a program with zero explicit libc calls
   in its own code would still drag in msvcrt.dll through that stub alone.
   Built with -nostartfiles so the linker uses this WinMainCRTStartup
   instead (the symbol name -mwindows/subsystem:windows expects as the PE
   entry point) - no C-runtime initialization happens at all, only what
   kernel32 already sets up for every process regardless of CRT. */
#include <windows.h>

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow);

void WinMainCRTStartup(void)
{
    STARTUPINFOA si;
    int nCmdShow;
    int ret;

    GetStartupInfoA(&si);
    nCmdShow = (si.dwFlags & STARTF_USESHOWWINDOW) ? si.wShowWindow : SW_SHOWDEFAULT;

    /* GetCommandLineA() includes the exe's own path/name at the front,
       unlike the traditional WinMain lpCmdLine convention (args only) -
       harmless here since every use of lpCmdLine in this project is a
       substring search (e.g. "/uninstall"), not positional parsing. */
    ret = WinMain(GetModuleHandleA(NULL), NULL, GetCommandLineA(), nCmdShow);

    ExitProcess((UINT)ret);
}
