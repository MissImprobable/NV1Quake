# Builds the nv1Quake DOS installer: a real-mode MZ stub (Open Watcom -bt=dos -ml) with the
# stage appended as a self-extracting archive by the vendored DOS packer.

# Turns NOTICE.TXT into a C header the DOS stub compiles in: one string per line, wrapped to
# 76 columns, ASCII only, quotes and backslashes escaped.
function ConvertTo-NoticeHeader {
    param([string]$NoticePath, [string]$OutPath)
    $out = New-Object System.Collections.Generic.List[string]
    foreach ($line in (Get-Content $NoticePath)) {
        $rest = $line
        if ($rest.Length -eq 0) { $out.Add(''); continue }
        while ($rest.Length -gt 76) {
            $cut = $rest.LastIndexOf(' ', 76)
            if ($cut -lt 1) { $cut = 76 }
            $out.Add($rest.Substring(0, $cut))
            $rest = $rest.Substring($cut).TrimStart()
        }
        $out.Add($rest)
    }
    $body = $out | ForEach-Object { '    "' + ($_.Replace('\', '\\').Replace('"', '\"')) + '",' }
    $text = @('static const char *const NOTICE_LINES[] = {') + $body + @('};', "#define NOTICE_LINE_COUNT $($out.Count)")
    [IO.File]::WriteAllText($OutPath, ($text -join "`r`n") + "`r`n", [Text.Encoding]::ASCII)
}

function Build-DosInstaller {
    param(
        [Parameter(Mandatory)][string]$StageDir,
        [Parameter(Mandatory)][string]$OutExe,
        [string]$Watcom = 'C:\WATCOM',
        [string]$MinGW  = 'C:\msys64\mingw32\bin'
    )
    $dos  = Join-Path $script:InstallersRoot 'dos'
    $work = Join-Path $script:InstallersRoot 'stage\dos-work'
    foreach ($tool in 'wcc.exe', 'wlink.exe') {
        if (-not (Test-Path (Join-Path $Watcom "binnt\$tool"))) { throw "Open Watcom tool missing: $(Join-Path $Watcom "binnt\$tool")" }
    }
    if (Test-Path -LiteralPath $work) { Remove-Item -LiteralPath $work -Recurse -Force }
    New-Item -ItemType Directory $work -Force | Out-Null
    if (Test-Path -LiteralPath $OutExe) { Remove-Item -LiteralPath $OutExe -Force }

    # the notice, compiled in
    ConvertTo-NoticeHeader -NoticePath (Join-Path $StageDir 'NOTICE.TXT') -OutPath (Join-Path $work 'notice_text.h')

    $env:WATCOM  = $Watcom
    $env:PATH    = "$Watcom\binnt;$Watcom\binw;$MinGW;$env:PATH"
    $env:INCLUDE = "$Watcom\h"

    Push-Location $work
    try {
        foreach ($n in 'nv1quake_dos_main', 'dosmain', 'dosengine', 'screen') {
            & wcc -bt=dos -ml -zq -os "-i=$work" "-i=$dos" "-fo=$n.obj" (Join-Path $dos "$n.c")
            if ($LASTEXITCODE -ne 0) { throw "wcc $n failed" }
        }
        & wlink system dos name stub.exe file nv1quake_dos_main.obj, dosmain.obj, dosengine.obj, screen.obj option quiet
        if ($LASTEXITCODE -ne 0) { throw 'wlink (DOS stub) failed' }
        & (Join-Path $MinGW 'gcc.exe') -O1 -o packer.exe (Join-Path $dos 'packer.c')
        if ($LASTEXITCODE -ne 0) { throw 'DOS packer build failed' }
        & .\packer.exe stub.exe $StageDir $OutExe
        if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $OutExe)) { throw 'DOS packer failed' }
    } finally { Pop-Location }
}
