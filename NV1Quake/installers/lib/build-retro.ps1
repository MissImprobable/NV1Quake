# Builds the nv1Quake Windows 95 installer (vendored Fragged retro engine, CRT-free, i486).
# Flags are the ones FraggedLANManager\retro-client\Makefile documents (no CMOV, no ASLR/DEP bits,
# no msvcrt) so the result loads on a stock Windows 95.

function Build-RetroInstaller {
    param(
        [Parameter(Mandatory)][string]$StageDir,
        [Parameter(Mandatory)][string]$OutExe,
        [Parameter(Mandatory)][string]$IconPath,
        [Parameter(Mandatory)][string]$NoticePath,
        [string]$MinGW = 'C:\msys64\mingw32\bin'
    )
    $retro = Join-Path $script:InstallersRoot 'retro'
    $work  = Join-Path $script:InstallersRoot 'stage\retro-work'
    $fwd   = { param($p) ([IO.Path]::GetFullPath($p)) -replace '\\', '/' }

    foreach ($tool in 'gcc.exe', 'i686-w64-mingw32-gcc.exe', 'windres.exe') {
        if (-not (Test-Path (Join-Path $MinGW $tool))) { throw "MinGW tool missing: $(Join-Path $MinGW $tool)" }
    }
    if (Test-Path -LiteralPath $work) { Remove-Item -LiteralPath $work -Recurse -Force }
    New-Item -ItemType Directory $work -Force | Out-Null
    if (Test-Path -LiteralPath $OutExe) { Remove-Item -LiteralPath $OutExe -Force }
    $env:PATH = "$MinGW;$env:PATH"   # cc1.exe must find its DLLs

    # 1. the host-side packer (normal CRT; never shipped)
    & (Join-Path $MinGW 'gcc.exe') -O1 -o (Join-Path $work 'packer.exe') (Join-Path $retro 'packer.c')
    if ($LASTEXITCODE -ne 0) { throw 'retro packer build failed' }

    # 2. pack the stage into payload.manifest / payload.blob
    Push-Location $work
    try {
        & .\packer.exe (& $fwd $StageDir) payload
        if ($LASTEXITCODE -ne 0) { throw 'retro packer failed' }
    } finally { Pop-Location }

    # 3. resource script with absolute paths (the stage lives outside this folder)
    $rc = @(
        '#include "engine.h"',
        '',
        ('IDI_APPICON  ICON   "{0}"' -f (& $fwd $IconPath)),
        ('IDR_MANIFEST RCDATA "{0}"' -f (& $fwd (Join-Path $work 'payload.manifest'))),
        ('IDR_BLOB     RCDATA "{0}"' -f (& $fwd (Join-Path $work 'payload.blob'))),
        ('IDR_NOTICE   RCDATA "{0}"' -f (& $fwd $NoticePath))
    )
    Set-Content (Join-Path $work 'payload.rc') $rc -Encoding ASCII
    & (Join-Path $MinGW 'windres.exe') -I $retro (Join-Path $work 'payload.rc') -O coff -o (Join-Path $work 'payload_res.o')
    if ($LASTEXITCODE -ne 0) { throw 'windres failed' }

    # 4. compile and link the installer
    $src = 'nv1quake_main.c', 'engine.c', 'installer_main.c', 'nocrt.c', 'crt_entry.c' | ForEach-Object { Join-Path $retro $_ }
    $cflags  = '-Os', '-s', '-march=i486', '-mtune=i486', '-D_WIN32_WINNT=0x0400', '-ffreestanding', '-fno-builtin', '-ffunction-sections', '-fdata-sections', '-I', $retro
    $ldflags = '-nostartfiles', '-nodefaultlibs', '-Wl,--gc-sections', '-Wl,--disable-dynamicbase', '-Wl,--disable-nxcompat'
    $libs    = '-lkernel32', '-luser32', '-lgdi32', '-lshell32', '-lole32', '-luuid', '-ladvapi32', '-lgcc'
    & (Join-Path $MinGW 'i686-w64-mingw32-gcc.exe') @cflags @ldflags -o $OutExe @src (Join-Path $work 'payload_res.o') @libs -mwindows
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $OutExe)) { throw 'Win95 installer link failed' }
}
