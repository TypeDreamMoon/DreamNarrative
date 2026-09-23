<#
.SYNOPSIS
    Headless driver for DreamNarrative: build, lint, dump and test without opening the editor.

.DESCRIPTION
    Wraps `UnrealEditor-Cmd.exe <project> -run=DreamNarrative ...` and the automation runner, and
    reads the verdict back out of the log so an agent or a CI job gets a clean exit code instead of
    several hundred lines of engine boot.

    Commands
      build   compile every .dna under the project's source directory into script assets
      lint    parse and validate only; nothing is written
      dump    print the syntax tree of one file (or all)
      test    run the DreamNarrative automation tests (corpus, lexer, expressions, runtime)

    The engine is resolved from the .uproject's EngineAssociation; the project is found by walking
    up from -Project, the target file, or the working directory.

    Any command that writes assets wants the editor CLOSED: two processes saving the same package
    means the second one silently wins.

.EXAMPLE
    ./dnc.ps1 build

.EXAMPLE
    ./dnc.ps1 lint -File DNarrative/ring.dna

.EXAMPLE
    ./dnc.ps1 test -Filter DreamNarrative.Corpus
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory, Position = 0)]
    [ValidateSet('build', 'lint', 'dump', 'test')]
    [string]$Command,

    # One source file (absolute, or relative to the project directory). Omitted = every file.
    [string]$File,

    # Source directory override (default: Project Settings > Dream Narrative > Source Directory).
    [string]$Root,

    # build: generate in memory without writing packages.
    [switch]$NoSave,

    # test: automation test filter (default: DreamNarrative).
    [string]$Filter = 'DreamNarrative',

    # Path to the .uproject. Omitted = discovered.
    [string]$Project,

    # Print the raw engine output instead of the filtered view.
    [switch]$Raw
)

$ErrorActionPreference = 'Stop'

# ---------------------------------------------------------------- project and engine

function Find-Project([string]$Start) {
    $dir = if ($Start) { (Resolve-Path $Start).Path } else { (Get-Location).Path }
    if (Test-Path $dir -PathType Leaf) { $dir = Split-Path -Parent $dir }
    while ($dir) {
        $found = Get-ChildItem -LiteralPath $dir -Filter '*.uproject' -File -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($found) { return $found.FullName }
        $parent = Split-Path -Parent $dir
        if ($parent -eq $dir) { break }
        $dir = $parent
    }
    throw "No .uproject found above '$dir'. Pass -Project."
}

$uproject = if ($Project) { (Resolve-Path $Project).Path } elseif ($File) { Find-Project $File } else { Find-Project $null }
$projectDir = Split-Path -Parent $uproject

$association = (Get-Content -LiteralPath $uproject -Raw | ConvertFrom-Json).EngineAssociation
if (-not $association) { throw "The .uproject has no EngineAssociation." }
$engineRoot = $null
$key = 'HKCU:\SOFTWARE\Epic Games\Unreal Engine\Builds'
if (Test-Path $key) {
    $value = (Get-ItemProperty -Path $key).$association
    if ($value) { $engineRoot = $value }
}
if (-not $engineRoot) {
    $launcherKey = "HKLM:\SOFTWARE\EpicGames\Unreal Engine\$association"
    if (Test-Path $launcherKey) { $engineRoot = (Get-ItemProperty -Path $launcherKey).InstalledDirectory }
}
if (-not $engineRoot) { throw "EngineAssociation '$association' is not registered on this machine." }
$editorCmd = Join-Path $engineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
if (-not (Test-Path $editorCmd)) { throw "UnrealEditor-Cmd.exe not found at '$editorCmd'." }

if (Get-Process -Name 'UnrealEditor' -ErrorAction SilentlyContinue) {
    Write-Warning "An UnrealEditor process is running. If it has this project open, a build that writes assets will fight it over the packages."
}

# ---------------------------------------------------------------- run

function Invoke-Editor([string[]]$Arguments, [string[]]$KeepPatterns) {
    $all = @($uproject) + $Arguments + @('-stdout', '-FullStdOutLogOutput', '-unattended', '-nopause', '-nosplash', '-nullrhi', '-NoLiveCoding')
    Write-Verbose ("UnrealEditor-Cmd " + ($all -join ' '))
    $output = & $editorCmd @all 2>&1
    $code = $LASTEXITCODE
    if ($Raw) {
        $output | ForEach-Object { $_.ToString() }
    }
    else {
        $seen = @{}
        foreach ($line in $output) {
            $text = $line.ToString()
            $keep = $false
            foreach ($pattern in $KeepPatterns) { if ($text -match $pattern) { $keep = $true; break } }
            if (-not $keep) { continue }
            # UnrealEditor-Cmd echoes narrative log lines twice (once with a LogInit: Display: prefix).
            $clean = $text -replace '^\[[^\]]*\]\[\s*\d+\]', '' -replace '^LogInit: Display: ', ''
            $clean = $clean.Trim()
            if ($seen.ContainsKey($clean)) { continue }
            $seen[$clean] = $true
            Write-Output $clean
        }
    }
    return $code
}

switch ($Command) {
    'build' {
        $args = @('-run=DreamNarrative')
        if ($File) { $args += "-File=$File" }
        if ($Root) { $args += "-Root=$Root" }
        if ($NoSave) { $args += '-NoSave' }
        $code = Invoke-Editor $args @('LogDreamNarrative', 'DNS\d{4}', 'Fatal', 'Assertion')
        exit $code
    }
    'lint' {
        $args = @('-run=DreamNarrative', '-Lint')
        if ($File) { $args += "-File=$File" }
        if ($Root) { $args += "-Root=$Root" }
        $code = Invoke-Editor $args @('LogDreamNarrative', 'DNS\d{4}', 'Fatal', 'Assertion')
        exit $code
    }
    'dump' {
        $args = @('-run=DreamNarrative', '-Lint', '-Dump')
        if ($File) { $args += "-File=$File" }
        if ($Root) { $args += "-Root=$Root" }
        $code = Invoke-Editor $args @('LogDreamNarrative', '^\d+: ', 'DNS\d{4}')
        exit $code
    }
    'test' {
        # No `Quit` in ExecCmds: it would fire the moment the tests are queued. -TestExit is what makes
        # the process leave once the queue drains. The controller writes one Result={Success|Fail} line
        # per test; those counts are the verdict, the process exit code only says whether it survived.
        $report = Join-Path $projectDir 'Saved/DreamNarrative/TestReport'
        $args = @("-ExecCmds=Automation RunTests $Filter", '-TestExit=Automation Test Queue Empty', "-ReportExportPath=$report", '-log=DnaTest.log')
        $all = @($uproject) + $args + @('-stdout', '-FullStdOutLogOutput', '-unattended', '-nopause', '-nosplash', '-nullrhi', '-NoLiveCoding')
        $output = & $editorCmd @all 2>&1
        $editorExit = $LASTEXITCODE
        $lines = @($output | ForEach-Object { "$_" })
        if ($Raw) { $lines | Write-Output }

        $passed = @($lines | Where-Object { $_.Contains('Result={Success}') }).Count
        $failed = @($lines | Where-Object { $_.Contains('Result={Fail}') }).Count
        foreach ($line in $lines) {
            if ($line -match 'Result=\{Fail\}|LogAutomationController: Error|Fatal|Assertion failed') {
                Write-Output (($line -replace '^\[[^\]]*\]\[\s*\d+\]', '').Trim())
            }
        }
        Write-Output ("{0} passed, {1} failed (editor exit {2})" -f $passed, $failed, $editorExit)
        if ($editorExit -ne 0 -and $failed -eq 0 -and $passed -eq 0) {
            Write-Warning "The editor did not run the tests (exit $editorExit). Use -Raw to see its output; a negative exit code usually means it could not start."
            exit 1
        }
        exit $failed
    }
}
