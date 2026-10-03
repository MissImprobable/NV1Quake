# Website publishing helpers.

# Works out everything a publish would do, without doing any of it: the four artefacts with their
# sizes and hashes, their R2 keys, and the rendered product page. Throws if anything is missing or
# if game data has found its way into the output folder.
function Get-PublishPlan {
    param([string]$OutputDir, [string]$SiteRepo, [string]$Version)
    $names = @(
        "NV1Quake-$Version-Emulated-Setup.exe",
        "NV1Quake-$Version-Win95-Setup.exe",
        "NV1Quake-$Version-DOS-Setup.exe",
        "NV1Quake-$Version-source.zip"
    )
    foreach ($n in $names) {
        if (-not (Test-Path -LiteralPath (Join-Path $OutputDir $n))) { throw "Missing $n in $OutputDir - run build-installers.ps1 first" }
    }
    Assert-NoGameData -Dir $OutputDir

    $files = foreach ($n in $names) {
        $p = Join-Path $OutputDir $n
        [pscustomobject]@{
            Name        = $n
            Path        = $p
            Size        = '{0:N1} MB' -f ((Get-Item -LiteralPath $p).Length / 1MB)
            Sha         = (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLower()
            R2Key       = "NV1Quake/$n"
            ContentType = if ($n.EndsWith('.zip')) { 'application/zip' } else { 'application/octet-stream' }
        }
    }
    $values = @{
        VERSION  = $Version
        EMU_FILE = $files[0].Name; EMU_SIZE = $files[0].Size; EMU_SHA = $files[0].Sha
        W95_FILE = $files[1].Name; W95_SIZE = $files[1].Size; W95_SHA = $files[1].Sha
        DOS_FILE = $files[2].Name; DOS_SIZE = $files[2].Size; DOS_SHA = $files[2].Sha
        SRC_FILE = $files[3].Name; SRC_SIZE = $files[3].Size; SRC_SHA = $files[3].Sha
    }
    [pscustomobject]@{
        Version      = $Version
        Files        = @($files)
        Assets       = @([pscustomobject]@{
            Source = Join-Path $script:InstallersRoot 'art\nv1quake-poster.png'
            Dest   = Join-Path $SiteRepo 'assets\nv1quake-poster.png'
            GitPath = 'assets/nv1quake-poster.png'
        })
        PagePath     = Join-Path $SiteRepo 'apps\nv1quake.html'
        VersionsPath = Join-Path $SiteRepo 'versions.json'
        EmulatedUrl  = "https://abnormalitysoftware.com/dl/NV1Quake/$($files[0].Name)"
        PageUrl      = 'https://abnormalitysoftware.com/apps/nv1quake'
        PageHtml     = Expand-Template -Path (Join-Path $script:InstallersRoot 'site\nv1quake.html.in') -Values $values
    }
}

# Adds or replaces the "nv1quake" entry in the site's versions.json by editing the TEXT, not by
# round-tripping through ConvertTo-Json: that would escape apostrophes ("Abigail's" -> ')
# and reformat every other app's entry, polluting the site repo's diff.
function Set-VersionsEntry {
    param([string]$Path, [string]$Version, [string]$Url, [string]$PageUrl)
    $text = [IO.File]::ReadAllText($Path)
    $nl = if ($text.Contains("`r`n")) { "`r`n" } else { "`n" }

    $block = ('  "nv1quake": {{{0}    "name": "nv1Quake",{0}    "version": "{1}",{0}    "url": "{2}",{0}    "pageUrl": "{3}"{0}  }}' -f $nl, $Version, $Url, $PageUrl)

    $existing = [regex]::Match($text, '  "nv1quake":\s*\{[^}]*\}')
    if ($existing.Success) {
        $text = $text.Substring(0, $existing.Index) + $block + $text.Substring($existing.Index + $existing.Length)
    } else {
        $close = $text.LastIndexOf('}')
        if ($close -lt 0) { throw "$Path is not a JSON object" }
        $head = $text.Substring(0, $close).TrimEnd()
        $tail = $text.Substring($close)
        $text = $head + ',' + $nl + $block + $nl + $tail
    }
    [IO.File]::WriteAllText($Path, $text, (New-Object Text.UTF8Encoding($false)))
    $null = $text | ConvertFrom-Json   # throws if the result is not valid JSON
}
