<#
.SYNOPSIS
    Publishes the nv1Quake installers: uploads them to R2, writes the product page
    (apps\nv1quake.html), adds the versions.json entry in the site repo and pushes.
    DRY RUN unless -Publish is given.

.DESCRIPTION
    Run build-installers.ps1 first (ideally -Rebuild, on a clean, committed tree so the notice and
    source zip name the exact commit). Without -Publish this only prints what it would do and
    changes nothing anywhere. It stages only its own two files in the site repo, never everything:
    that repo often has unrelated uncommitted work.

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

Write-Host "nv1Quake $version" -ForegroundColor Cyan
foreach ($f in $plan.Files) { Write-Host ("  R2  abnormalitysoftware-downloads/{0}   ({1})" -f $f.R2Key, $f.Size) }
foreach ($a in $plan.Assets) { Write-Host "  asset     $($a.Dest)" }
Write-Host "  page      $($plan.PagePath)"
Write-Host "  versions  $($plan.VersionsPath)  ->  nv1quake $version  ($($plan.EmulatedUrl))"

if (-not $Publish) {
    Write-Host "`nDRY RUN - nothing uploaded, written or pushed. Re-run with -Publish." -ForegroundColor Yellow
    return
}

# Refuse to publish over uncommitted edits to the two files we own in the site repo.
$owned = @('apps/nv1quake.html', 'versions.json') + @($plan.Assets | ForEach-Object { $_.GitPath })
$pending = git -C $SiteRepo status --porcelain -- $owned
if ($pending) { throw "The site repo has uncommitted changes to files this script owns; resolve them first:`n$pending" }

Push-Location $SiteRepo
try {
    # Upload everything first; if any upload fails nothing in the site repo has been touched yet.
    foreach ($f in $plan.Files) {
        Write-Host "Uploading $($f.Name) ..." -ForegroundColor Cyan
        npx --yes wrangler@4 r2 object put "abnormalitysoftware-downloads/$($f.R2Key)" --file $f.Path --content-type $f.ContentType --remote
        if ($LASTEXITCODE -ne 0) { throw "wrangler upload of $($f.Name) failed" }
    }
    foreach ($a in $plan.Assets) { Copy-Item -LiteralPath $a.Source -Destination $a.Dest -Force }
    [IO.File]::WriteAllText($plan.PagePath, $plan.PageHtml, (New-Object Text.UTF8Encoding($false)))
    Set-VersionsEntry -Path $plan.VersionsPath -Version $version -Url $plan.EmulatedUrl -PageUrl $plan.PageUrl
    git add -- 'apps/nv1quake.html' 'versions.json' ($plan.Assets | ForEach-Object { $_.GitPath })
    git commit -m "Add nv1Quake $version product page and versions.json entry"
    if ($LASTEXITCODE -ne 0) { throw 'git commit failed' }
    git push
    if ($LASTEXITCODE -ne 0) { throw 'git push failed' }
} finally { Pop-Location }
Write-Host 'Published.' -ForegroundColor Green
