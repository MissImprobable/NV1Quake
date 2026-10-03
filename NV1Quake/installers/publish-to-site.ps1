#Requires -Version 7.0
<#
.SYNOPSIS
    Publishes the nv1Quake installers: uploads them to R2, writes the product page
    (apps\nv1quake.html), links it from the home page and sitemap, adds the versions.json entry
    in the site repo, and pushes. DRY RUN unless -Publish is given.

.DESCRIPTION
    Run build-installers.ps1 -Rebuild first, from a clean, committed and pushed tree. -Publish
    refuses to run unless every installer and the source zip were built from HEAD, from a clean
    tree, with freshly rebuilt games, and HEAD is on origin: the notice inside each installer
    promises "a source archive of exactly this commit", and the GPL requires the source to be
    available. Without -Publish nothing is uploaded, written or pushed.

    In the site repo it commits ONLY its own files (commit --only), never whatever else happens to
    be staged there, and refuses to run if those files already have uncommitted changes.

.EXAMPLE
    .\publish-to-site.ps1            # dry run
    .\publish-to-site.ps1 -Publish   # really upload and push
#>
param(
    [string]$SiteRepo = 'X:\repos\AbnormalitySoftwareWebsite',
    [switch]$Publish
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\lib\common.ps1"

$version = Get-NV1Version
$output  = Join-Path $PSScriptRoot 'output'
if (-not (Test-Path -LiteralPath $SiteRepo)) { throw "Site repo not found at $SiteRepo" }
$plan = Get-PublishPlan -OutputDir $output -SiteRepo $SiteRepo -Version $version
$git  = Get-GitInfo
$problems = @(Test-PublishReady -OutputDir $output -HeadCommit $git.Commit -TreeDirty $git.Dirty -CommitOnOrigin (Test-CommitOnOrigin -Commit $git.Commit))
$missingAssets = @($plan.Assets | Where-Object { -not (Test-Path -LiteralPath $_.Path) } | ForEach-Object { "Missing site asset: $($_.Path)" })
$problems += $missingAssets

Write-Host "nv1Quake $version  (commit $($git.Short))" -ForegroundColor Cyan
foreach ($f in $plan.Files) { Write-Host ("  R2  abnormalitysoftware-downloads/{0}   ({1})" -f $f.R2Key, $f.Size) }
foreach ($a in $plan.Assets) { Write-Host "  asset     $($a.Path)  (committed with the page)" }
Write-Host "  page      $($plan.PagePath)"
Write-Host "  home      $($plan.IndexPath)  (adds an nv1Quake card)"
Write-Host "  sitemap   $($plan.SitemapPath)"
Write-Host "  versions  $($plan.VersionsPath)  ->  nv1quake $version  ($($plan.EmulatedUrl))"

if ($problems.Count -gt 0) {
    Write-Host "`nNOT READY TO PUBLISH:" -ForegroundColor Red
    $problems | ForEach-Object { Write-Host "  - $_" -ForegroundColor Red }
}
if (-not $Publish) {
    Write-Host "`nDRY RUN - nothing uploaded, written or pushed. Re-run with -Publish when ready." -ForegroundColor Yellow
    return
}
if ($problems.Count -gt 0) { throw "Refusing to publish: $($problems.Count) problem(s) above." }

# Refuse to publish over uncommitted edits to files we rewrite (the poster is expected to be new/untracked).
$rewritten = @('apps/nv1quake.html', 'versions.json', 'sitemap.xml', 'index.html')
$pending = git -C $SiteRepo status --porcelain -- $rewritten
if ($pending) { throw "The site repo has uncommitted changes to files this script rewrites; resolve them first:`n$pending" }

Push-Location $SiteRepo
try {
    # Upload everything first; if any upload fails nothing in the site repo has been touched yet.
    foreach ($f in $plan.Files) {
        Write-Host "Uploading $($f.Name) ..." -ForegroundColor Cyan
        npx --yes wrangler@4 r2 object put "abnormalitysoftware-downloads/$($f.R2Key)" --file $f.Path --content-type $f.ContentType --remote
        if ($LASTEXITCODE -ne 0) { throw "wrangler upload of $($f.Name) failed" }
    }
    [IO.File]::WriteAllText($plan.PagePath, $plan.PageHtml, (New-Object Text.UTF8Encoding($false)))
    Set-VersionsEntry -Path $plan.VersionsPath -Version $version -Url $plan.EmulatedUrl -PageUrl $plan.PageUrl
    Add-SitemapEntry -Path $plan.SitemapPath
    Add-HomeCard -Path $plan.IndexPath -PosterFile 'NV1Quake.jpg'
    $owned = $rewritten + @($plan.Assets | ForEach-Object { $_.GitPath })
    $committed = Publish-SiteFiles -SiteRepo $SiteRepo -Paths $owned -Message "Add nv1Quake $version product page, home card and versions.json entry"
    if (-not $committed) { Write-Host 'Nothing new to commit (already published); pushing anyway.' -ForegroundColor Yellow }
    git push
    if ($LASTEXITCODE -ne 0) { throw 'git push failed' }
} finally { Pop-Location }
Write-Host 'Published.' -ForegroundColor Green
