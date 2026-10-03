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

if ($script:Failures -gt 0) { Write-Host "$script:Failures failure(s)" -ForegroundColor Red; exit 1 }
Write-Host 'All tests passed' -ForegroundColor Green
