#Requires -Version 7.2
<#
.SYNOPSIS
    Cooks and packages the test host the way a game ships, then plays the packaged game: the narrative
    scripts, and what their stages play, have to be in the package.

.DESCRIPTION
    A game finds its scripts by listing the asset directory, and a stage names its effects, montages
    and sequences in text: nothing a cook follows by default points at either. This checks the two
    things that carry them into a package -- the editor module adding every script to the cook, and
    the scripts holding what their stages play as soft references -- the only way that counts: in a
    cooked, staged build.

      1. Builds Tests/Cook/CookProbe.dna into the project's asset directory (dnc build).
      2. For each configuration: RunUAT BuildCookRun -build -cook -stage -pak into
         <project>\Saved\CookSmoke\<configuration>.
      3. The cook output has to hold the probe script, the Niagara system its stage spawns and the
         sound its stage plays and its first line speaks with.
      4. Development only (Shipping has no console to drive it): starts the staged game headless with
         `dn.load`, `dn.talk 守灯人`, `dn.next`, `quit`. Its log has to show the scripts registered
         and no stage asset missing.

    The editor has to be closed: the build and the cook write what it holds. A first cook compiles
    shaders and takes a while; later ones reuse the derived data cache.

    Exit codes: 0 every check passed; 1 a check failed; 2 a step could not run.

.EXAMPLE
    pwsh -NoProfile -File Tools\CookSmoke.ps1
.EXAMPLE
    pwsh -NoProfile -File Tools\CookSmoke.ps1 -Configuration Development
#>
[CmdletBinding()]
param(
    [ValidateSet('Development', 'Shipping')]
    [string[]]$Configuration = @('Development', 'Shipping'),

    # The .uproject. Omitted: the test host.
    [string]$Project,

    # Time limit of the packaged game, in seconds.
    [int]$GameTimeoutSeconds = 180
)

Set-StrictMode -Version 3.0
$ErrorActionPreference = 'Stop'

$PluginDir = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')).TrimEnd('\')
$HostProject = 'I:\UnrealProject_Moon\DEV_58\DreamNarrativeTestHost\DreamNarrativeTestHost.uproject'
$Probe = Join-Path $PluginDir 'Tests\Cook\CookProbe.dna'
# What the probe's stage spawns, and where a cook of the engine's Niagara content puts it.
$ProbeEffect = 'Engine\Plugins\FX\Niagara\Content\DefaultAssets\DefaultSystem.uasset'
# The sound its stage plays and its first line speaks with.
$ProbeSound = 'Engine\Content\EngineSounds\1kSineTonePing.uasset'

function Write-Step([string]$Text) { Write-Host "==> $Text" -ForegroundColor Cyan }
function Stop-Run([int]$Code, [string]$Text) { Write-Host "!!  $Text" -ForegroundColor Red; exit $Code }

function Resolve-EngineRoot([string]$UProject) {
    $association = (Get-Content -LiteralPath $UProject -Raw | ConvertFrom-Json).EngineAssociation
    $key = 'HKCU:\SOFTWARE\Epic Games\Unreal Engine\Builds'
    if (Test-Path $key) {
        $builds = Get-ItemProperty -Path $key
        if ($builds.PSObject.Properties[$association]) { return $builds.$association }
    }
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

$uproject = if ($Project) { (Resolve-Path $Project).Path } else { $HostProject }
$projectDir = Split-Path $uproject -Parent
$projectName = [IO.Path]::GetFileNameWithoutExtension($uproject)
$engineRoot = [IO.Path]::GetFullPath((Resolve-EngineRoot $uproject)).TrimEnd('\')
$runUat = Join-Path $engineRoot 'Engine\Build\BatchFiles\RunUAT.bat'
$runRoot = Join-Path $projectDir 'Saved\CookSmoke'
New-Item -ItemType Directory -Force -Path $runRoot | Out-Null
$failures = [Collections.Generic.List[string]]::new()

$editors = @(Get-CimInstance -ClassName Win32_Process -Filter "Name LIKE 'UnrealEditor%'" -ErrorAction SilentlyContinue |
    Where-Object { $_.CommandLine -and $_.CommandLine.Replace('/', '\').ToLowerInvariant().Contains($uproject.ToLowerInvariant()) })
if ($editors.Count -gt 0) { Stop-Run 2 "An editor has $uproject open; close it first." }

# ---------------------------------------------------------------- the probe

Write-Step "Building the cook probe into $projectName"
& pwsh -NoProfile -File (Join-Path $PSScriptRoot 'dnc.ps1') build -File $Probe -Project $uproject
if ($LASTEXITCODE -ne 0) { Stop-Run 2 "dnc build of $Probe failed (exit $LASTEXITCODE)." }
$assetDirectory = 'Narrative'
$probeAsset = Join-Path $projectDir "Content\$assetDirectory\CookProbe.uasset"
if (-not (Test-Path -LiteralPath $probeAsset)) { Stop-Run 2 "The probe asset is not at $probeAsset." }

# ---------------------------------------------------------------- cook, stage, play

foreach ($config in $Configuration) {
    $staging = Join-Path $runRoot $config
    $uatLog = Join-Path $runRoot "BuildCookRun-$config.log"
    Write-Step "BuildCookRun $config (log: $uatLog)"
    $started = Get-Date
    & $runUat BuildCookRun "-project=$uproject" -platform=Win64 "-clientconfig=$config" -build -cook -stage -pak `
        "-stagingdirectory=$staging" -noP4 -nocompileeditor -unattended -utf8output *> $uatLog
    $code = $LASTEXITCODE
    Write-Host ("    exit {0} after {1:N0} s" -f $code, ((Get-Date) - $started).TotalSeconds)
    if ($code -ne 0) {
        $failures.Add("BuildCookRun $config failed (exit $code); see $uatLog")
        continue
    }

    $cooked = Join-Path $projectDir 'Saved\Cooked\Windows'
    foreach ($wanted in @("$projectName\Content\$assetDirectory\CookProbe.uasset", $ProbeEffect, $ProbeSound)) {
        if (Test-Path -LiteralPath (Join-Path $cooked $wanted)) { Write-Host "    cooked: $wanted" }
        else { $failures.Add("$config : the cook left out $wanted") }
    }
    if (-not (Select-String -LiteralPath $uatLog -SimpleMatch 'narrative script(s) along' -Quiet)) {
        $failures.Add("$config : the cook log does not show DreamNarrative adding its scripts")
    }

    $suffix = if ($config -eq 'Shipping') { '-Win64-Shipping' } else { '' }
    $exe = Join-Path $staging "Windows\$projectName\Binaries\Win64\$projectName$suffix.exe"
    if (-not (Test-Path -LiteralPath $exe)) {
        $failures.Add("$config : no staged game at $exe")
        continue
    }
    Write-Host "    staged: $exe"
    if ($config -ne 'Development') { continue }

    $gameLog = Join-Path $runRoot "Game-$config.log"
    Remove-Item -LiteralPath $gameLog -ErrorAction SilentlyContinue
    Write-Step "Playing the staged $config game (log: $gameLog)"
    $arguments = @('-nullrhi', '-nosound', '-unattended', '-nosplash', '-log', "-abslog=$gameLog",
        '-ExecCmds="dn.load, dn.talk 守灯人, dn.next, quit"')
    $game = Start-Process -FilePath $exe -ArgumentList $arguments -PassThru
    if (-not $game.WaitForExit($GameTimeoutSeconds * 1000)) {
        $game.Kill($true)
        $failures.Add("$config : the packaged game did not quit within $GameTimeoutSeconds s")
        continue
    }
    if (-not (Test-Path -LiteralPath $gameLog)) {
        $failures.Add("$config : the packaged game wrote no log (exit $($game.ExitCode))")
        continue
    }
    $registered = Select-String -LiteralPath $gameLog -Pattern 'Registered (\d+) script' | Select-Object -First 1
    if (-not $registered -or [int]$registered.Matches[0].Groups[1].Value -lt 1) {
        $failures.Add("$config : the packaged game registered no narrative script")
    } else {
        Write-Host "    $($registered.Line.Trim())"
    }
    $missing = @(Select-String -LiteralPath $gameLog -Pattern 'Stage .*: no (effect|montage|level sequence|sound|camera shake) called')
    foreach ($line in $missing) { $failures.Add("$config : $($line.Line.Trim())") }
    if (-not (Select-String -LiteralPath $gameLog -SimpleMatch '灯还亮着' -Quiet)) {
        $failures.Add("$config : the packaged game never spoke the probe's line")
    }
}

if ($failures.Count -gt 0) {
    Write-Step 'Cook smoke: red'
    foreach ($failure in $failures) { Write-Host "    - $failure" -ForegroundColor Red }
    exit 1
}
Write-Step 'Cook smoke: green'
exit 0
