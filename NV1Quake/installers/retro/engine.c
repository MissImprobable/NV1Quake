#include "engine.h"
#include <shlobj.h>
#include <objbase.h>
#include "nocrt.h"

/* ---- provisioning args ---- */

static int find_arg_value(const char *cmdLine, const char *argName, char *out, int outSize)
{
    const char *p = nc_strstr(cmdLine, argName);
    const char *valStart;
    int i = 0;

    if (!p)
    {
        return 0;
    }
    valStart = p + nc_strlen(argName);

    if (*valStart == '"')
    {
        valStart++;
        while (*valStart && *valStart != '"' && i < outSize - 1)
        {
            out[i++] = *valStart++;
        }
    }
    else
    {
        while (*valStart && *valStart != ' ' && i < outSize - 1)
        {
            out[i++] = *valStart++;
        }
    }
    out[i] = '\0';
    return 1;
}

void parse_provisioning_args(const char *cmdLine, ProvisioningArgs *out)
{
    char tmp[32];

    nc_memset(out, 0, sizeof(*out));

    out->hasInstallDir = find_arg_value(cmdLine, "/INSTALLDIR=", out->installDir, sizeof(out->installDir));
    out->hasPlayerName = find_arg_value(cmdLine, "/PLAYERNAME=", out->playerName, sizeof(out->playerName));

    if (find_arg_value(cmdLine, "/RESWIDTH=", tmp, sizeof(tmp)))
    {
        out->resWidth = (int)nc_atol(tmp);
        if (find_arg_value(cmdLine, "/RESHEIGHT=", tmp, sizeof(tmp)))
        {
            out->resHeight = (int)nc_atol(tmp);
            out->hasRes = 1;
        }
    }

    if (find_arg_value(cmdLine, "/INVERTMOUSE=", tmp, sizeof(tmp)))
    {
        out->invertMouse = (int)nc_atol(tmp);
        out->hasInvertMouse = 1;
    }
}

/* ---- filesystem ---- */

void create_dirs_recursive(const char *path)
{
    char buf[MAX_PATH];
    char *p;

    nc_strncpy(buf, path, sizeof(buf));

    for (p = buf + 3; *p; p++) /* skip the "C:\" drive prefix */
    {
        if (*p == '\\')
        {
            *p = '\0';
            CreateDirectory(buf, NULL);
            *p = '\\';
        }
    }
    CreateDirectory(buf, NULL);
}

void delete_dir_recursive(const char *path)
{
    char searchPath[MAX_PATH];
    char childPath[MAX_PATH];
    WIN32_FIND_DATA fd;
    HANDLE hFind;

    nc_snprintf(searchPath, sizeof(searchPath), "%s\\*", path);
    hFind = FindFirstFile(searchPath, &fd);
    if (hFind == INVALID_HANDLE_VALUE)
    {
        return;
    }

    do
    {
        if (nc_strcmp(fd.cFileName, ".") == 0 || nc_strcmp(fd.cFileName, "..") == 0)
        {
            continue;
        }

        nc_snprintf(childPath, sizeof(childPath), "%s\\%s", path, fd.cFileName);

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            delete_dir_recursive(childPath);
        }
        else
        {
            SetFileAttributes(childPath, FILE_ATTRIBUTE_NORMAL);
            DeleteFile(childPath);
        }
    } while (FindNextFile(hFind, &fd));

    FindClose(hFind);
    RemoveDirectory(path);
}

/* ---- archive extraction (manifest + blob resources) ---- */

int extract_archive(HINSTANCE hInst, const char *installDir, ExtractProgressFn onProgress, void *progressCtx)
{
    HRSRC hManifestRes, hBlobRes;
    HGLOBAL hManifestData, hBlobData;
    const unsigned char *manifest;
    const unsigned char *blob;
    unsigned long fileCount, i;
    unsigned long blobOffset = 0;

    hManifestRes = FindResource(hInst, MAKEINTRESOURCE(IDR_MANIFEST), RT_RCDATA);
    hBlobRes = FindResource(hInst, MAKEINTRESOURCE(IDR_BLOB), RT_RCDATA);
    if (!hManifestRes || !hBlobRes)
    {
        return 0;
    }

    hManifestData = LoadResource(hInst, hManifestRes);
    hBlobData = LoadResource(hInst, hBlobRes);
    if (!hManifestData || !hBlobData)
    {
        return 0;
    }

    manifest = (const unsigned char *)LockResource(hManifestData);
    blob = (const unsigned char *)LockResource(hBlobData);
    if (!manifest || !blob)
    {
        return 0;
    }

    create_dirs_recursive(installDir);

    nc_memcpy(&fileCount, manifest, sizeof(fileCount));
    manifest += sizeof(fileCount);

    for (i = 0; i < fileCount; i++)
    {
        unsigned long pathLen;
        char relPath[512];
        char fullPath[MAX_PATH];
        unsigned long fileSize;
        char *lastSlash;
        NCFILE *out;

        nc_memcpy(&pathLen, manifest, sizeof(pathLen));
        manifest += sizeof(pathLen);

        if (pathLen >= sizeof(relPath))
        {
            return 0; /* corrupt manifest - fail loudly rather than truncate silently */
        }
        nc_memcpy(relPath, manifest, pathLen);
        relPath[pathLen] = '\0';
        manifest += pathLen;

        nc_memcpy(&fileSize, manifest, sizeof(fileSize));
        manifest += sizeof(fileSize);

        if (onProgress)
        {
            onProgress(relPath, i + 1, fileCount, progressCtx);
        }

        nc_snprintf(fullPath, sizeof(fullPath), "%s\\%s", installDir, relPath);

        /* Create the file's parent directory if this path has one -
           manifest entries can be nested arbitrarily deep. */
        lastSlash = nc_strrchr(fullPath, '\\');
        if (lastSlash)
        {
            char dirOnly[MAX_PATH];
            unsigned long dirLen = (unsigned long)(lastSlash - fullPath);
            nc_memcpy(dirOnly, fullPath, dirLen);
            dirOnly[dirLen] = '\0';
            create_dirs_recursive(dirOnly);
        }

        out = nc_fopen_write(fullPath);
        if (!out)
        {
            return 0;
        }
        nc_fwrite(blob + blobOffset, (long)fileSize, out);
        nc_fclose(out);

        blobOffset += fileSize;
    }

    return 1;
}

/* ---- ini editing (real Win95-era API, not hand-rolled) ---- */

int ini_set_value(const char *path, const char *section, const char *key, const char *value)
{
    return WritePrivateProfileStringA(section, key, value, path) != 0;
}

/* ---- flat cvar-dump style config (Quake-family etc.) ---- */

int cvar_set_line(const char *path, const char *cvarName, const char *value)
{
    NCFILE *in;
    char *orig = NULL;
    long origSize = 0;
    char *out;
    long outCap;
    long outLen = 0;
    long cvarNameLen = (long)nc_strlen(cvarName);
    char *lineStart;
    int replaced = 0;
    NCFILE *outFile;

    in = nc_fopen_read(path);
    if (in)
    {
        HANDLE h = in->h;
        origSize = (long)GetFileSize(h, NULL);
        orig = (char *)nc_alloc(origSize + 1);
        nc_fread(orig, origSize, in);
        orig[origSize] = '\0';
        nc_fclose(in);
    }

    outCap = origSize + cvarNameLen + 256;
    out = (char *)nc_alloc(outCap);

    lineStart = orig;
    while (lineStart && *lineStart)
    {
        char *lineEnd = nc_strchr(lineStart, '\n');
        long lineLen = lineEnd ? (long)(lineEnd - lineStart) : (long)nc_strlen(lineStart);
        int isMatch = 0;

        /* Match if the line's first token equals cvarName exactly (not
           just a prefix - "name" must not match a line starting with
           "name2"). */
        if (lineLen > cvarNameLen &&
            nc_strncmp(lineStart, cvarName, (size_t)cvarNameLen) == 0 &&
            (lineStart[cvarNameLen] == ' ' || lineStart[cvarNameLen] == '\t'))
        {
            isMatch = 1;
        }

        if (isMatch && !replaced)
        {
            outLen += nc_snprintf(out + outLen, (size_t)(outCap - outLen), "%s \"%s\"\n", cvarName, value);
            replaced = 1;
        }
        else
        {
            if (outLen + lineLen + 2 < outCap)
            {
                nc_memcpy(out + outLen, lineStart, (size_t)lineLen);
                outLen += lineLen;
                out[outLen++] = '\n';
            }
        }

        lineStart = lineEnd ? lineEnd + 1 : NULL;
    }

    if (!replaced)
    {
        outLen += nc_snprintf(out + outLen, (size_t)(outCap - outLen), "%s \"%s\"\n", cvarName, value);
    }

    outFile = nc_fopen_write(path);
    if (!outFile)
    {
        nc_free(orig);
        nc_free(out);
        return 0;
    }
    nc_fwrite(out, outLen, outFile);
    nc_fclose(outFile);

    nc_free(orig);
    nc_free(out);
    return 1;
}

/* ---- registry ---- */

int write_registry_string(HKEY hive, const char *subkey, const char *valueName, const char *data)
{
    HKEY hKey;
    LONG rc;

    rc = RegCreateKeyEx(hive, subkey, 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL);
    if (rc != ERROR_SUCCESS)
    {
        return 0;
    }
    rc = RegSetValueEx(hKey, valueName, 0, REG_SZ, (const BYTE *)data, (DWORD)nc_strlen(data) + 1);
    RegCloseKey(hKey);
    return rc == ERROR_SUCCESS;
}

int write_registry_dword(HKEY hive, const char *subkey, const char *valueName, DWORD data)
{
    HKEY hKey;
    LONG rc;

    rc = RegCreateKeyEx(hive, subkey, 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL);
    if (rc != ERROR_SUCCESS)
    {
        return 0;
    }
    rc = RegSetValueEx(hKey, valueName, 0, REG_DWORD, (const BYTE *)&data, sizeof(data));
    RegCloseKey(hKey);
    return rc == ERROR_SUCCESS;
}

/* ---- shortcuts (identical technique to retro-client/installer/setup.c) ---- */

int get_shell_folder(const char *valueName, char *out, DWORD outSize)
{
    HKEY hKey;
    DWORD type;
    LONG rc;

    rc = RegOpenKeyEx(HKEY_CURRENT_USER,
        "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Shell Folders",
        0, KEY_READ, &hKey);
    if (rc != ERROR_SUCCESS)
    {
        return 0;
    }

    rc = RegQueryValueEx(hKey, valueName, NULL, &type, (LPBYTE)out, &outSize);
    RegCloseKey(hKey);
    return rc == ERROR_SUCCESS && type == REG_SZ;
}

int create_shortcut(const char *lnkPath, const char *targetPath, const char *workingDir,
                     const char *iconPath, const char *args, const char *description)
{
    IShellLinkA *psl = NULL;
    IPersistFile *ppf = NULL;
    HRESULT hr;
    WCHAR wPath[MAX_PATH];
    int ok = 0;

    hr = CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLinkA, (void **)&psl);
    if (FAILED(hr) || !psl)
    {
        return 0;
    }

    psl->lpVtbl->SetPath(psl, targetPath);
    if (workingDir)
    {
        psl->lpVtbl->SetWorkingDirectory(psl, workingDir);
    }
    if (iconPath)
    {
        psl->lpVtbl->SetIconLocation(psl, iconPath, 0);
    }
    if (args)
    {
        psl->lpVtbl->SetArguments(psl, args);
    }
    if (description)
    {
        psl->lpVtbl->SetDescription(psl, description);
    }

    hr = psl->lpVtbl->QueryInterface(psl, &IID_IPersistFile, (void **)&ppf);
    if (SUCCEEDED(hr) && ppf)
    {
        MultiByteToWideChar(CP_ACP, 0, lnkPath, -1, wPath, MAX_PATH);
        hr = ppf->lpVtbl->Save(ppf, wPath, TRUE);
        ok = SUCCEEDED(hr);
        ppf->lpVtbl->Release(ppf);
    }

    psl->lpVtbl->Release(psl);
    return ok;
}
