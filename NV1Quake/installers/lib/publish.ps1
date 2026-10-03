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
        # The product art is Abigail's own, already in the site's assets folder (it is deliberately not
        # copied into this GPL repo). The publish commits it so the page does not show a broken image.
        Assets       = @([pscustomobject]@{
            Path    = Join-Path $SiteRepo 'assets\NV1Quake.jpg'
            GitPath = 'assets/NV1Quake.jpg'
        })
        SitemapPath  = Join-Path $SiteRepo 'sitemap.xml'
        IndexPath    = Join-Path $SiteRepo 'index.html'
        PagePath     = Join-Path $SiteRepo 'apps\nv1quake.html'
        VersionsPath = Join-Path $SiteRepo 'versions.json'
        EmulatedUrl  = "https://abnormalitysoftware.com/dl/NV1Quake/$($files[0].Name)"
        PageUrl      = 'https://abnormalitysoftware.com/apps/nv1quake'
        PageHtml     = Expand-Template -Path (Join-Path $script:InstallersRoot 'site\nv1quake.html.in') -Values $values
    }
}

# Adds the product page to sitemap.xml, right after DiskPixie's entry. A text insert that keeps the
# file's line endings, idempotent, and it fails loudly if the anchor entry is gone rather than
# silently skipping.
function Add-SitemapEntry {
    param([string]$Path)
    $text = [IO.File]::ReadAllText($Path)
    if ($text.Contains('/apps/nv1quake<')) { return }
    $m = [regex]::Match($text, '(?m)^([ \t]*)<url><loc>https://abnormalitysoftware\.com/apps/diskpixie</loc></url>(\r?\n)')
    if (-not $m.Success) { throw "$Path has no diskpixie entry to anchor the nv1quake entry after" }
    $entry = $m.Groups[1].Value + '<url><loc>https://abnormalitysoftware.com/apps/nv1quake</loc></url>' + $m.Groups[2].Value
    $text = $text.Insert($m.Index + $m.Length, $entry)
    [IO.File]::WriteAllText($Path, $text, (New-Object Text.UTF8Encoding($false)))
}

# Adds an nv1Quake card to the home page, before the Fragged LAN Manager card (same markup as the other
# app cards). Text insert, idempotent, fails loudly if the anchor comment is missing.
function Add-HomeCard {
    param([string]$Path, [string]$PosterFile)
    $text = [IO.File]::ReadAllText($Path)
    if ($text.Contains('href="apps/nv1quake"')) { return }
    $m = [regex]::Match($text, '(?m)^([ \t]*)<!-- Fragged LAN Manager -->')
    if (-not $m.Success) { throw "$Path has no '<!-- Fragged LAN Manager -->' comment to anchor the nv1Quake card before" }
    $nl = if ($text.Contains("`r`n")) { "`r`n" } else { "`n" }
    $i = $m.Groups[1].Value
    $lines = @(
        '<!-- nv1Quake -->',
        '<div class="cute-card overflow-hidden fade-in">',
        "    <a href=`"apps/nv1quake`"><img src=`"assets/$PosterFile`" class=`"w-full`" alt=`"nv1Quake Poster`"></a>",
        '    <div class="p-6">',
        '        <h2 class="text-2xl font-semibold mb-3" style="color: var(--text-main);">nv1Quake</h2>',
        '        <p class="mb-5" style="color: var(--text-body);">',
        '            Quake on the NVIDIA NV1 - a community experiment porting GLQuake to the 1995',
        '            accelerator that drew quadratic patches instead of triangles. Windows 10/11',
        '            (emulated), Windows 95 and DOS builds, free, with source.',
        '        </p>',
        '        <a href="apps/nv1quake" class="cute-button-sm">More Information</a>',
        '    </div>',
        '</div>',
        ''
    )
    $card = (($lines | ForEach-Object { if ($_ -eq '') { '' } else { $i + $_ } }) -join $nl) + $nl
    $text = $text.Insert($m.Index, $card)
    [IO.File]::WriteAllText($Path, $text, (New-Object Text.UTF8Encoding($false)))
}

# Commits ONLY the given paths in the site repo. A bare `git commit` would also commit whatever the
# user happens to have staged there (unrelated half-done site work, which would then be pushed and
# deployed), so this uses `commit --only`. Returns $false, without error, if the paths are already
# up to date, so re-running a publish (or re-pushing after a failed push) just carries on.
function Publish-SiteFiles {
    param([string]$SiteRepo, [string[]]$Paths, [string]$Message)
    git -C $SiteRepo add -- @Paths
    if ($LASTEXITCODE -ne 0) { throw 'git add failed' }
    git -C $SiteRepo diff --cached --quiet -- @Paths
    if ($LASTEXITCODE -eq 0) { return $false }
    git -C $SiteRepo commit -q --only -m $Message -- @Paths
    if ($LASTEXITCODE -ne 0) { throw 'git commit failed' }
    return $true
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
