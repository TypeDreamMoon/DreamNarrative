#Requires -Version 7.2
<#
.SYNOPSIS
    Source rules no compiler checks, for the DreamNarrative repository.

.DESCRIPTION
    time        The runtime modules (Core, Story, Stage, GAS) never read time or set timers themselves:
                every wait goes through the ledger's clock (IDnaClock), which tests replace with a
                manual one. DreamNarrativeClock.h/.cpp is the one place allowed to touch world time.
    stage-name  Plan labels (M1, MT, T0...T5, "batch 2", 第二批) stay in the plan. Code, comments,
                documents, test names and corpus files describe what things are, not when they were
                built.
    eol         Files are stored with LF in git, and no file mixes CRLF and LF in the working tree.

    Exit code: 0 clean, 1 findings (each printed as path:line: rule: text).

.PARAMETER Root
    The repository to check. Default: the one this script lives in.
#>
[CmdletBinding()]
param(
    [string]$Root
)

Set-StrictMode -Version 3.0
$ErrorActionPreference = 'Stop'

if (-not $Root) { $Root = Join-Path $PSScriptRoot '..' }
$Root = [IO.Path]::GetFullPath($Root).TrimEnd('\')

$findings = [System.Collections.Generic.List[string]]::new()
function Add-Finding([string]$Path, [int]$Line, [string]$Rule, [string]$Text) {
    $findings.Add(('{0}:{1}: {2}: {3}' -f $Path, $Line, $Rule, $Text.Trim()))
}

function Invoke-GitLines([string[]]$Arguments) {
    $saved = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $lines = & git -C $Root @Arguments 2>$null
        if ($LASTEXITCODE -ne 0) { throw "git $($Arguments -join ' ') failed in $Root" }
        return @($lines)
    } finally {
        $ErrorActionPreference = $saved
    }
}

# Tracked files plus new ones git does not ignore -- what the next commit could contain.
$files = @(Invoke-GitLines @('-c', 'core.quotepath=false', 'ls-files', '--cached', '--others', '--exclude-standard') |
        Where-Object { $_ } | Sort-Object -Unique |
        Where-Object { Test-Path -LiteralPath (Join-Path $Root $_) -PathType Leaf })

$textExtensions = @('.h', '.cpp', '.cs', '.ps1', '.md', '.dna', '.tree', '.ini', '.json', '.uplugin', '.txt', '.gitignore', '.gitattributes')

function Get-CodeLines([string]$Text) {
    # Line-for-line copy of a C++ source with comments and string literals blanked, so a rule about
    # code does not fire on prose.
    $out = [System.Text.StringBuilder]::new($Text.Length)
    $i = 0
    $n = $Text.Length
    $inBlock = $false
    while ($i -lt $n) {
        $c = $Text[$i]
        $next = if ($i + 1 -lt $n) { $Text[$i + 1] } else { [char]0 }
        if ($inBlock) {
            if ($c -eq '*' -and $next -eq '/') { $inBlock = $false; $i += 2; $null = $out.Append('  '); continue }
            $null = $out.Append($(if ($c -eq "`n") { "`n" } else { ' ' }))
            $i++
            continue
        }
        if ($c -eq '/' -and $next -eq '*') { $inBlock = $true; $i += 2; $null = $out.Append('  '); continue }
        if ($c -eq '/' -and $next -eq '/') {
            while ($i -lt $n -and $Text[$i] -ne "`n") { $null = $out.Append(' '); $i++ }
            continue
        }
        if ($c -eq '"') {
            $null = $out.Append('"'); $i++
            while ($i -lt $n -and $Text[$i] -ne '"' -and $Text[$i] -ne "`n") {
                if ($Text[$i] -eq '\' -and $i + 1 -lt $n) { $null = $out.Append('  '); $i += 2; continue }
                $null = $out.Append(' '); $i++
            }
            if ($i -lt $n -and $Text[$i] -eq '"') { $null = $out.Append('"'); $i++ }
            continue
        }
        $null = $out.Append($c)
        $i++
    }
    return $out.ToString().Split("`n")
}

# ------------------------------------------------------------------------------------------------ time

$runtimeModules = '^Source/DreamNarrative(Core|Story|Stage|GAS)/'
$clockFiles = @('Source/DreamNarrativeCore/Public/DreamNarrativeClock.h', 'Source/DreamNarrativeCore/Private/DreamNarrativeClock.cpp')
$timePattern = '\bFPlatformTime\s*::|\bGet(Real|Unpaused|Audio)?TimeSeconds\s*\(|\bGetDeltaSeconds\s*\(|\bGetTimerManager\s*\(|\bFTimerManager\b|\bFDateTime\s*::\s*(Now|UtcNow)\b|\bFApp\s*::\s*Get(CurrentTime|DeltaTime|GameTime)\b'
foreach ($file in $files) {
    if ($file -notmatch $runtimeModules -or $file -notmatch '\.(h|cpp)$' -or $clockFiles -contains $file) { continue }
    $lines = Get-CodeLines ([IO.File]::ReadAllText((Join-Path $Root $file)))
    for ($l = 0; $l -lt $lines.Count; $l++) {
        if ($lines[$l] -match $timePattern) {
            Add-Finding $file ($l + 1) 'time' "reads time or sets a timer directly ('$($Matches[0])'); wait through the ledger's clock (IDnaClock) instead"
        }
    }
}

# ------------------------------------------------------------------------------------------ stage-name

$stagePattern = '(?<![\w.])(M[0-9]|MT|T[0-5])(?![\w])|第[一二三四五六七八九十0-9]+批|\b[Bb]atch [0-9]+\b'
foreach ($file in $files) {
    $extension = [IO.Path]::GetExtension($file).ToLowerInvariant()
    if ($file -eq '.gitignore' -or $file -eq '.gitattributes') { $extension = $file }
    if ($textExtensions -notcontains $extension) { continue }
    if ($file -eq 'Tools/StaticChecks.ps1') { continue }   # it has to spell the labels out
    $lines = [IO.File]::ReadAllLines((Join-Path $Root $file))
    for ($l = 0; $l -lt $lines.Count; $l++) {
        $m = [regex]::Match($lines[$l], $stagePattern)
        if ($m.Success) {
            Add-Finding $file ($l + 1) 'stage-name' "plan label '$($m.Value)' outside the plan"
        }
    }
}

# ------------------------------------------------------------------------------------------------- eol

foreach ($line in (Invoke-GitLines @('-c', 'core.quotepath=false', 'ls-files', '--eol', '--cached', '--others', '--exclude-standard'))) {
    # i/<index> w/<worktree> attr/<attributes> <tab> path
    if ($line -notmatch '^i/(\S*)\s+w/(\S*)\s+attr/\S*\s+(.+)$') { continue }
    $index = $Matches[1]; $work = $Matches[2]; $path = $Matches[3].Trim()
    if ($index -eq 'crlf' -or $index -eq 'mixed') {
        Add-Finding $path 1 'eol' "stored in git with $index line endings; git stores text with LF"
    }
    if ($work -eq 'mixed') {
        Add-Finding $path 1 'eol' 'mixes CRLF and LF line endings'
    }
}

# ------------------------------------------------------------------------------------------------ done

if ($findings.Count -eq 0) {
    Write-Output "Static checks: clean ($($files.Count) files)."
    exit 0
}
$findings | ForEach-Object { Write-Output $_ }
Write-Output "Static checks: $($findings.Count) finding(s)."
exit 1
