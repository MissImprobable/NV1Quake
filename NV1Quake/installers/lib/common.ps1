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
