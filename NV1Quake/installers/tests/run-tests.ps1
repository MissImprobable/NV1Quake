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

if ($script:Failures -gt 0) { Write-Host "$script:Failures failure(s)" -ForegroundColor Red; exit 1 }
Write-Host 'All tests passed' -ForegroundColor Green
