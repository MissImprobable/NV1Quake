<#
.SYNOPSIS
    Builds the nv1Quake installers (emulated / win95 / dos), a source zip and checksums.

.DESCRIPTION
    Each installer is built from the game executables in NV1Quake\ (nv1quake.exe, nv1q95.exe,
    nv1qdos.exe). Use -Rebuild so they are rebuilt first and an installer is never built from a
    stale binary. Nothing under id1\ or any *.PAK is ever packaged.

.EXAMPLE
    .\build-installers.ps1 -Rebuild          # rebuild all three games, then all three installers
    .\build-installers.ps1 -Variant dos      # only the DOS installer, from the existing nv1qdos.exe
#>
param(
    [ValidateSet('emulated', 'win95', 'dos', 'all')][string]$Variant = 'all',
    [switch]$Rebuild,
    [switch]$UpdateRelease,
    [string]$Inno   = 'C:\Program Files\Inno Setup 7\ISCC.exe',
    [string]$MinGW  = 'C:\msys64\mingw32\bin',
    [string]$Watcom = 'C:\WATCOM'
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\lib\common.ps1"

$version  = Get-NV1Version
$git      = Get-GitInfo
$buildDir = Split-Path $PSScriptRoot -Parent          # NV1Quake\
$stage    = Join-Path $PSScriptRoot 'stage'
$output   = Join-Path $PSScriptRoot 'output'
$variants = if ($Variant -eq 'all') { @('emulated', 'win95', 'dos') } else { @($Variant) }

if ($git.Dirty) { Write-Warning 'Tracked files have uncommitted changes; the source zip (git archive) will NOT contain them.' }
New-Item -ItemType Directory $output -Force | Out-Null

# 1. Rebuild the games so no installer is ever built from a stale binary.
if ($Rebuild) {
    if ($variants -contains 'emulated') { Invoke-Build 'game: emulated (MSVC)' $buildDir 'build.bat' }
    if ($variants -contains 'win95')    { Invoke-Build 'game: Win95 (Watcom)'  $buildDir 'build-nv1.bat' }
    if ($variants -contains 'dos')      { Invoke-Build 'game: DOS (Watcom)'    $buildDir 'build-dos.bat' }
}

foreach ($v in $variants) {
    Write-Host "`n##### $v #####" -ForegroundColor Yellow
    Stage-Variant -Variant $v -StageRoot $stage -BuildDir $buildDir
    $sd = [IO.Path]::GetFullPath((Join-Path $stage $v))

    switch ($v) {
        'emulated' {
            if (-not (Test-Path -LiteralPath $Inno)) { throw "Inno Setup not found at $Inno (use -Inno)" }
            $expected = Join-Path $output "NV1Quake-$version-Emulated-Setup.exe"
            if (Test-Path -LiteralPath $expected) { Remove-Item -LiteralPath $expected -Force }
            & $Inno "/DAppVersion=$version" "/DStageDir=$sd" "/DArtDir=$([IO.Path]::GetFullPath($ArtDir))" "/DOutDir=$([IO.Path]::GetFullPath($output))" (Join-Path $PSScriptRoot 'inno\NV1Quake-Emulated.iss')
            if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $expected)) { throw 'Inno Setup compile failed' }
        }
        'win95' {
            Build-RetroInstaller -StageDir $sd -OutExe (Join-Path $output "NV1Quake-$version-Win95-Setup.exe") `
                -IconPath (Join-Path $ArtDir 'retro-app-small.ico') -NoticePath (Join-Path $sd 'NOTICE.TXT') -MinGW $MinGW
        }
        'dos' {
            Build-DosInstaller -StageDir $sd -OutExe (Join-Path $output "NV1Quake-$version-DOS-Setup.exe") -Watcom $Watcom -MinGW $MinGW
        }
    }
}

# 2. Source offer + checksums (covers whatever is in output\).
New-SourceZip -OutPath (Join-Path $output "NV1Quake-$version-source.zip")
$sums = Get-ChildItem $output -File | Where-Object { $_.Name -ne 'SHA256SUMS.txt' } | Sort-Object Name | ForEach-Object {
    '{0}  {1}' -f (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLower(), $_.Name
}
Set-Content (Join-Path $output 'SHA256SUMS.txt') $sums -Encoding ASCII

# 3. Optionally refresh the hand-assembled Release\ folders from the new builds.
if ($UpdateRelease) {
    $rel = Join-Path (Split-Path $buildDir -Parent) 'Release'
    $map = @{ 'nv1quake.exe' = 'NV1Quake-Emulated\nv1quake.exe'; 'nv1q95.exe' = 'NV1Quake\NV1QUAKE.EXE'; 'nv1qdos.exe' = 'NV1Quake-DOS\NV1QUAKE.EXE' }
    foreach ($k in $map.Keys) {
        $dest = Join-Path $rel $map[$k]
        if (Test-Path -LiteralPath (Split-Path $dest -Parent)) { Copy-Item (Join-Path $buildDir $k) $dest -Force; Write-Host "Release refreshed: $($map[$k])" }
    }
}

Write-Host "`nBuilt (version $version, commit $($git.Short)):" -ForegroundColor Green
Get-ChildItem $output -File | Select-Object Name, @{ n = 'MB'; e = { [math]::Round($_.Length / 1MB, 2) } } | Format-Table -AutoSize
