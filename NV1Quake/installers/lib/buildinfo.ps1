# BUILD-INFO.json records, per variant, which commit each installer was built from, whether the tree
# was clean, and whether the game was freshly rebuilt; plus which commit the source zip came from.
# Publishing refuses to proceed unless they all agree with HEAD, so the "source archive of exactly
# this commit is published next to this installer" promise in NOTICE.TXT is true (the GPL requires
# the corresponding source to be available).

function Get-BuildInfoPath { param([string]$OutputDir) Join-Path $OutputDir 'BUILD-INFO.json' }

function Read-BuildInfo {
    param([string]$OutputDir)
    $p = Get-BuildInfoPath $OutputDir
    if (Test-Path -LiteralPath $p) { return (Get-Content -LiteralPath $p -Raw | ConvertFrom-Json -AsHashtable) }
    @{ variants = @{}; zipCommit = $null }
}

function Write-BuildInfo {
    param([string]$OutputDir, [hashtable]$Info)
    ($Info | ConvertTo-Json -Depth 6) | Set-Content -LiteralPath (Get-BuildInfoPath $OutputDir) -Encoding ASCII
}

function Update-BuildInfo {
    param([string]$OutputDir, [string]$Variant, [string]$Commit, [bool]$Dirty, [bool]$Rebuilt)
    $info = Read-BuildInfo $OutputDir
    $info.variants[$Variant] = @{ commit = $Commit; dirty = $Dirty; rebuilt = $Rebuilt }
    Write-BuildInfo $OutputDir $info
}

function Set-BuildInfoZip {
    param([string]$OutputDir, [string]$Commit)
    $info = Read-BuildInfo $OutputDir
    $info.zipCommit = $Commit
    Write-BuildInfo $OutputDir $info
}

function Test-CommitOnOrigin {
    param([string]$Commit)
    $remotes = git -C $RepoRoot branch -r --contains $Commit 2>$null
    [bool]$remotes
}

# Returns a list of problems; empty means the installers and the source zip correspond to HEAD.
function Test-PublishReady {
    param([string]$OutputDir, [string]$HeadCommit, [bool]$TreeDirty, [bool]$CommitOnOrigin)
    $problems = New-Object System.Collections.Generic.List[string]
    if (-not (Test-Path -LiteralPath (Get-BuildInfoPath $OutputDir))) {
        $problems.Add('BUILD-INFO.json is missing from the output folder; run build-installers.ps1 -Rebuild first.')
        return @($problems)
    }
    $info = Read-BuildInfo $OutputDir
    if ($TreeDirty) { $problems.Add('The working tree has uncommitted changes to tracked files.') }
    if (-not $CommitOnOrigin) { $problems.Add("Commit $HeadCommit is not on origin; push it first so the source offer in NOTICE.TXT is true.") }
    foreach ($v in 'emulated', 'win95', 'dos') {
        if (-not $info.variants.ContainsKey($v)) { $problems.Add("The '$v' installer has not been built."); continue }
        $e = $info.variants[$v]
        if ($e.commit -ne $HeadCommit) { $problems.Add("HEAD ($HeadCommit) is not the commit the '$v' installer was built from ($($e.commit)).") }
        if ($e.dirty)                  { $problems.Add("The '$v' installer was built from a tree with uncommitted changes.") }
        if (-not $e.rebuilt)           { $problems.Add("The '$v' game was not freshly rebuilt (use build-installers.ps1 -Rebuild).") }
    }
    if ($info.zipCommit -ne $HeadCommit) { $problems.Add("The source zip is from commit $($info.zipCommit), not HEAD ($HeadCommit).") }
    @($problems)
}
