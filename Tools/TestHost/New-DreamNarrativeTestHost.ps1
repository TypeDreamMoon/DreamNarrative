#Requires -Version 7.2
<#
.SYNOPSIS
    Creates or checks the DreamNarrative test host: a minimal project whose Plugins\DreamNarrative is
    a git worktree of this repository.

.DESCRIPTION
    The host lets the plugin build and run its tests while the working project's editor stays open,
    and proves the plugin declares every dependency it uses. See README.md beside this script.

      1. Checks before touching anything: git is on PATH, -Root is not on drive C and not inside the
         repository, the repository is a git repository, the engine the template names exists.
      2. Plugins\DreamNarrative: if it exists it must be a worktree of the repository (same git common
         directory, registered in "git worktree list") with -Branch checked out. If it does not exist
         it is created with "git worktree add" -- on -Branch when that branch exists, otherwise as a
         new branch -Branch started at -StartPoint. An existing worktree is never removed, reset or
         switched.
      3. The template: missing files are written, matching files are left alone (line endings and a
         byte-order mark are not differences), a file that differs is listed and kept unless -Force,
         which keeps the old one as <name>.bak-<timestamp>.
      4. .dreamnarrative-testhost.json records what the host was made from.

    Safe to run again at any time. Nothing outside -Root is written, apart from what "git worktree
    add" records in the repository's .git\worktrees directory.

.PARAMETER Root
    The host directory. Refused on drive C.
.PARAMETER RepoPath
    Any working tree of the DreamNarrative repository. Default: the one this script lives in.
.PARAMETER Branch
    The branch the worktree must have checked out (and is created with).
.PARAMETER StartPoint
    Where a new -Branch starts when it does not exist yet.
.PARAMETER EngineRoot
    The engine. Default: looked up from the template's engine association.
.PARAMETER Force
    Replace template files that differ, keeping each old one as <name>.bak-<timestamp>.
.PARAMETER KeepCurrentHead
    Accept an existing worktree on another branch or a detached commit.

.NOTES
    Exit codes: 0 the host matches the template; 1 a check failed and nothing after it was done;
    2 the host is usable but differs from the template (files kept without -Force, or -WhatIf found
    work to do).
#>
[CmdletBinding(SupportsShouldProcess)]
param(
    [string]$Root = 'I:\UnrealProject_Moon\DEV_58\DreamNarrativeTestHost',
    [string]$RepoPath,
    [string]$Branch = 'dev',
    [string]$StartPoint = 'main',
    [string]$EngineRoot,
    [switch]$Force,
    [switch]$KeepCurrentHead
)

Set-StrictMode -Version 3.0
$ErrorActionPreference = 'Stop'

$TemplateVersion = 2
$PluginName = 'DreamNarrative'
$HostName = 'DreamNarrativeTestHost'
$TemplateDir = Join-Path $PSScriptRoot 'Template'

function Write-Step([string]$Text) { Write-Host "==> $Text" -ForegroundColor Cyan }
function Write-Info([string]$Text) { Write-Host "    $Text" }
function Stop-WithFailure([string[]]$Lines) {
    foreach ($line in $Lines) { Write-Host "!!  $line" -ForegroundColor Yellow }
    exit 1
}

function Resolve-UserPath([string]$Path) {
    return [IO.Path]::GetFullPath($ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Path)).TrimEnd('\')
}

function ConvertTo-NormalPath([string]$Path) {
    return [IO.Path]::GetFullPath($Path.Replace('/', '\')).TrimEnd('\')
}

function Test-SamePath([string]$A, [string]$B) {
    return [string]::Equals((ConvertTo-NormalPath $A), (ConvertTo-NormalPath $B), [StringComparison]::OrdinalIgnoreCase)
}

function Test-PathUnder([string]$Path, [string]$Directory) {
    $p = (ConvertTo-NormalPath $Path) + '\'
    $d = (ConvertTo-NormalPath $Directory) + '\'
    return $p.StartsWith($d, [StringComparison]::OrdinalIgnoreCase)
}

function Resolve-PhysicalPath([string]$Path) {
    # Follows junctions and symbolic links on the longest existing prefix, so a link from another
    # drive into C: is seen for what it is.
    $current = ConvertTo-NormalPath $Path
    $rest = @()
    while ($current -and -not (Test-Path -LiteralPath $current)) {
        $rest = @(Split-Path -Leaf $current) + $rest
        $current = Split-Path -Parent $current
    }
    if (-not $current) { return ConvertTo-NormalPath $Path }
    $item = Get-Item -LiteralPath $current -Force
    $resolved = $item.FullName
    try {
        $target = $item.ResolveLinkTarget($true)
        if ($target) { $resolved = $target.FullName }
    } catch { }
    foreach ($part in $rest) { $resolved = Join-Path $resolved $part }
    return ConvertTo-NormalPath $resolved
}

function Invoke-Git([string]$Directory, [string[]]$Arguments, [switch]$AllowFailure) {
    $saved = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $output = & git -C $Directory @Arguments 2>&1 | ForEach-Object { "$_" }
        $code = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $saved
    }
    if ($code -ne 0 -and -not $AllowFailure) {
        throw "git $($Arguments -join ' ') (in $Directory) failed with exit code $code.`n$($output -join "`n")"
    }
    return [pscustomobject]@{ Code = $code; Output = @($output | Where-Object { $_ -notmatch '^warning: ' }) }
}

function Get-WorktreeEntries([string]$Repository) {
    $entries = @()
    $entry = $null
    foreach ($line in (Invoke-Git $Repository @('worktree', 'list', '--porcelain')).Output) {
        if ($line.StartsWith('worktree ')) {
            if ($entry) { $entries += $entry }
            $entry = [pscustomobject]@{ Path = (ConvertTo-NormalPath $line.Substring(9)); Branch = ''; Detached = $false; Prunable = $false }
        } elseif ($entry -and $line.StartsWith('branch ')) {
            $entry.Branch = $line.Substring(7) -replace '^refs/heads/', ''
        } elseif ($entry -and $line -eq 'detached') {
            $entry.Detached = $true
        } elseif ($entry -and $line.StartsWith('prunable')) {
            $entry.Prunable = $true
        }
    }
    if ($entry) { $entries += $entry }
    return $entries
}

function ConvertTo-ComparableText([byte[]]$Bytes) {
    # Line endings and a byte-order mark are not differences: git checks the template out with the
    # repository's line endings, and editors differ on the BOM.
    $text = [Text.Encoding]::UTF8.GetString($Bytes)
    if ($text.Length -gt 0 -and $text[0] -eq [char]0xFEFF) { $text = $text.Substring(1) }
    return $text.Replace("`r`n", "`n").Replace("`r", "`n")
}

function Get-FirstDifference([string]$Existing, [string]$Wanted) {
    $a = $Existing.Split("`n")
    $b = $Wanted.Split("`n")
    for ($i = 0; $i -lt [Math]::Max($a.Count, $b.Count); $i++) {
        $x = if ($i -lt $a.Count) { $a[$i] } else { '<end of file>' }
        $y = if ($i -lt $b.Count) { $b[$i] } else { '<end of file>' }
        if ($x -ne $y) { return "line $($i + 1): has '$x', template has '$y'" }
    }
    return 'no differing line'
}

# ------------------------------------------------------------------------------------------ checks

if (-not $RepoPath) { $RepoPath = Join-Path $PSScriptRoot '..\..' }
$RepoPath = Resolve-UserPath $RepoPath
$Root = Resolve-UserPath $Root
$WorktreePath = Join-Path (Join-Path $Root 'Plugins') $PluginName

Write-Step "DreamNarrative test host"
Write-Info "Host:       $Root"
Write-Info "Repository: $RepoPath"
Write-Info "Branch:     $Branch$(if ($KeepCurrentHead) { ' (or whatever an existing worktree has checked out)' })"

$failures = @()
if (-not (Get-Command git -CommandType Application -ErrorAction SilentlyContinue)) { $failures += 'git is not on PATH.' }
if (-not (Test-Path -LiteralPath $TemplateDir -PathType Container)) { $failures += "The template is missing: $TemplateDir" }
$physicalRoot = Resolve-PhysicalPath $Root
if ($physicalRoot.Substring(0, 2).ToUpperInvariant() -eq 'C:') {
    $failures += "The host would live on drive C ($physicalRoot), which is nearly full. Pass -Root on another drive."
}
if ($failures.Count -gt 0) { Stop-WithFailure $failures }

$common = Invoke-Git $RepoPath @('rev-parse', '--path-format=absolute', '--git-common-dir') -AllowFailure
if ($common.Code -ne 0) { Stop-WithFailure @("$RepoPath is not a git working tree: $($common.Output -join ' ')") }
$RepoCommonDir = ConvertTo-NormalPath ([string]$common.Output[0])
$RepoTopLevel = ConvertTo-NormalPath ([string](Invoke-Git $RepoPath @('rev-parse', '--show-toplevel')).Output[0])
if (Test-PathUnder $Root $RepoTopLevel) { Stop-WithFailure @("The host ($Root) may not live inside the working tree $RepoTopLevel.") }
Write-Info "Git dir:    $RepoCommonDir"

$uprojectTemplate = Get-Content -LiteralPath (Join-Path $TemplateDir "$HostName.uproject") -Raw | ConvertFrom-Json
$association = $uprojectTemplate.EngineAssociation
if (-not $EngineRoot) {
    $key = 'HKCU:\SOFTWARE\Epic Games\Unreal Engine\Builds'
    if (Test-Path $key) { $EngineRoot = (Get-ItemProperty -Path $key).$association }
}
if (-not $EngineRoot -or -not (Test-Path -LiteralPath (Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'))) {
    Stop-WithFailure @("The engine $association is not registered on this machine, or has no UnrealEditor-Cmd.exe. Pass -EngineRoot.")
}
$EngineRoot = Resolve-UserPath $EngineRoot
Write-Info "Engine:     $EngineRoot"

$existingProjects = @(Get-ChildItem -LiteralPath $Root -Filter '*.uproject' -File -ErrorAction SilentlyContinue | Where-Object { $_.Name -ne "$HostName.uproject" })
if ($existingProjects.Count -gt 0) { Stop-WithFailure @("$Root already holds another project: $($existingProjects[0].Name).") }

$pendingWork = $false
$keptDifferent = $false

# ------------------------------------------------------------------------------------------ worktree

Write-Step "Plugins\$PluginName as a worktree of the repository"
$entries = @(Get-WorktreeEntries $RepoPath)
# git lists the main working tree first.
$MainTree = $entries[0].Path
if (Test-Path -LiteralPath $WorktreePath) {
    $top = Invoke-Git $WorktreePath @('rev-parse', '--show-toplevel') -AllowFailure
    if ($top.Code -ne 0) { Stop-WithFailure @("$WorktreePath exists but is not a git working tree. Move it away to let this script create the worktree.") }
    $topPath = ConvertTo-NormalPath ([string]$top.Output[0])
    if (-not (Test-SamePath $topPath $WorktreePath)) { Stop-WithFailure @("$WorktreePath is a folder inside the working tree $topPath, not a worktree of its own.") }
    $theirCommon = ConvertTo-NormalPath ([string](Invoke-Git $WorktreePath @('rev-parse', '--path-format=absolute', '--git-common-dir')).Output[0])
    if (-not (Test-SamePath $theirCommon $RepoCommonDir)) { Stop-WithFailure @("$WorktreePath belongs to another repository (git dir $theirCommon).") }
    # The host must have a worktree of its own, not the main one -- directly or through a link.
    if (Test-SamePath (Resolve-PhysicalPath $WorktreePath) $MainTree) { Stop-WithFailure @("$WorktreePath is the repository's main working tree ($MainTree), not a worktree of its own.") }
    $mine = @($entries | Where-Object { Test-SamePath $_.Path $WorktreePath })
    if ($mine.Count -eq 0) { Stop-WithFailure @("$WorktreePath is not in the repository's worktree list.") }
    $head = if ($mine[0].Detached) { 'a detached commit' } else { "branch $($mine[0].Branch)" }
    if (-not $KeepCurrentHead -and ($mine[0].Detached -or -not [string]::Equals($mine[0].Branch, $Branch, [StringComparison]::Ordinal))) {
        Stop-WithFailure @("The worktree has $head checked out, not branch $Branch.",
            'This script never switches a worktree. Switch it yourself, pass -Branch with the one it has, or -KeepCurrentHead.')
    }
    Write-Info "Existing worktree on $head."
    $dirty = @((Invoke-Git $WorktreePath @('status', '--porcelain')).Output | Where-Object { $_.Trim() })
    if ($dirty.Count -gt 0) { Write-Info "It has $($dirty.Count) uncommitted change(s); the host builds it exactly as it is." }
} else {
    $stale = @($entries | Where-Object { Test-SamePath $_.Path $WorktreePath })
    if ($stale.Count -gt 0) { Stop-WithFailure @("$WorktreePath is gone but still registered as a worktree. Run git -C `"$RepoPath`" worktree prune, then this again.") }
    $holder = @($entries | Where-Object { -not $_.Detached -and [string]::Equals($_.Branch, $Branch, [StringComparison]::Ordinal) })
    if ($holder.Count -gt 0) { Stop-WithFailure @("Branch $Branch is already checked out in $($holder[0].Path); a branch can be checked out in one worktree at a time.") }
    $branchExists = (Invoke-Git $RepoPath @('rev-parse', '--verify', '--quiet', "refs/heads/$Branch") -AllowFailure).Code -eq 0
    $arguments = if ($branchExists) { @('worktree', 'add', $WorktreePath, $Branch) } else { @('worktree', 'add', '-b', $Branch, $WorktreePath, $StartPoint) }
    $what = if ($branchExists) { "git worktree add (existing branch $Branch)" } else { "git worktree add (new branch $Branch at $StartPoint)" }
    if ($PSCmdlet.ShouldProcess($WorktreePath, $what)) {
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $WorktreePath) | Out-Null
        $added = Invoke-Git $RepoPath $arguments -AllowFailure
        if ($added.Code -ne 0) { Stop-WithFailure (@('git worktree add failed:') + $added.Output) }
        $short = [string](Invoke-Git $WorktreePath @('rev-parse', '--short', 'HEAD')).Output[0]
        Write-Info "Created the worktree on branch $Branch at $short."
    } else {
        $pendingWork = $true
    }
}

# ------------------------------------------------------------------------------------------ template

Write-Step 'Template files'
$templateFiles = @(Get-ChildItem -LiteralPath $TemplateDir -File -Recurse)
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
foreach ($file in $templateFiles) {
    $relative = [IO.Path]::GetRelativePath($TemplateDir, $file.FullName)
    $target = Join-Path $Root $relative
    $wanted = [IO.File]::ReadAllBytes($file.FullName)
    if (-not (Test-Path -LiteralPath $target -PathType Leaf)) {
        if ($PSCmdlet.ShouldProcess($target, 'write from the template')) {
            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
            [IO.File]::WriteAllBytes($target, $wanted)
            Write-Info "wrote     $relative"
        } else {
            $pendingWork = $true
        }
        continue
    }
    $existingText = ConvertTo-ComparableText ([IO.File]::ReadAllBytes($target))
    $wantedText = ConvertTo-ComparableText $wanted
    if ($existingText -eq $wantedText) {
        Write-Info "same      $relative"
        continue
    }
    $difference = Get-FirstDifference $existingText $wantedText
    if ($Force -and $PSCmdlet.ShouldProcess($target, 'replace with the template (keeping a backup)')) {
        Copy-Item -LiteralPath $target -Destination "$target.bak-$stamp"
        [IO.File]::WriteAllBytes($target, $wanted)
        Write-Info "replaced  $relative  (old copy: $(Split-Path -Leaf $target).bak-$stamp)"
    } else {
        Write-Info "DIFFERENT $relative -- $difference"
        $keptDifferent = $true
    }
}

# ------------------------------------------------------------------------------------------ record

$recordPath = Join-Path $Root '.dreamnarrative-testhost.json'
$record = [ordered]@{
    schema            = 1
    templateVersion   = $TemplateVersion
    repoPath          = $MainTree
    gitCommonDir      = $RepoCommonDir
    worktreePath      = $WorktreePath
    branch            = $Branch
    engineAssociation = $association
    engineRoot        = $EngineRoot
}
$recordText = (($record | ConvertTo-Json) -replace "`r`n", "`n") + "`n"
$currentRecord = if (Test-Path -LiteralPath $recordPath) { [IO.File]::ReadAllText($recordPath) } else { '' }
if ($currentRecord.Replace("`r`n", "`n") -ne $recordText) {
    if ($PSCmdlet.ShouldProcess($recordPath, 'write the host record')) {
        [IO.File]::WriteAllText($recordPath, $recordText)
        Write-Info 'wrote     .dreamnarrative-testhost.json'
    } else {
        $pendingWork = $true
    }
}

# ------------------------------------------------------------------------------------------ done

$runner = Join-Path $WorktreePath 'Tools\dnc.ps1'
Write-Step 'Next'
Write-Info "Build and run the quick tests:"
Write-Info "  pwsh -NoProfile -File `"$runner`" test -Project `"$(Join-Path $Root "$HostName.uproject")`""
if ($keptDifferent) {
    Write-Info 'Some files differ from the template and were kept; -Force replaces them (with backups).'
    exit 2
}
if ($pendingWork) { exit 2 }
exit 0
