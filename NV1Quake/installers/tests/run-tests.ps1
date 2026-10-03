$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\..\lib\common.ps1"

$script:Failures = 0
function Assert-Equal($Actual, $Expected, $Name) {
    if ($Actual -ceq $Expected) { Write-Host "PASS $Name" -ForegroundColor Green }
    else { Write-Host "FAIL $Name`n  expected: $Expected`n  actual:   $Actual" -ForegroundColor Red; $script:Failures++ }
}
function Assert-Throws([scriptblock]$Block, $Pattern, $Name) {
    try { & $Block; Write-Host "FAIL $Name (did not throw)" -ForegroundColor Red; $script:Failures++ }
    catch {
        if ($_.Exception.Message -match $Pattern) { Write-Host "PASS $Name" -ForegroundColor Green }
        else { Write-Host "FAIL $Name (wrong error: $($_.Exception.Message))" -ForegroundColor Red; $script:Failures++ }
    }
}

# --- Get-NV1Version
Assert-Equal (Get-NV1Version) '0.10' 'version is read from VERSION'

# --- Expand-Template
$tpl = Join-Path $env:TEMP 'nv1-tpl-test.txt'
Set-Content $tpl 'v@@VERSION@@ at @@COMMIT@@' -NoNewline
Assert-Equal (Expand-Template -Path $tpl -Values @{ VERSION = '1.2'; COMMIT = 'abc' }) 'v1.2 at abc' 'template substitution'
Assert-Throws { Expand-Template -Path $tpl -Values @{ VERSION = '1.2' } } 'COMMIT' 'unfilled placeholder is an error'

# --- Assert-NoGameData
$d = Join-Path $env:TEMP 'nv1-nodata-test'
Remove-Item $d -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory "$d\id1" -Force | Out-Null
Set-Content "$d\id1\PUT_PAKS.TXT" 'put your pak files here'
Assert-NoGameData -Dir $d
Assert-Equal $true $true 'placeholder text file in id1 is allowed'
Set-Content "$d\id1\PAK0.PAK" 'x'
Assert-Throws { Assert-NoGameData -Dir $d } 'PAK' 'PAK file is rejected'
Remove-Item "$d\id1\PAK0.PAK"
New-Item -ItemType Directory "$d\sub" | Out-Null
Set-Content "$d\sub\pak1.pak" 'x'
Assert-Throws { Assert-NoGameData -Dir $d } 'PAK' 'lowercase .pak in a subfolder is rejected'
Remove-Item $d -Recurse -Force

# --- New-LegalFiles
foreach ($v in 'emulated','win95','dos') {
    $d = Join-Path $env:TEMP "nv1-legal-$v"
    Remove-Item $d -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory $d | Out-Null
    New-LegalFiles -Dir $d -Variant $v
    foreach ($f in 'COPYING.TXT','NOTICE.TXT','README.TXT','id1\PUT_PAKS.TXT') {
        Assert-Equal (Test-Path "$d\$f") $true "$v has $f"
    }
    Assert-Equal ((Get-FileHash "$d\COPYING.TXT").Hash -eq (Get-FileHash 'X:\repos\Quake\gnu.txt').Hash) $true "$v COPYING.TXT is id's gnu.txt, unmodified"
    $n = Get-Content "$d\NOTICE.TXT" -Raw
    Assert-Equal ($n -match 'passionate fans of id Software') $true "$v notice has the community statement"
    Assert-Equal ($n -match 'comply immediately') $true "$v notice has the takedown promise"
    Assert-Equal ($n -match 'Installer created by Abigail / Abnormality Software \(2026\)') $true "$v notice has the credit line"
    Assert-Equal ($n -match '@@') $false "$v notice has no unfilled placeholders"
    Assert-NoGameData -Dir $d
    Remove-Item $d -Recurse -Force
}

# --- New-LegalFiles with a RELATIVE directory (.NET resolves against the process cwd, not the PowerShell location)
$rel = Join-Path $env:TEMP 'nv1-legal-rel'
Remove-Item $rel -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory "$rel\work" | Out-Null
Push-Location "$rel\work"
New-Item -ItemType Directory 'stage-here' | Out-Null
New-LegalFiles -Dir 'stage-here' -Variant dos
Pop-Location
Assert-Equal (Test-Path "$rel\work\stage-here\NOTICE.TXT") $true 'relative -Dir writes where PowerShell is'
Remove-Item $rel -Recurse -Force

# --- ConvertTo-NoticeHeader
$n = Join-Path $env:TEMP 'nv1-notice.txt'
Set-Content $n ('say "hi" \ ' + ('word ' * 40)) -Encoding ASCII
$h = Join-Path $env:TEMP 'nv1-notice.h'
ConvertTo-NoticeHeader -NoticePath $n -OutPath $h
$hc = Get-Content $h -Raw
Assert-Equal ($hc -match 'NOTICE_LINE_COUNT') $true 'header defines NOTICE_LINE_COUNT'
Assert-Equal ($hc -match '\\"hi\\"') $true 'quotes are escaped'
Assert-Equal ($hc -match '\\\\ ') $true 'backslashes are escaped'
Assert-Equal (@(Get-Content $h | Where-Object { $_.Length -gt 90 }).Count) 0 'lines are wrapped'
$counted = [int](([regex]::Match($hc, 'NOTICE_LINE_COUNT (\d+)')).Groups[1].Value)
$entries = @(Get-Content $h | Where-Object { $_ -match '^\s+".*",$' }).Count
Assert-Equal $entries $counted 'NOTICE_LINE_COUNT matches the number of strings'
$empty = Join-Path $env:TEMP 'nv1-notice-blank.txt'
Set-Content $empty "line one`r`n`r`nline three" -Encoding ASCII
ConvertTo-NoticeHeader -NoticePath $empty -OutPath $h
Assert-Equal (@(Get-Content $h | Where-Object { $_ -match '^\s+"",$' }).Count) 1 'blank lines are kept'

# --- Stage-Variant (uses fake binaries, so no real build is needed)
$fake = Join-Path $env:TEMP 'nv1-fakebuild'
Remove-Item $fake -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory $fake | Out-Null
foreach ($n in 'nv1quake.exe','nv1q95.exe','nv1qdos.exe') { Set-Content "$fake\$n" 'MZ' }
$stageRoot = Join-Path $env:TEMP 'nv1-stageroot'
Remove-Item $stageRoot -Recurse -Force -ErrorAction SilentlyContinue
foreach ($v in 'emulated','win95','dos') {
    Stage-Variant -Variant $v -StageRoot $stageRoot -BuildDir $fake
    Assert-Equal (Test-Path "$stageRoot\$v\NV1QUAKE.EXE") $true "$v stage has NV1QUAKE.EXE"
    Assert-Equal (Test-Path "$stageRoot\$v\NOTICE.TXT") $true "$v stage has NOTICE.TXT"
    Assert-Equal (Test-Path "$stageRoot\$v\id1\PUT_PAKS.TXT") $true "$v stage has the id1 placeholder"
}
Assert-Equal (Test-Path "$stageRoot\win95\NVVIDMOD.DLL") $true 'win95 stage has NVVIDMOD.DLL'
Assert-Equal (Test-Path "$stageRoot\win95\nv1quake.ico") $true 'win95 stage has the icon'
Assert-Equal (Test-Path "$stageRoot\dos\DOS4GW.EXE") $true 'dos stage has DOS4GW.EXE'
Assert-Equal (Test-Path "$stageRoot\emulated\NVVIDMOD.DLL") $false 'emulated stage has no NVIDIA DLL'
Assert-Equal (Test-Path "$stageRoot\emulated\DOS4GW.EXE") $false 'emulated stage has no DOS extender'
Remove-Item "$fake\nv1q95.exe"
Assert-Throws { Stage-Variant -Variant win95 -StageRoot $stageRoot -BuildDir $fake } 'nv1q95.exe' 'missing exe aborts staging'
Set-Content "$fake\nv1qdos.exe" 'MZ'
Set-Content "$stageRoot\dos\STALE.TXT" 'leftover from an earlier build'
New-Item -ItemType Directory "$stageRoot\dos\id1" -Force | Out-Null
Set-Content "$stageRoot\dos\id1\PAK0.PAK" 'x'
Stage-Variant -Variant dos -StageRoot $stageRoot -BuildDir $fake
Assert-Equal (Test-Path "$stageRoot\dos\id1\PAK0.PAK") $false 're-staging wipes a leftover PAK'
Assert-Equal (Test-Path "$stageRoot\dos\STALE.TXT") $false 're-staging wipes stale files'
Remove-Item $fake -Recurse -Force
Remove-Item $stageRoot -Recurse -Force

# --- New-SourceZip: really contains the port, and no game data
$zipPath = Join-Path $env:TEMP 'nv1-test-source.zip'
New-SourceZip -OutPath $zipPath
Add-Type -AssemblyName System.IO.Compression.FileSystem
$z = [IO.Compression.ZipFile]::OpenRead($zipPath)
$names = $z.Entries | ForEach-Object { $_.FullName }
$z.Dispose()
Assert-Equal (@($names | Where-Object { $_ -match '\.pak$' }).Count) 0 'source zip has no PAK files'
Assert-Equal (@($names | Where-Object { $_ -match 'NV1Quake/src/nv1_rmain\.c$' }).Count) 1 'source zip contains the port'
Assert-Equal (@($names | Where-Object { $_ -match '^NV1Quake-0\.10-source/' }).Count -gt 0) $true 'source zip has the versioned prefix'
Remove-Item $zipPath -Force

# --- Set-VersionsEntry: edits the text in place (ConvertTo-Json would rewrite apostrophes and formatting)
$vj = Join-Path $env:TEMP 'nv1-versions.json'
$orig = "{`r`n  `"renamer`": {`r`n    `"name`": `"Abigail's Media Renamer`",`r`n    `"version`": `"1.0.42`",`r`n    `"url`": `"u1`",`r`n    `"pageUrl`": `"p1`"`r`n  },`r`n  `"bindery`": {`r`n    `"name`": `"Bindery`",`r`n    `"version`": `"1.0.0`",`r`n    `"url`": `"u2`",`r`n    `"pageUrl`": `"p2`"`r`n  }`r`n}"
[IO.File]::WriteAllText($vj, $orig, (New-Object Text.UTF8Encoding($false)))
Set-VersionsEntry -Path $vj -Version '0.10' -Url 'https://x/e.exe' -PageUrl 'https://x/apps/nv1quake'
$after1 = [IO.File]::ReadAllText($vj)
$j = $after1 | ConvertFrom-Json
Assert-Equal $j.nv1quake.version '0.10' 'new entry is added'
Assert-Equal $j.nv1quake.name 'nv1Quake' 'entry has a name'
Assert-Equal $j.bindery.version '1.0.0' 'other apps are untouched'
Assert-Equal $after1.StartsWith($orig.Substring(0, $orig.Length - 3)) $true 'existing text is byte-identical (apostrophe, CRLF, indent kept)'
Set-VersionsEntry -Path $vj -Version '0.11' -Url 'https://x/e2.exe' -PageUrl 'https://x/apps/nv1quake'
$after2 = [IO.File]::ReadAllText($vj)
$j = $after2 | ConvertFrom-Json
Assert-Equal $j.nv1quake.version '0.11' 'second publish replaces the version'
Assert-Equal ([regex]::Matches($after2, '"nv1quake"').Count) 1 'no duplicate entry after a second publish'
Assert-Equal $after2.StartsWith($orig.Substring(0, $orig.Length - 3)) $true 'existing text still byte-identical after the second publish'
Assert-Equal ($after2 -match "`r`n") $true 'CRLF line endings preserved'
Assert-Equal (@($j.PSObject.Properties.Name).Count) 3 'exactly three apps listed'
Remove-Item $vj

# --- Get-PublishPlan
$po = Join-Path $env:TEMP 'nv1-pubout'
Remove-Item $po -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory $po | Out-Null
foreach ($n in 'NV1Quake-0.10-Emulated-Setup.exe','NV1Quake-0.10-Win95-Setup.exe','NV1Quake-0.10-DOS-Setup.exe','NV1Quake-0.10-source.zip') { Set-Content "$po\$n" "data for $n" }
$plan = Get-PublishPlan -OutputDir $po -SiteRepo 'X:\fake-site' -Version '0.10'
Assert-Equal $plan.Files.Count 4 'plan lists four files'
Assert-Equal $plan.Files[0].R2Key 'NV1Quake/NV1Quake-0.10-Emulated-Setup.exe' 'R2 key uses the NV1Quake/ folder'
Assert-Equal $plan.Files[3].ContentType 'application/zip' 'zip content type'
Assert-Equal $plan.Files[0].Sha.Length 64 'SHA-256 is computed'
Assert-Equal $plan.PagePath 'X:\fake-site\apps\nv1quake.html' 'page path inside the site repo'
Assert-Equal ($plan.PageHtml -match '@@') $false 'rendered page has no unfilled placeholders'
foreach ($n in 'NV1Quake-0.10-Emulated-Setup.exe','NV1Quake-0.10-Win95-Setup.exe','NV1Quake-0.10-DOS-Setup.exe','NV1Quake-0.10-source.zip') {
    Assert-Equal ($plan.PageHtml -match [regex]::Escape("/dl/NV1Quake/$n")) $true "page links $n via the counted /dl/ path"
}
Assert-Equal ($plan.PageHtml -match 'passionate fans of id Software and NVIDIA') $true 'page has the community statement'
Assert-Equal ($plan.PageHtml -match 'we will comply immediately') $true 'page has the takedown promise'
Assert-Equal ($plan.PageHtml -match 'twitch\.tv/carcenomy') $true 'page credits Carcenomy'
Assert-Equal ($plan.PageHtml -match 'never been run on a real NV1') $true 'page states the hardware builds are untested'
Assert-Equal $plan.Assets.Count 1 'plan has one site asset (the poster)'
Assert-Equal $plan.Assets[0].Path 'X:\fake-site\assets\NV1Quake.jpg' 'poster is the art already in the site assets folder'
Assert-Equal $plan.Assets[0].GitPath 'assets/NV1Quake.jpg' 'poster git path'
Assert-Equal ($plan.PageHtml -match 'assets/NV1Quake\.jpg') $true 'page uses the NV1Quake poster'
Assert-Equal ($plan.PageHtml -match 'fragged-poster') $false 'page does not use the Fragged LAN Manager poster'
Assert-Equal $plan.SitemapPath 'X:\fake-site\sitemap.xml' 'plan knows the sitemap'
Assert-Equal $plan.IndexPath 'X:\fake-site\index.html' 'plan knows the home page'
Remove-Item "$po\NV1Quake-0.10-DOS-Setup.exe"
Assert-Throws { Get-PublishPlan -OutputDir $po -SiteRepo 'X:\fake-site' -Version '0.10' } 'DOS-Setup' 'a missing installer aborts the plan'
Set-Content "$po\NV1Quake-0.10-DOS-Setup.exe" 'x'
Set-Content "$po\PAK0.PAK" 'x'
Assert-Throws { Get-PublishPlan -OutputDir $po -SiteRepo 'X:\fake-site' -Version '0.10' } 'PAK' 'game data in output aborts the plan'
Remove-Item $po -Recurse -Force

# --- Add-SitemapEntry / Add-HomeCard: make the page reachable (text inserts, idempotent, fail loudly)
$sm = Join-Path $env:TEMP 'nv1-sitemap.xml'
[IO.File]::WriteAllText($sm, "<urlset>`r`n  <url><loc>https://abnormalitysoftware.com/apps/bindery</loc></url>`r`n  <url><loc>https://abnormalitysoftware.com/apps/diskpixie</loc></url>`r`n  <url><loc>https://abnormalitysoftware.com/apps/fragged</loc></url>`r`n</urlset>`r`n")
$orig = [IO.File]::ReadAllText($sm)
Add-SitemapEntry -Path $sm
Add-SitemapEntry -Path $sm
$t = [IO.File]::ReadAllText($sm)
Assert-Equal ([regex]::Matches($t, 'apps/nv1quake').Count) 1 'sitemap entry added exactly once (idempotent)'
Assert-Equal ($t.Replace("  <url><loc>https://abnormalitysoftware.com/apps/nv1quake</loc></url>`r`n", '') -ceq $orig) $true 'sitemap otherwise byte-identical'
Assert-Equal ($t -match "diskpixie</loc></url>`r`n  <url><loc>https://abnormalitysoftware.com/apps/nv1quake") $true 'entry sits after DiskPixie, CRLF kept'
[IO.File]::WriteAllText($sm, '<urlset></urlset>')
Assert-Throws { Add-SitemapEntry -Path $sm } 'diskpixie' 'sitemap without the anchor entry fails loudly'
Remove-Item $sm

$ix = Join-Path $env:TEMP 'nv1-index.html'
[IO.File]::WriteAllText($ix, "<div>`r`n        <!-- DiskPixie -->`r`n        <div>dp</div>`r`n`r`n        <!-- Fragged LAN Manager -->`r`n        <div>fr</div>`r`n</div>`r`n")
$orig = [IO.File]::ReadAllText($ix)
Add-HomeCard -Path $ix -PosterFile 'NV1Quake.jpg'
Add-HomeCard -Path $ix -PosterFile 'NV1Quake.jpg'
$t = [IO.File]::ReadAllText($ix)
Assert-Equal ([regex]::Matches($t, 'href="apps/nv1quake"').Count) 2 'home card links the product page (poster + button), once'
Assert-Equal ($t -match 'assets/NV1Quake\.jpg') $true 'home card uses the poster'
Assert-Equal ($t.IndexOf('<!-- nv1Quake -->') -lt $t.IndexOf('<!-- Fragged LAN Manager -->')) $true 'card is inserted before the Fragged card'
Assert-Equal ($t.Contains($orig.Substring(0, $orig.IndexOf('        <!-- Fragged')))) $true 'text before the insertion point is untouched'
[IO.File]::WriteAllText($ix, '<div>no markers here</div>')
Assert-Throws { Add-HomeCard -Path $ix -PosterFile 'NV1Quake.jpg' } 'Fragged' 'home page without the anchor fails loudly'
Remove-Item $ix

# --- entry scripts require PowerShell 7 (5.1 reads the UTF-8 page template as ANSI and would publish garbled text)
foreach ($s in 'build-installers.ps1', 'publish-to-site.ps1') {
    $first = (Get-Content (Join-Path $InstallersRoot $s) -TotalCount 1)
    Assert-Equal ($first -match '^#Requires -Version 7') $true "$s requires PowerShell 7"
}

# --- Publish-SiteFiles: commits ONLY the named paths, even if the user has other things staged
$gr = Join-Path $env:TEMP 'nv1-gitsite'
if (Test-Path -LiteralPath $gr) { Remove-Item -LiteralPath $gr -Recurse -Force }
New-Item -ItemType Directory "$gr\apps", "$gr\assets" | Out-Null
git -C $gr init -q 2>$null
git -C $gr config user.email 'test@example.com'; git -C $gr config user.name 'Test'
Set-Content "$gr\versions.json" '{ "a": 1 }'
Set-Content "$gr\unrelated.txt" 'original'
git -C $gr add -A 2>$null; git -C $gr commit -q -m initial 2>$null
# the user has unrelated work staged
Set-Content "$gr\unrelated.txt" 'half-done edit'
Set-Content "$gr\half-done-page.html" '<p>wip</p>'
git -C $gr add unrelated.txt half-done-page.html 2>$null
# our publish touches its own files
Set-Content "$gr\apps\nv1quake.html" '<p>page</p>'
Set-Content "$gr\versions.json" '{ "a": 1, "nv1quake": 2 }'
Set-Content "$gr\assets\nv1quake-poster.png" 'png'
$did = Publish-SiteFiles -SiteRepo $gr -Paths 'apps/nv1quake.html', 'versions.json', 'assets/nv1quake-poster.png' -Message 'Add nv1Quake'
Assert-Equal $did $true 'Publish-SiteFiles reports it committed'
$inCommit = @(git -C $gr show --name-only --format= HEAD) | Sort-Object
Assert-Equal ($inCommit -join ',') 'apps/nv1quake.html,assets/nv1quake-poster.png,versions.json' 'the commit contains exactly our three files'
$stillStaged = @(git -C $gr diff --cached --name-only) | Sort-Object
Assert-Equal ($stillStaged -join ',') 'half-done-page.html,unrelated.txt' "the user's unrelated staged work is left staged, not committed"
$did2 = Publish-SiteFiles -SiteRepo $gr -Paths 'apps/nv1quake.html', 'versions.json', 'assets/nv1quake-poster.png' -Message 'Add nv1Quake'
Assert-Equal $did2 $false 'republishing identical files is a no-op, not an error'
Remove-Item -LiteralPath $gr -Recurse -Force

# --- BUILD-INFO.json + Test-PublishReady: published installers must correspond to the published source
$bo = Join-Path $env:TEMP 'nv1-buildinfo'
if (Test-Path -LiteralPath $bo) { Remove-Item -LiteralPath $bo -Recurse -Force }
New-Item -ItemType Directory $bo | Out-Null
$H = 'a' * 40; $OLD = 'b' * 40
Assert-Equal (@(Test-PublishReady -OutputDir $bo -HeadCommit $H -TreeDirty $false -CommitOnOrigin $true).Count -gt 0) $true 'no BUILD-INFO.json: not ready to publish'
foreach ($v in 'emulated', 'win95', 'dos') { Update-BuildInfo -OutputDir $bo -Variant $v -Commit $H -Dirty $false -Rebuilt $true }
Set-BuildInfoZip -OutputDir $bo -Commit $H
Assert-Equal @(Test-PublishReady -OutputDir $bo -HeadCommit $H -TreeDirty $false -CommitOnOrigin $true).Count 0 'everything consistent: ready'
Assert-Equal (@(Test-PublishReady -OutputDir $bo -HeadCommit $H -TreeDirty $true -CommitOnOrigin $true) -join ' ') 'The working tree has uncommitted changes to tracked files.' 'dirty tree now: not ready'
Assert-Equal ((@(Test-PublishReady -OutputDir $bo -HeadCommit $H -TreeDirty $false -CommitOnOrigin $false) -join ' ') -match 'not on origin') $true 'commit not pushed to origin: not ready'
Assert-Equal ((@(Test-PublishReady -OutputDir $bo -HeadCommit $OLD -TreeDirty $false -CommitOnOrigin $true) -join ' ') -match 'HEAD') $true 'HEAD moved since the build: not ready'
Update-BuildInfo -OutputDir $bo -Variant dos -Commit $OLD -Dirty $false -Rebuilt $true
Assert-Equal ((@(Test-PublishReady -OutputDir $bo -HeadCommit $H -TreeDirty $false -CommitOnOrigin $true) -join ' ') -match 'dos') $true 'one variant built from another commit than the zip: not ready'
Update-BuildInfo -OutputDir $bo -Variant dos -Commit $H -Dirty $false -Rebuilt $false
Assert-Equal ((@(Test-PublishReady -OutputDir $bo -HeadCommit $H -TreeDirty $false -CommitOnOrigin $true) -join ' ') -match 'rebuild') $true 'variant not freshly rebuilt: not ready'
Update-BuildInfo -OutputDir $bo -Variant dos -Commit $H -Dirty $true -Rebuilt $true
Assert-Equal ((@(Test-PublishReady -OutputDir $bo -HeadCommit $H -TreeDirty $false -CommitOnOrigin $true) -join ' ') -match 'uncommitted') $true 'variant built from a dirty tree: not ready'
Update-BuildInfo -OutputDir $bo -Variant dos -Commit $H -Dirty $false -Rebuilt $true
Set-BuildInfoZip -OutputDir $bo -Commit $OLD
Assert-Equal ((@(Test-PublishReady -OutputDir $bo -HeadCommit $H -TreeDirty $false -CommitOnOrigin $true) -join ' ') -match 'source zip') $true 'source zip from another commit: not ready'
Remove-Item -LiteralPath $bo -Recurse -Force

if ($script:Failures -gt 0) { Write-Host "$script:Failures failure(s)" -ForegroundColor Red; exit 1 }
Write-Host 'All tests passed' -ForegroundColor Green
