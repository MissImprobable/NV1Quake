Set-StrictMode -Version Latest

$script:InstallersRoot = Split-Path $PSScriptRoot -Parent
$InstallersRoot = $script:InstallersRoot
$RepoRoot = Split-Path (Split-Path $InstallersRoot -Parent) -Parent

function Get-NV1Version {
    (Get-Content (Join-Path $script:InstallersRoot 'VERSION') -Raw).Trim()
}

function Get-GitInfo {
    $commit = (git -C $RepoRoot rev-parse HEAD).Trim()
    $dirty = [bool](git -C $RepoRoot status --porcelain --untracked-files=no)
    @{
        Commit = $commit
        Short  = $commit.Substring(0, 7)
        Dirty  = $dirty
        Url    = 'https://github.com/MissImprobable/NV1Quake'
    }
}

function Expand-Template {
    param([string]$Path, [hashtable]$Values)
    $text = Get-Content $Path -Raw
    foreach ($k in $Values.Keys) { $text = $text.Replace("@@$k@@", [string]$Values[$k]) }
    $left = [regex]::Matches($text, '@@([A-Z_]+)@@') | ForEach-Object { $_.Groups[1].Value } | Select-Object -Unique
    if ($left) { throw "Template $Path has unfilled placeholder(s): $($left -join ', ')" }
    $text
}

function Assert-NoGameData {
    param([string]$Dir)
    $paks = Get-ChildItem -Path $Dir -Recurse -File -Filter '*.pak' -ErrorAction SilentlyContinue
    if ($paks) { throw "Game data found in ${Dir}: refusing to package PAK files ($($paks.Name -join ', '))" }
}

$ArtDir   = Join-Path $script:InstallersRoot 'art'
$LegalDir = Join-Path $script:InstallersRoot 'legal'

$script:VariantText = @{
    emulated = @{
        WhatItIs   = 'The software-NV1 build. It runs on a modern Windows PC and emulates the NV1 chip''s quadratic-patch rendering in software (about 22 fps on demo1 at 640x400).'
        Notes      = "- Windows 10/11, 32-bit program.`r`n- Starts in a 640x400 window."
        ThirdParty = 'This build links no NVIDIA library; it uses nvsoft, our own software implementation of the NVLIB API. It includes NVIDIA-compatible headers only at build time.'
    }
    win95 = @{
        WhatItIs   = 'The real NV1 build for Windows 95 and a Diamond Edge 3D / NV1 card. It links NVIDIA''s NVLIB and uses NVVIDMOD.DLL for video modes.'
        Notes      = "- Needs the NVIDIA/Diamond Windows 95 driver and resource manager installed (NVAPI.DLL).`r`n- Never run on real hardware yet. Start with -nosound and the cvar nv1_subdiv 0.5.`r`n- Use -vidmem 2 on an Edge 3D 2200 with its expansion modules."
        ThirdParty = "NVVIDMOD.DLL is NVIDIA's, from the redistributable folder of the NV1 SDK 1.50; NVIDIA's SDK documentation says it must be shipped with the application.`r`nOPEN QUESTION: this program statically links NVIDIA's nvlib.lib. The SDK contains no licence text, and we have not confirmed that distributing that library inside a GPL-derived program is permitted."
    }
    dos = @{
        WhatItIs   = 'The real NV1 build for DOS (DOS/4GW) and a Diamond Edge 3D / NV1 card.'
        Notes      = "- Needs the NVIDIA DOS resource manager. No sound.`r`n- Never run on real hardware yet.`r`n- Run NV1QUAKE.EXE from the install folder."
        ThirdParty = "DOS4GW.EXE is the DOS extender supplied with Open Watcom (Tenberry Software). OPEN QUESTION: we found no redistribution grant for it in the local Open Watcom documentation.`r`nOPEN QUESTION: this program statically links NVIDIA's nvlibdos.lib and nvrm.lib; the SDK contains no licence text, and we have not confirmed that distributing them inside a GPL-derived program is permitted."
    }
}

function New-LegalFiles {
    param([string]$Dir, [ValidateSet('emulated','win95','dos')][string]$Variant)
    $git = Get-GitInfo
    $t = $script:VariantText[$Variant]
    $values = @{
        VERSION = Get-NV1Version; VARIANT = $Variant; COMMIT = $git.Commit; SOURCE_URL = $git.Url
        THIRDPARTY = $t.ThirdParty; WHATITIS = $t.WhatItIs; NOTES = $t.Notes
    }
    New-Item -ItemType Directory (Join-Path $Dir 'id1') -Force | Out-Null
    Copy-Item (Join-Path $LegalDir 'COPYING.TXT') (Join-Path $Dir 'COPYING.TXT') -Force
    Copy-Item (Join-Path $LegalDir 'PUT_PAKS.TXT') (Join-Path $Dir 'id1\PUT_PAKS.TXT') -Force
    foreach ($n in 'NOTICE','README') {
        $text = Expand-Template -Path (Join-Path $LegalDir "$n.TXT.in") -Values $values
        $text = $text -replace "(?<!`r)`n", "`r`n"
        [IO.File]::WriteAllText((Join-Path $Dir "$n.TXT"), $text, [Text.Encoding]::ASCII)
    }
}
