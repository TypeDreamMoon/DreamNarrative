#Requires -Version 7.2
<#
.SYNOPSIS
    Headless driver for DreamNarrative: build, lint, dump, check and test without opening the editor.

.DESCRIPTION
    Commands
      build   compile every .dna under the project's source directory into script assets
      lint    parse and validate only; nothing is written
      dump    print the syntax tree of one file (or all)
      vo-script  write the recording script (-Out, default Saved/DreamNarrative/VoiceScript.csv): every line,
              its speaker, text, context and voice; with the Voice column filled in it reads back as a voice table
      vo-status  how many lines of each character have a voice, and which lack one
      loc-export  write every localizable text as CSV (-Out, default Saved/DreamNarrative/Translations.csv)
              with who says it and where; with -Culture the target's translations fill the Translation column
      loc-import  read a translated CSV (-File) into the localization target's archive for -Culture; gather
              the target first, compile it in the Localization Dashboard after
      freeze  write `#id` onto every line and choice that has none, keeping the id it has now
              (all files, or -File's); do it before recording or translating
      check   the static checks (Tools/StaticChecks.ps1) over this repository
      test    static checks, build, then one preset of the automation tests in a headless editor

    build / lint / dump run `UnrealEditor-Cmd.exe <project> -run=DreamNarrative` and keep the lines
    that matter. Any command that writes assets wants the editor CLOSED: two processes saving the same
    package means the second one silently wins.

    test works like this:
      1. The project is -Project, else the test host (Tools/TestHost) when it exists, else the project
         the working directory is in. It refuses to run while an editor has that project open
         (-AllowEditorOpen overrides): the build would fight it over the DLLs.
      2. Static checks, unless -SkipStaticChecks.
      3. Build <Project>Editor Win64 Development, the whole target, with -NoEngineChanges, unless
         -NoBuild. Then the plugin's module manifest must carry the engine's BuildId and every module
         with a DLL -- a plugin that fails to load otherwise looks like "0 tests, nothing failed".
      4. Each run of the preset in its own headless editor, killed after its time limit. The engine
         writes index.json (the verdict), the log and the console output into the report directory,
         <project>\Saved\DreamNarrativeTestReports\<time>-<preset>.
      5. The digest: passed / failed / not run, the first error of each failure, crashes and ensures
         from the log, the slowest tests, and summary.md beside the rest.

    Presets
      Quick    language, corpus, ledger, dialogue, quest, GAS bridge, stages, editor panels, spec, replay, the project's own and in-process smoke tests (the daily one)
      Explore  the explorer: every explore block, and the explorer's own fixtures
      Smoke    the standalone-game smoke tests, in an editor binary running -game
      All      the three above, one editor each

    Exit codes of test: 0 green; 1 red (a test failed, or an ensure fired); 2 not judged (static
    checks, build, manifest, crash, timeout, no report, fewer tests than the preset's floor).

.EXAMPLE
    pwsh -NoProfile -File Tools\dnc.ps1 test
.EXAMPLE
    pwsh -NoProfile -File Tools\dnc.ps1 test -Filter StartsWith:DreamNarrative.Corpus -NoBuild
.EXAMPLE
    pwsh -NoProfile -File Tools\dnc.ps1 lint -File DNarrative/ring.dna -Project I:\UnrealProject_Moon\DEV_58\DevTest\DevTest.uproject
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory, Position = 0)]
    [ValidateSet('build', 'lint', 'dump', 'freeze', 'vo-script', 'vo-status', 'loc-export', 'loc-import', 'check', 'test')]
    [string]$Command,

    # build / lint / dump / freeze: one source file (absolute, or relative to the project directory).
    # loc-import: the translated CSV.
    [string]$File,

    # build / lint / dump: source directory override (default: Project Settings > Dream Narrative).
    [string]$Root,

    # build: generate in memory without writing packages.
    [switch]$NoSave,

    # vo-script / loc-export: where the CSV goes (relative to the project directory).
    [string]$Out,

    # loc-export / loc-import: the culture of the translations (en, ja, ...).
    [string]$Culture,

    # loc-export / loc-import: the localization target, Content/Localization/<Target>.
    [string]$Target = 'Game',

    # loc-export / loc-import: the target's native culture, when its .locmeta does not say.
    [string]$NativeCulture,

    # test: which tests.
    [ValidateSet('Quick', 'Explore', 'Smoke', 'All')]
    [string]$Preset = 'Quick',

    # test: an engine RunTests filter used instead of the preset's. Terms joined with '+';
    # StartsWith:X matches a name prefix, anything else is a substring. No commas, semicolons or quotes.
    [string]$Filter,

    # test: skip the build (the module manifest is still checked).
    [switch]$NoBuild,

    # test: skip the static checks.
    [switch]$SkipStaticChecks,

    # test: time limit of one editor run, overriding the preset's.
    [int]$TimeoutMinutes = 0,

    # test: go ahead although an editor has the project open.
    [switch]$AllowEditorOpen,

    # test: report root (default <project>\Saved\DreamNarrativeTestReports).
    [string]$ReportDir,

    # The .uproject. Omitted: see above.
    [string]$Project,

    # build / lint / dump: print the raw engine output instead of the filtered view.
    [switch]$Raw
)

Set-StrictMode -Version 3.0
$ErrorActionPreference = 'Stop'

$PluginDir = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')).TrimEnd('\')
$HostProject = 'I:\UnrealProject_Moon\DEV_58\DreamNarrativeTestHost\DreamNarrativeTestHost.uproject'

# The engine keeps at most 16384 characters of its command line and drops the rest silently.
$MaxFilterLength = 12000

$Presets = [ordered]@{
    Quick   = @{
        Filter   = 'StartsWith:DreamNarrative.Regression+StartsWith:DreamNarrative.Lang+StartsWith:DreamNarrative.Corpus+StartsWith:DreamNarrative.Ledger+StartsWith:DreamNarrative.Dialogue+StartsWith:DreamNarrative.Quest+StartsWith:DreamNarrative.GAS+StartsWith:DreamNarrative.Stage+StartsWith:DreamNarrative.Editor+StartsWith:DreamNarrative.Spec+StartsWith:DreamNarrative.Replay+StartsWith:DreamNarrative.Project+StartsWith:DreamNarrative.Smoke.InProcess'
        MinTests = 260
        Timeout  = 10
        Game     = $false
    }
    Explore = @{
        Filter   = 'StartsWith:DreamNarrative.Explore'
        MinTests = 17
        Timeout  = 30
        Game     = $false
    }
    Smoke   = @{
        Filter   = 'StartsWith:DreamNarrative.Smoke.Game'
        MinTests = 1
        Timeout  = 10
        Game     = $true
        # Built into the project's /Game/Narrative before the game starts, one commandlet run each.
        Content  = @('Tests/Corpus/Validate/Samples/ring.dna')
    }
}

# ---------------------------------------------------------------- helpers

function Write-Step([string]$Text) { Write-Host "==> $Text" -ForegroundColor Cyan }
function Write-Note([string]$Text) { Write-Host "    $Text" }
function Write-Problem([string]$Text) { Write-Host "!!  $Text" -ForegroundColor Yellow }

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

function Resolve-EngineRoot([string]$UProject) {
    $association = (Get-Content -LiteralPath $UProject -Raw | ConvertFrom-Json).EngineAssociation
    if (-not $association) { throw "$UProject has no EngineAssociation." }
    # A source build, as UnrealVersionSelector registers it. Strict mode throws on a missing property.
    $key = 'HKCU:\SOFTWARE\Epic Games\Unreal Engine\Builds'
    if (Test-Path $key) {
        $builds = Get-ItemProperty -Path $key
        if ($builds.PSObject.Properties[$association]) { return $builds.$association }
    }
    # An installed build: the launcher's registry key, or else its install list.
    $launcherKey = "HKLM:\SOFTWARE\EpicGames\Unreal Engine\$association"
    if (Test-Path $launcherKey) { return (Get-ItemProperty -Path $launcherKey).InstalledDirectory }
    $installed = Join-Path $env:ProgramData 'Epic\UnrealEngineLauncher\LauncherInstalled.dat'
    if (Test-Path -LiteralPath $installed) {
        $entry = @((Get-Content -LiteralPath $installed -Raw | ConvertFrom-Json).InstallationList |
            Where-Object { $_.AppName -eq "UE_$association" }) | Select-Object -First 1
        if ($entry) { return $entry.InstallLocation }
    }
    throw "EngineAssociation '$association' is not registered on this machine."
}

function Invoke-Native([string]$Exe, [string[]]$Arguments) {
    # Output lines, stderr merged; the caller reads $LASTEXITCODE.
    $saved = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        & $Exe @Arguments 2>&1 | ForEach-Object { "$_" }
    } finally {
        $ErrorActionPreference = $saved
    }
}

function Read-JsonFile([string]$Path) {
    return ([IO.File]::ReadAllText($Path) | ConvertFrom-Json -AsHashtable -Depth 64)
}

function Get-EditorsOnProject([string]$UProject) {
    # Every UnrealEditor*.exe whose command line names this .uproject. An editor opened from the
    # project browser without the path on its command line is the one case this cannot see.
    $needle = $UProject.Replace('/', '\').ToLowerInvariant()
    $procs = @(Get-CimInstance -ClassName Win32_Process -Filter "Name LIKE 'UnrealEditor%'" -ErrorAction SilentlyContinue)
    foreach ($p in $procs) {
        $line = [string]$p.CommandLine
        if ($line -and $line.Replace('/', '\').ToLowerInvariant().Contains($needle)) { $p }
    }
}

# ---------------------------------------------------------------- project and engine

if ($Command -eq 'check') {
    & (Join-Path $PSScriptRoot 'StaticChecks.ps1') -Root $PluginDir
    exit $LASTEXITCODE
}

$uproject = if ($Project) { (Resolve-Path $Project).Path }
elseif ($Command -eq 'test' -and (Test-Path -LiteralPath $HostProject -PathType Leaf)) { $HostProject }
elseif ($File) { Find-Project $File }
else { Find-Project $null }
$uproject = [IO.Path]::GetFullPath($uproject)
$projectDir = Split-Path -Parent $uproject
$projectName = [IO.Path]::GetFileNameWithoutExtension($uproject)
$engineRoot = [IO.Path]::GetFullPath((Resolve-EngineRoot $uproject)).TrimEnd('\')
$editorCmd = Join-Path $engineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
if (-not (Test-Path -LiteralPath $editorCmd)) { throw "UnrealEditor-Cmd.exe not found at '$editorCmd'." }

# ---------------------------------------------------------------- build / lint / dump

function Invoke-Editor([string[]]$Arguments, [string[]]$KeepPatterns) {
    $all = @($uproject) + $Arguments + @('-stdout', '-FullStdOutLogOutput', '-unattended', '-nopause', '-nosplash', '-nullrhi', '-NoLiveCoding')
    Write-Verbose ("UnrealEditor-Cmd " + ($all -join ' '))
    $output = Invoke-Native $editorCmd $all
    $code = $LASTEXITCODE
    # Written to the host: what the function returns is the exit code alone.
    if ($Raw) {
        $output | ForEach-Object { Write-Host $_ }
    }
    else {
        $seen = @{}
        foreach ($text in $output) {
            $keep = $false
            foreach ($pattern in $KeepPatterns) { if ($text -match $pattern) { $keep = $true; break } }
            if (-not $keep) { continue }
            # UnrealEditor-Cmd echoes narrative log lines twice (once with a LogInit: Display: prefix).
            $clean = ($text -replace '^\[[^\]]*\]\[\s*\d+\]', '' -replace '^LogInit: Display: ', '').Trim()
            if ($seen.ContainsKey($clean)) { continue }
            $seen[$clean] = $true
            Write-Host $clean
        }
    }
    return $code
}

if ($Command -in 'build', 'lint', 'dump', 'freeze', 'vo-script', 'vo-status', 'loc-export', 'loc-import') {
    if (Get-EditorsOnProject $uproject) {
        Write-Warning "An editor has $uproject open. A build that writes assets will fight it over the packages."
    }
    $arguments = @('-run=DreamNarrative')
    if ($Command -in 'lint', 'dump') { $arguments += '-Lint' }
    if ($Command -eq 'freeze') { $arguments += '-FreezeIds' }
    if ($Command -eq 'vo-script') { $arguments += "-VoScript=$(if ($Out) { $Out } else { 'Saved/DreamNarrative/VoiceScript.csv' })" }
    if ($Command -eq 'vo-status') { $arguments += '-VoStatus' }
    if ($Command -eq 'loc-export') { $arguments += "-LocExport=$(if ($Out) { $Out } else { 'Saved/DreamNarrative/Translations.csv' })" }
    if ($Command -eq 'loc-import') {
        if (-not $File -or -not $Culture) { Write-Error 'loc-import wants the translated CSV (-File) and its culture (-Culture).'; exit 2 }
        $arguments += "-LocImport=$File"
    }
    if ($Command -in 'loc-export', 'loc-import') {
        $arguments += "-Target=$Target"
        if ($Culture) { $arguments += "-Culture=$Culture" }
        if ($NativeCulture) { $arguments += "-NativeCulture=$NativeCulture" }
    }
    if ($Command -eq 'dump') { $arguments += '-Dump' }
    if ($File -and $Command -ne 'loc-import') { $arguments += "-File=$File" }
    if ($Root) { $arguments += "-Root=$Root" }
    if ($Command -eq 'build' -and $NoSave) { $arguments += '-NoSave' }
    $keep = if ($Command -eq 'dump') { @('LogDreamNarrative', '^\d+: ', 'DNS\d{4}') } else { @('LogDreamNarrative', 'DNS\d{4}', 'Fatal', 'Assertion') }
    exit (Invoke-Editor $arguments $keep)
}

# ================================================================ test

$script:ReportPath = $null

function Stop-Run([int]$Code, [string]$Why, [string[]]$Details = @()) {
    Write-Problem $Why
    foreach ($d in $Details) { Write-Note $d }
    if ($script:ReportPath) {
        $lines = @("# DreamNarrative tests: $Preset -- stopped before a verdict", '', "- $Why") + @($Details | ForEach-Object { "  - $_" }) + @('', "Exit code: $Code")
        Set-Content -LiteralPath (Join-Path $script:ReportPath 'summary.md') -Value ($lines -join "`n") -Encoding utf8
        Write-Note "report: $script:ReportPath"
    }
    exit $Code
}

function Test-ModuleManifest {
    # The editor loads a plugin's modules through Binaries\Win64\UnrealEditor.modules. A manifest with
    # another BuildId, a module missing from it, or a zero-byte DLL makes the whole plugin fail to load.
    $problems = @()
    $uplugin = Read-JsonFile (Join-Path $PluginDir 'DreamNarrative.uplugin')
    $expected = @($uplugin.Modules | Where-Object { $_.Type -in 'Runtime', 'UncookedOnly', 'Editor', 'Developer', 'DeveloperTool' } | ForEach-Object { $_.Name })
    $manifestPath = Join-Path $PluginDir 'Binaries\Win64\UnrealEditor.modules'
    if (-not (Test-Path -LiteralPath $manifestPath)) { return @("$manifestPath does not exist: the plugin has not been built for the editor.") }
    $manifest = Read-JsonFile $manifestPath
    $engineManifest = Join-Path $engineRoot 'Engine\Binaries\Win64\UnrealEditor.modules'
    if (Test-Path -LiteralPath $engineManifest) {
        $engineBuildId = (Read-JsonFile $engineManifest).BuildId
        if ($engineBuildId -and $manifest.BuildId -ne $engineBuildId) {
            $problems += "The plugin manifest's BuildId ($($manifest.BuildId)) is not the engine's ($engineBuildId); the module manager would skip every module. Rebuild the whole editor target."
        }
    }
    foreach ($name in $expected) {
        if (-not $manifest.Modules.ContainsKey($name)) {
            $problems += "Module $name is not in $manifestPath. Rebuild the whole editor target (a -Module= build does not rewrite the manifest)."
            continue
        }
        $dll = Join-Path (Split-Path -Parent $manifestPath) $manifest.Modules[$name]
        if (-not (Test-Path -LiteralPath $dll)) { $problems += "$dll is listed but does not exist." }
        elseif ((Get-Item -LiteralPath $dll).Length -eq 0) { $problems += "$dll is zero bytes (an interrupted link); delete it and rebuild." }
    }
    return $problems
}

function Get-DevTestLag {
    # When the plugin under test is a worktree, the repository's main working tree is DevTest's copy.
    # Say so when that copy's branch is behind the one under test.
    $saved = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $listing = @(& git -C $PluginDir worktree list --porcelain 2>$null)
        $head = (& git -C $PluginDir rev-parse --abbrev-ref HEAD 2>$null)
        if ($LASTEXITCODE -ne 0 -or $listing.Count -eq 0) { return $null }
        # The first block of the listing is the main working tree; blocks end at an empty line.
        $mainPath = ($listing[0] -replace '^worktree ', '').Replace('/', '\')
        if ([string]::Equals($mainPath.TrimEnd('\'), $PluginDir, [StringComparison]::OrdinalIgnoreCase)) { return $null }
        $firstBlock = @()
        foreach ($line in $listing) { if (-not $line) { break }; $firstBlock += $line }
        $mainBranch = ($firstBlock | Where-Object { $_ -like 'branch *' } | Select-Object -First 1) -replace '^branch refs/heads/', ''
        if (-not $mainBranch -or $mainBranch -eq $head) { return $null }
        $behind = [int](& git -C $PluginDir rev-list --count "$mainBranch..HEAD" 2>$null)
        if ($behind -gt 0) {
            return "The copy in $mainPath ($mainBranch) is $behind commit(s) behind $head. With its editor closed: git -C `"$mainPath`" merge --ff-only $head"
        }
    } catch { } finally {
        $ErrorActionPreference = $saved
    }
    return $null
}

function Build-TestContent([string[]]$Files, [string]$RunDir) {
    # The game tests find their scripts the way a game does, as assets on disk; build them first.
    foreach ($relative in $Files) {
        $source = Join-Path $PluginDir $relative
        $log = Join-Path $RunDir ("content-{0}.log" -f [IO.Path]::GetFileNameWithoutExtension($relative))
        Write-Note "content: $relative"
        $output = @(Invoke-Native $editorCmd @($uproject, '-run=DreamNarrative', "-File=$source", '-unattended', '-nopause', '-nosplash', '-nullrhi', '-NoLiveCoding', "-abslog=$log"))
        $code = $LASTEXITCODE
        if ($code -ne 0) {
            $diagnostics = @($output | Where-Object { $_ -match 'DNS\d{4}' } | Select-Object -First 10)
            return @("Building the test content $relative failed (exit $code); log: $log") + $diagnostics
        }
    }
    return @()
}

function Invoke-TestRun([string]$Name, [hashtable]$Spec, [string]$RunDir) {
    $runFilter = if ($Filter) { $Filter.Trim() } else { $Spec.Filter }
    $floor = if ($Filter) { 1 } else { $Spec.MinTests }
    $timeout = if ($TimeoutMinutes -gt 0) { $TimeoutMinutes } else { $Spec.Timeout }
    New-Item -ItemType Directory -Force -Path $RunDir | Out-Null
    Set-Content -LiteralPath (Join-Path $RunDir 'filter.txt') -Value $runFilter -Encoding utf8
    if ($Spec.ContainsKey('Content')) {
        Write-Step "Building the test content for $Name"
        $problems = @(Build-TestContent $Spec.Content $RunDir)
        if ($problems.Count -gt 0) {
            foreach ($p in $problems) { Write-Problem $p }
            return 2
        }
    }

    $runLog = Join-Path $RunDir 'run.log'
    $stdout = Join-Path $RunDir 'stdout.txt'
    $stderr = Join-Path $RunDir 'stderr.txt'
    # No value may end in a backslash: the C runtime reads \" as a quote inside the argument.
    $parts = @(
        "`"$uproject`"",
        "-ExecCmds=`"Automation RunTests $runFilter`"",
        '-TestExit="Automation Test Queue Empty"',
        "-ReportExportPath=`"$($RunDir.TrimEnd('\'))`"",
        "-abslog=`"$runLog`"",
        '-unattended', '-nopause', '-nosplash', '-NoSound', '-nullrhi', '-NoLiveCoding'
    )
    if ($Spec.Game) { $parts += '-game' }
    $argLine = $parts -join ' '
    Set-Content -LiteralPath (Join-Path $RunDir 'command.txt') -Value "`"$editorCmd`" $argLine" -Encoding utf8

    Write-Step "Running $Name"
    Write-Note "filter:  $runFilter"
    Write-Note "log:     $runLog"
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $runStarted = Get-Date
    $proc = $null
    $timedOut = $false
    try {
        $proc = Start-Process -FilePath $editorCmd -ArgumentList $argLine -WorkingDirectory $projectDir -NoNewWindow -PassThru `
            -RedirectStandardOutput $stdout -RedirectStandardError $stderr
        $null = $proc.Handle   # keeps ExitCode readable after the process is gone
        if (-not $proc.WaitForExit($timeout * 60 * 1000)) {
            $timedOut = $true
            Write-Problem "Still running after $timeout minutes: killing the editor."
            try { $proc.Kill($true) } catch { }
            $null = $proc.WaitForExit(60000)
        }
    } finally {
        if ($null -ne $proc -and -not $proc.HasExited) { try { $proc.Kill($true) } catch { } }
    }
    $watch.Stop()
    $exitCode = if ($timedOut) { $null } else { $proc.ExitCode }
    # DreamNarrative.Spec.Coverage writes its matrix report under Saved; keep this run's copy with the run.
    $coverage = Join-Path $projectDir 'Saved\DreamNarrativeCoverage.md'
    if ((Test-Path -LiteralPath $coverage) -and (Get-Item -LiteralPath $coverage).LastWriteTime -gt $runStarted) {
        Copy-Item -LiteralPath $coverage -Destination (Join-Path $RunDir 'coverage.md')
    }
    Write-Note ('editor finished after {0:N0} s, exit code {1}' -f $watch.Elapsed.TotalSeconds, $(if ($null -eq $exitCode) { 'none (killed)' } else { $exitCode }))

    return (Get-Digest -Name $Name -RunDir $RunDir -Floor $floor -TimedOut $timedOut -ExitCode $exitCode -Seconds $watch.Elapsed.TotalSeconds)
}

function Get-Digest([string]$Name, [string]$RunDir, [int]$Floor, [bool]$TimedOut, $ExitCode, [double]$Seconds) {
    $runLog = Join-Path $RunDir 'run.log'
    $logLines = if (Test-Path -LiteralPath $runLog) { @(Get-Content -LiteralPath $runLog -Encoding utf8) } else { @() }
    $infra = @()
    $crash = @($logLines | Where-Object { $_ -match '=== Critical error: ===|Fatal error!|Unhandled Exception|Assertion failed:' } | Select-Object -First 5)
    if ($crash.Count -gt 0) { $infra += 'The editor crashed:'; $infra += $crash }
    if ($TimedOut) { $infra += 'The run timed out.' }
    $ensures = @($logLines | Where-Object { $_ -match 'Ensure condition failed' } | Select-Object -Unique)
    $loadFailures = @($logLines | Where-Object { $_ -match 'Plugin ''DreamNarrative'' failed to load|Unable to load module.*DreamNarrative' } | Select-Object -First 3)
    if ($loadFailures.Count -gt 0) { $infra += 'The plugin did not load:'; $infra += $loadFailures }

    $tests = @()
    $indexPath = Join-Path $RunDir 'index.json'
    if (Test-Path -LiteralPath $indexPath) {
        $index = Read-JsonFile $indexPath
        foreach ($t in @($index.tests)) {
            $errors = @()
            foreach ($entry in @($t.entries)) {
                if ($entry.event.type -eq 'Error') { $errors += [string]$entry.event.message }
            }
            $tests += [pscustomobject]@{ Path = [string]$t.fullTestPath; State = [string]$t.state; Duration = [double]$t.duration; Errors = $errors }
        }
    } elseif ($Floor -gt 0 -or $crash.Count -gt 0 -or $TimedOut) {
        $infra += "No index.json in $RunDir; the tests did not report."
    }

    $passed = @($tests | Where-Object { $_.State -eq 'Success' })
    $failed = @($tests | Where-Object { $_.State -eq 'Fail' })
    $notRun = @($tests | Where-Object { $_.State -notin 'Success', 'Fail' })
    $ran = $passed.Count + $failed.Count
    if ($ran -lt $Floor) { $infra += "Only $ran test(s) ran; this preset runs at least $Floor. Did the plugin's test module load?" }

    $code = if ($infra.Count -gt 0) { 2 } elseif ($failed.Count -gt 0 -or $ensures.Count -gt 0) { 1 } else { 0 }
    $word = switch ($code) { 0 { 'green' } 1 { 'red' } default { 'not judged' } }

    $summary = @("# DreamNarrative tests: $Name -- $word", '')
    $summary += ('Ran {0}: {1} passed, {2} failed, {3} not run (floor {4}); {5:N0} s in the editor.' -f $ran, $passed.Count, $failed.Count, $notRun.Count, $Floor, $Seconds)
    if ($infra.Count -gt 0) { $summary += ''; $summary += '## Not judged'; $summary += @($infra | ForEach-Object { "- $_" }) }
    if ($failed.Count -gt 0) {
        $summary += ''; $summary += '## Failed'
        foreach ($t in $failed) {
            $first = if ($t.Errors.Count -gt 0) { $t.Errors[0] } else { '(no error message)' }
            $summary += "- $($t.Path): $first"
        }
    }
    if ($ensures.Count -gt 0) { $summary += ''; $summary += '## Ensures'; $summary += @($ensures | ForEach-Object { "- $($_.Trim())" }) }
    if ($notRun.Count -gt 0) { $summary += ''; $summary += '## Not run'; $summary += @($notRun | ForEach-Object { "- $($_.Path) ($($_.State))" }) }
    $slowest = @($tests | Sort-Object Duration -Descending | Select-Object -First 5)
    if ($slowest.Count -gt 0) {
        $summary += ''; $summary += '## Slowest'
        $summary += @($slowest | ForEach-Object { '- {0:N1} ms  {1}' -f ($_.Duration * 1000.0), $_.Path })
    }
    Set-Content -LiteralPath (Join-Path $RunDir 'summary.md') -Value ($summary -join "`n") -Encoding utf8

    Write-Step "$Name`: $word"
    foreach ($line in $summary | Select-Object -Skip 2) { if ($line) { Write-Note $line } }
    return $code
}

# ---------------------------------------------------------------- 1. pre-flight

$runs = @(if ($Preset -eq 'All') { 'Quick', 'Explore', 'Smoke' } else { $Preset })
if ($Filter) {
    if ($Filter -match "[,;'`"]") { Stop-Run 2 "The filter may not hold commas, semicolons or quotes (the engine splits -ExecCmds on commas and the Automation command on semicolons): $Filter" }
    if ($Preset -eq 'All') { Stop-Run 2 '-Filter replaces one preset''s filter; it cannot be combined with -Preset All.' }
}
$longestFilter = if ($Filter) { $Filter } else { ($runs | ForEach-Object { $Presets[$_].Filter } | Sort-Object Length -Descending | Select-Object -First 1) }
if ($longestFilter.Length -gt $MaxFilterLength) { Stop-Run 2 'The filter is too long for the engine''s command line.' }

$reportRoot = if ($ReportDir) { [IO.Path]::GetFullPath($ReportDir) } else { Join-Path $projectDir 'Saved\DreamNarrativeTestReports' }
if ([IO.Path]::GetPathRoot($reportRoot).ToUpperInvariant().StartsWith('C:')) { Stop-Run 2 "The report directory $reportRoot is on drive C, which is nearly full. Pass -ReportDir." }
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$script:ReportPath = Join-Path $reportRoot "$stamp-$Preset"
New-Item -ItemType Directory -Force -Path $script:ReportPath | Out-Null

$pluginsUnderTest = @(Get-ChildItem -LiteralPath (Join-Path $projectDir 'Plugins') -Filter 'DreamNarrative.uplugin' -File -Recurse -Depth 2 -ErrorAction SilentlyContinue)
if ($pluginsUnderTest.Count -ne 1 -or -not [string]::Equals($pluginsUnderTest[0].DirectoryName, $PluginDir, [StringComparison]::OrdinalIgnoreCase)) {
    $found = if ($pluginsUnderTest.Count -gt 0) { ($pluginsUnderTest | ForEach-Object { $_.DirectoryName }) -join ', ' } else { 'none' }
    Stop-Run 2 "The project's DreamNarrative plugin is not the checkout this script lives in ($PluginDir); found: $found. Run the dnc.ps1 of the copy you mean to test."
}

$branch = (& git -C $PluginDir rev-parse --abbrev-ref HEAD 2>$null)
$commit = (& git -C $PluginDir rev-parse --short HEAD 2>$null)
$dirty = @(& git -C $PluginDir status --porcelain 2>$null).Count -gt 0
Write-Step "DreamNarrative tests: $Preset"
Write-Note "project: $uproject"
Write-Note "plugin:  $PluginDir ($branch $commit$(if ($dirty) { ', uncommitted changes' }))"
Write-Note "engine:  $engineRoot"
Write-Note "report:  $script:ReportPath"

if (-not $AllowEditorOpen) {
    $open = @(Get-EditorsOnProject $uproject)
    if ($open.Count -gt 0) {
        Stop-Run 2 "An editor has $uproject open (process $(($open | ForEach-Object { $_.ProcessId }) -join ', ')); the build would fail on the DLLs it holds." @('Close it, test in the host project, or pass -AllowEditorOpen.')
    }
}

# ---------------------------------------------------------------- 2. static checks

if (-not $SkipStaticChecks) {
    Write-Step 'Static checks'
    $staticLog = Join-Path $script:ReportPath 'static-checks.txt'
    $staticOutput = @(Invoke-Native 'pwsh' @('-NoProfile', '-File', (Join-Path $PSScriptRoot 'StaticChecks.ps1'), '-Root', $PluginDir))
    $staticCode = $LASTEXITCODE
    $staticOutput | Set-Content -LiteralPath $staticLog -Encoding utf8
    $staticOutput | ForEach-Object { Write-Note $_ }
    if ($staticCode -ne 0) { Stop-Run 2 'The static checks failed; nothing was built or run.' @("Findings: $staticLog") }
}

# ---------------------------------------------------------------- 3. build and manifest

if (-not $NoBuild) {
    Write-Step "Building ${projectName}Editor (Win64 Development, the whole target)"
    $buildLog = Join-Path $script:ReportPath 'build.log'
    $buildBat = Join-Path $engineRoot 'Engine\Build\BatchFiles\Build.bat'
    # -NoEngineChanges: an out-of-date engine module would otherwise be rebuilt in place, which fails
    # while any editor on this engine is open. With it UBT stops first and lists what it would write.
    $watch = [Diagnostics.Stopwatch]::StartNew()
    Invoke-Native $buildBat @("${projectName}Editor", 'Win64', 'Development', "-Project=$uproject", '-WaitMutex', '-NoHotReloadFromIDE', '-NoEngineChanges') |
        Out-File -LiteralPath $buildLog -Encoding utf8
    $buildCode = $LASTEXITCODE
    $watch.Stop()
    if ($buildCode -ne 0) {
        if (Select-String -LiteralPath $buildLog -SimpleMatch 'FailedDueToEngineChange' -Quiet) {
            Stop-Run 2 'The build would have rebuilt engine modules; nothing was built.' @('Build the engine (or the working project) with every editor closed first.', "Log: $buildLog")
        }
        $errors = @(Select-String -LiteralPath $buildLog -Pattern ': error |error [A-Z]+\d+:' | Select-Object -First 10 | ForEach-Object { $_.Line.Trim() })
        Stop-Run 2 "The build failed (exit $buildCode) after $([int]$watch.Elapsed.TotalSeconds) s." (@("Log: $buildLog") + $errors)
    }
    Write-Note "built in $([int]$watch.Elapsed.TotalSeconds) s"
}
$manifestProblems = @(Test-ModuleManifest)
if ($manifestProblems.Count -gt 0) { Stop-Run 2 'The plugin would not load as built.' $manifestProblems }

# ---------------------------------------------------------------- 4 and 5. run and digest

$final = 0
foreach ($name in $runs) {
    $runDir = if ($runs.Count -gt 1) { Join-Path $script:ReportPath $name } else { $script:ReportPath }
    $code = Invoke-TestRun -Name $name -Spec $Presets[$name] -RunDir $runDir
    $final = [Math]::Max($final, [int]$code)
}

$lag = Get-DevTestLag
if ($lag) { Write-Note "note:    $lag" }
Write-Note "report:  $script:ReportPath"
exit $final
