# Staging, build invocation and the source-offer zip.

$script:BuildDirDefault = Split-Path $script:InstallersRoot -Parent   # NV1Quake\
$script:Dos4gwDefault   = 'C:\WATCOM\binw\dos4gw.exe'

function Stage-Variant {
    param(
        [ValidateSet('emulated', 'win95', 'dos')][string]$Variant,
        [string]$StageRoot,
        [string]$BuildDir = $script:BuildDirDefault,
        [string]$Dos4gw   = $script:Dos4gwDefault
    )
    $exeName = @{ emulated = 'nv1quake.exe'; win95 = 'nv1q95.exe'; dos = 'nv1qdos.exe' }[$Variant]
    $exe = Join-Path $BuildDir $exeName
    if (-not (Test-Path -LiteralPath $exe)) { throw "Built executable missing: $exe (run with -Rebuild)" }

    $dir = Join-Path $StageRoot $Variant
    if (Test-Path -LiteralPath $dir) { Remove-Item -LiteralPath $dir -Recurse -Force }
    New-Item -ItemType Directory $dir -Force | Out-Null

    Copy-Item -LiteralPath $exe (Join-Path $dir 'NV1QUAKE.EXE')
    switch ($Variant) {
        'win95' {
            $dll = Join-Path $RepoRoot 'NV1\NV\SDK\REDIST\NVVIDMOD.DLL'
            if (-not (Test-Path -LiteralPath $dll)) { throw "NVVIDMOD.DLL not found at $dll (the NV1 SDK must be unpacked in NV1\)" }
            Copy-Item -LiteralPath $dll (Join-Path $dir 'NVVIDMOD.DLL')
            Copy-Item (Join-Path $ArtDir 'retro-app-small.ico') (Join-Path $dir 'nv1quake.ico')
        }
        'dos' {
            if (-not (Test-Path -LiteralPath $Dos4gw)) { throw "DOS4GW.EXE not found at $Dos4gw (Open Watcom is required)" }
            Copy-Item -LiteralPath $Dos4gw (Join-Path $dir 'DOS4GW.EXE')
        }
    }
    New-LegalFiles -Dir $dir -Variant $Variant
    Assert-NoGameData -Dir $dir
}

function Invoke-Build {
    param([string]$Name, [string]$Dir, [string]$Command)
    Write-Host "=== $Name ===" -ForegroundColor Cyan
    Push-Location $Dir
    try {
        cmd /c ".\$Command"
        if ($LASTEXITCODE -ne 0) { throw "$Name failed (exit $LASTEXITCODE)" }
    } finally { Pop-Location }
}

function New-SourceZip {
    param([string]$OutPath)
    if (Test-Path -LiteralPath $OutPath) { Remove-Item -LiteralPath $OutPath -Force }
    $git = Get-GitInfo
    $prefix = "NV1Quake-$(Get-NV1Version)-source/"
    git -C $RepoRoot archive --format=zip "--prefix=$prefix" -o $OutPath $git.Commit
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $OutPath)) { throw 'git archive failed' }
}
