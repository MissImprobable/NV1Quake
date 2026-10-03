#include "installer_main.h"
#include <shellapi.h>
#include <shlobj.h>
#include <objbase.h>
#include "nocrt.h"

#define IDC_PATH_EDIT   401
#define IDC_BTN_INSTALL 402

static HINSTANCE g_hInstance;
static const GameProfile *g_profile;

static void uninstall_key_path(const GameProfile *profile, char *out, int outSize)
{
    nc_snprintf(out, outSize,
        "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\%s", profile->appNameSafe);
}

static void register_uninstaller(const GameProfile *profile, const char *installDir)
{
    char keyPath[256];
    char uninsPath[MAX_PATH];
    char uninstallCmd[MAX_PATH * 2];
    HKEY hKey;
    DWORD noVal = 1;

    uninstall_key_path(profile, keyPath, sizeof(keyPath));
    nc_snprintf(uninsPath, sizeof(uninsPath), "%s\\unins.exe", installDir);
    nc_snprintf(uninstallCmd, sizeof(uninstallCmd), "\"%s\" /uninstall", uninsPath);

    if (RegCreateKeyEx(HKEY_LOCAL_MACHINE, keyPath, 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) != ERROR_SUCCESS)
    {
        return;
    }
    RegSetValueEx(hKey, "DisplayName", 0, REG_SZ, (const BYTE *)profile->appName, (DWORD)nc_strlen(profile->appName) + 1);
    RegSetValueEx(hKey, "UninstallString", 0, REG_SZ, (const BYTE *)uninstallCmd, (DWORD)nc_strlen(uninstallCmd) + 1);
    RegSetValueEx(hKey, "InstallLocation", 0, REG_SZ, (const BYTE *)installDir, (DWORD)nc_strlen(installDir) + 1);
    RegSetValueEx(hKey, "NoModify", 0, REG_DWORD, (const BYTE *)&noVal, sizeof(DWORD));
    RegSetValueEx(hKey, "NoRepair", 0, REG_DWORD, (const BYTE *)&noVal, sizeof(DWORD));
    RegCloseKey(hKey);
}

static void create_default_shortcuts(const GameProfile *profile, const char *installDir)
{
    char buf[MAX_PATH];
    char groupDir[MAX_PATH];
    char lnkPath[MAX_PATH];
    char targetPath[MAX_PATH];
    char iconPath[MAX_PATH];

    if (!profile->exeToLaunchRelative)
    {
        return;
    }

    nc_snprintf(targetPath, sizeof(targetPath), "%s\\%s", installDir, profile->exeToLaunchRelative);
    if (profile->shortcutIconRelative)
    {
        nc_snprintf(iconPath, sizeof(iconPath), "%s\\%s", installDir, profile->shortcutIconRelative);
    }
    else
    {
        nc_strncpy(iconPath, targetPath, sizeof(iconPath));
    }

    if (get_shell_folder("Programs", buf, sizeof(buf)))
    {
        nc_snprintf(groupDir, sizeof(groupDir), "%s\\%s", buf, profile->appName);
        create_dirs_recursive(groupDir);
        nc_snprintf(lnkPath, sizeof(lnkPath), "%s\\%s.lnk", groupDir, profile->appName);
        create_shortcut(lnkPath, targetPath, installDir, iconPath, NULL, profile->appName);
    }

    if (get_shell_folder("Desktop", buf, sizeof(buf)))
    {
        nc_snprintf(lnkPath, sizeof(lnkPath), "%s\\%s.lnk", buf, profile->appName);
        create_shortcut(lnkPath, targetPath, installDir, iconPath, NULL, profile->appName);
    }
}

static int do_install(HINSTANCE hInstance, const GameProfile *profile, const char *installDir,
                       const ProvisioningArgs *args, int silent)
{
    char selfPath[MAX_PATH];
    char uninsPath[MAX_PATH];

    if (!extract_archive(hInstance, installDir, NULL, NULL))
    {
        if (!silent)
        {
            MessageBox(NULL, "Could not extract game files - installation failed.", "Setup", MB_ICONERROR);
        }
        return 0;
    }

    if (profile->configure)
    {
        profile->configure(installDir, args);
    }

    GetModuleFileName(NULL, selfPath, sizeof(selfPath));
    nc_snprintf(uninsPath, sizeof(uninsPath), "%s\\unins.exe", installDir);
    CopyFile(selfPath, uninsPath, FALSE);

    register_uninstaller(profile, installDir);
    create_default_shortcuts(profile, installDir);

    if (!silent)
    {
        char targetPath[MAX_PATH];
        if (profile->exeToLaunchRelative)
        {
            nc_snprintf(targetPath, sizeof(targetPath), "%s\\%s", installDir, profile->exeToLaunchRelative);
            if (MessageBox(NULL, "Installation complete. Launch now?", "Setup", MB_YESNO | MB_ICONQUESTION) == IDYES)
            {
                ShellExecute(NULL, "open", targetPath, NULL, installDir, SW_SHOWNORMAL);
            }
        }
        else
        {
            MessageBox(NULL, "Installation complete.\n\nInstaller created by Abigail / Abnormality Software (2026).", "Setup", MB_OK | MB_ICONINFORMATION);
        }
    }

    return 1;
}

/* Removes the shortcuts create_default_shortcuts made (and the Start Menu group if it is now empty). */
static void remove_default_shortcuts(const GameProfile *profile)
{
    char buf[MAX_PATH];
    char groupDir[MAX_PATH];
    char lnkPath[MAX_PATH];

    if (!profile->exeToLaunchRelative)
    {
        return;
    }
    if (get_shell_folder("Programs", buf, sizeof(buf)))
    {
        nc_snprintf(groupDir, sizeof(groupDir), "%s\\%s", buf, profile->appName);
        nc_snprintf(lnkPath, sizeof(lnkPath), "%s\\%s.lnk", groupDir, profile->appName);
        DeleteFile(lnkPath);
        RemoveDirectory(groupDir); /* only succeeds if empty */
    }
    if (get_shell_folder("Desktop", buf, sizeof(buf)))
    {
        nc_snprintf(lnkPath, sizeof(lnkPath), "%s\\%s.lnk", buf, profile->appName);
        DeleteFile(lnkPath);
    }
}

static void do_uninstall(const GameProfile *profile, int silent)
{
    char selfPath[MAX_PATH];
    char installDir[MAX_PATH];
    char keyPath[256];
    char *lastSlash;
    char msg[256];

    if (!silent)
    {
        nc_snprintf(msg, sizeof(msg), "Remove %s?\n\nOnly the files nv1Quake installed are removed; your own game data and saves are kept.", profile->appName);
        if (MessageBox(NULL, msg, "Uninstall", MB_YESNO | MB_ICONQUESTION) != IDYES)
        {
            return;
        }
    }

    GetModuleFileName(NULL, selfPath, sizeof(selfPath));
    nc_strncpy(installDir, selfPath, sizeof(installDir));
    lastSlash = nc_strrchr(installDir, '\\');
    if (lastSlash)
    {
        *lastSlash = '\0';
    }

    /* Deletes exactly the files this installer put there (taken from its own embedded manifest),
       NOT the whole folder: the user may have put game data, saves or configs in it, or chosen a
       folder shared with other programs. The running unins.exe itself is left behind (Windows
       won't let a running exe delete its own file); the batch below removes it and the folder
       if, and only if, the folder is then empty. */
    remove_archive_files(GetModuleHandle(NULL), installDir);
    remove_default_shortcuts(profile);

    uninstall_key_path(profile, keyPath, sizeof(keyPath));
    RegDeleteKey(HKEY_LOCAL_MACHINE, keyPath);

    if (!silent)
    {
        MessageBox(NULL, "Uninstalled.", "Uninstall", MB_OK | MB_ICONINFORMATION);
    }

    {
        char tempDir[MAX_PATH];
        char batPath[MAX_PATH];
        char comspec[MAX_PATH];
        char cmdLine[MAX_PATH * 2];
        char line[MAX_PATH * 2];
        STARTUPINFO si;
        PROCESS_INFORMATION pi;
        NCFILE *bat;

        GetTempPath(sizeof(tempDir), tempDir);
        nc_snprintf(batPath, sizeof(batPath), "%sretro_installer_uninst.bat", tempDir);

        bat = nc_fopen_write(batPath);
        if (bat)
        {
            nc_snprintf(line, sizeof(line), "@echo off\r\n:wait\r\ndel \"%s\"\r\n", selfPath);
            nc_fwrite(line, (long)nc_strlen(line), bat);
            nc_snprintf(line, sizeof(line), "if exist \"%s\" goto wait\r\n", selfPath);
            nc_fwrite(line, (long)nc_strlen(line), bat);
            nc_snprintf(line, sizeof(line), "rmdir \"%s\"\r\n", installDir);
            nc_fwrite(line, (long)nc_strlen(line), bat);
            nc_snprintf(line, sizeof(line), "del \"%s\"\r\n", batPath);
            nc_fwrite(line, (long)nc_strlen(line), bat);
            nc_fclose(bat);

            if (!GetEnvironmentVariable("COMSPEC", comspec, sizeof(comspec)))
            {
                nc_strcpy(comspec, "command.com");
            }
            nc_snprintf(cmdLine, sizeof(cmdLine), "\"%s\" /c \"%s\"", comspec, batPath);

            nc_memset(&si, 0, sizeof(si));
            si.cb = sizeof(si);
            if (CreateProcess(NULL, cmdLine, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
            {
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
            }
        }
    }
}

static LRESULT CALLBACK InstallWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
        case WM_CREATE:
        {
            char label[300];
            nc_snprintf(label, sizeof(label), "Install %s to:", g_profile->appName);
            CreateWindow("STATIC", label, WS_CHILD | WS_VISIBLE, 10, 10, 380, 18, hwnd, NULL, g_hInstance, NULL);
            CreateWindow("EDIT", g_profile->defaultInstallDir, WS_CHILD | WS_VISIBLE | WS_BORDER,
                10, 32, 380, 20, hwnd, (HMENU)IDC_PATH_EDIT, g_hInstance, NULL);
            CreateWindow("BUTTON", "Install", WS_CHILD | WS_VISIBLE, 10, 64, 100, 26, hwnd, (HMENU)IDC_BTN_INSTALL, g_hInstance, NULL);
            CreateWindow("STATIC", "Installer created by Abigail / Abnormality Software (2026)",
                WS_CHILD | WS_VISIBLE, 10, 96, 390, 16, hwnd, NULL, g_hInstance, NULL);
            return 0;
        }

        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_BTN_INSTALL)
            {
                char installDir[MAX_PATH];
                ProvisioningArgs args;

                GetWindowText(GetDlgItem(hwnd, IDC_PATH_EDIT), installDir, sizeof(installDir));
                if (installDir[0] == '\0')
                {
                    MessageBox(hwnd, "Choose an install folder first.", "Setup", MB_ICONWARNING);
                    return 0;
                }

                nc_memset(&args, 0, sizeof(args));
                nc_strncpy(args.installDir, installDir, sizeof(args.installDir));
                args.hasInstallDir = 1;

                if (do_install(g_hInstance, g_profile, installDir, &args, 0))
                {
                    DestroyWindow(hwnd);
                }
            }
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

static int show_install_ui(HINSTANCE hInstance, const GameProfile *profile)
{
    WNDCLASS wc;
    MSG msg;
    HWND hwnd;

    g_hInstance = hInstance;
    g_profile = profile;

    CoInitialize(NULL);

    nc_memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = InstallWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "RetroGameSetup";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APPICON));
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    RegisterClass(&wc);

    hwnd = CreateWindow("RetroGameSetup", profile->appName,
        WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME,
        CW_USEDEFAULT, CW_USEDEFAULT, 420, 170,
        NULL, NULL, hInstance, NULL);
    ShowWindow(hwnd, SW_SHOWNORMAL);
    UpdateWindow(hwnd);

    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    CoUninitialize();
    return (int)msg.wParam;
}

#define IDC_NOTICE_TEXT   501
#define IDC_NOTICE_OK     502
#define IDC_NOTICE_CANCEL 503

static int g_noticeResult;
static char *g_noticeText;

static LRESULT CALLBACK NoticeWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
        case WM_CREATE:
            CreateWindow("EDIT", g_noticeText,
                WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                10, 10, 560, 320, hwnd, (HMENU)IDC_NOTICE_TEXT, g_hInstance, NULL);
            CreateWindow("BUTTON", "Continue", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                390, 342, 85, 26, hwnd, (HMENU)IDC_NOTICE_OK, g_hInstance, NULL);
            CreateWindow("BUTTON", "Cancel", WS_CHILD | WS_VISIBLE,
                485, 342, 85, 26, hwnd, (HMENU)IDC_NOTICE_CANCEL, g_hInstance, NULL);
            CreateWindow("STATIC", "Installer created by Abigail / Abnormality Software (2026)",
                WS_CHILD | WS_VISIBLE, 10, 348, 370, 16, hwnd, NULL, g_hInstance, NULL);
            return 0;

        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_NOTICE_OK)
            {
                g_noticeResult = 1;
                DestroyWindow(hwnd);
            }
            else if (LOWORD(wParam) == IDC_NOTICE_CANCEL)
            {
                g_noticeResult = 0;
                DestroyWindow(hwnd);
            }
            return 0;

        case WM_CLOSE:
            g_noticeResult = 0;
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

/* Shows NOTICE.TXT (embedded as IDR_NOTICE) before anything is written to
   disk. Returns 1 to continue, 0 if the user cancelled. */
static int show_notice(HINSTANCE hInstance, const GameProfile *profile)
{
    HRSRC res;
    HGLOBAL mem;
    DWORD size;
    const char *data;
    WNDCLASS wc;
    MSG msg;
    HWND hwnd;
    char caption[300];

    res = FindResource(hInstance, MAKEINTRESOURCE(IDR_NOTICE), RT_RCDATA);
    if (!res)
    {
        return 1; /* no notice embedded: nothing to show */
    }
    mem = LoadResource(hInstance, res);
    size = SizeofResource(hInstance, res);
    data = (const char *)LockResource(mem);
    g_noticeText = (char *)LocalAlloc(LPTR, size + 1);
    if (!g_noticeText)
    {
        return 1;
    }
    nc_memcpy(g_noticeText, data, size);
    g_noticeText[size] = '\0';

    g_hInstance = hInstance;
    g_noticeResult = 0;
    nc_memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = NoticeWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "RetroGameNotice";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APPICON));
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    RegisterClass(&wc);

    nc_snprintf(caption, sizeof(caption), "%s - Licence and notices", profile->appName);
    hwnd = CreateWindow("RetroGameNotice", caption,
        WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME,
        CW_USEDEFAULT, CW_USEDEFAULT, 596, 418, NULL, NULL, hInstance, NULL);
    ShowWindow(hwnd, SW_SHOWNORMAL);
    UpdateWindow(hwnd);
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    LocalFree(g_noticeText);
    return g_noticeResult;
}

int run_installer(HINSTANCE hInstance, LPSTR lpCmdLine, const GameProfile *profile)
{
    ProvisioningArgs args;

    parse_provisioning_args(lpCmdLine, &args);

    if (lpCmdLine && nc_strstr(lpCmdLine, "/uninstall"))
    {
        do_uninstall(profile, nc_strstr(lpCmdLine, "/silent") != NULL);
        return 0;
    }

    if (args.hasInstallDir)
    {
        /* Launched automatically by a client that already collected the
           user's preferences - no UI, no prompts. */
        do_install(hInstance, profile, args.installDir, &args, 1);
        return 0;
    }

    if (!show_notice(hInstance, profile))
    {
        return 0; /* cancelled at the licence screen: nothing written */
    }
    return show_install_ui(hInstance, profile);
}
