# DreamNarrative test host

A minimal Unreal project that exists only to build DreamNarrative and run its automation tests, away
from any project you actually work in.

## Why

Building and testing inside the working project (DevTest) costs three things:

- **Its editor has to be closed.** A build relinks the plugin's DLLs, which a running editor holds
  open; a test run needs an editor process of its own on the same project.
- **"The project builds" is not "the plugin builds".** A project that enables thirty other plugins can
  hide a dependency the plugin forgot to declare.
- **Its config and content leak into the results.** A test must not pass because DevTest happens to
  have a setting or an asset.

The host removes all three. Its `Plugins/DreamNarrative` is a separate **git worktree** of this
repository with its own `Binaries/` and `Intermediate/`, so it builds and runs while DevTest's editor
stays open; it enables nothing but DreamNarrative (whose own descriptor brings Niagara, Enhanced Input
and Gameplay Abilities); it has no content or code of its own.

## Layout

```
DreamNarrativeTestHost/                    (default I:\UnrealProject_Moon\DEV_58\DreamNarrativeTestHost)
  DreamNarrativeTestHost.uproject          from Template/
  Config/DefaultEngine.ini                 from Template/
  Config/DefaultGame.ini                   from Template/
  Source/DreamNarrativeTestHost*.Target.cs from Template/
  Source/DreamNarrativeTestHost/           from Template/ -- an empty primary game module
  Plugins/DreamNarrative/                  a git worktree of this repository
  .dreamnarrative-testhost.json            what the host was made from
  Binaries/ Intermediate/ Saved/           created by the first build and run
```

The template is here, in `Tools/TestHost/Template/`, outside the plugin's `Source/` folder, so no
project that carries the plugin ever picks its `.Build.cs` or `.Target.cs` files up.

## Creating it

```powershell
pwsh -NoProfile -File Tools\TestHost\New-DreamNarrativeTestHost.ps1 -WhatIf   # what it would do
pwsh -NoProfile -File Tools\TestHost\New-DreamNarrativeTestHost.ps1
```

| Parameter | Default | |
| --- | --- | --- |
| `-Root` | `I:\UnrealProject_Moon\DEV_58\DreamNarrativeTestHost` | The host directory. Refused on drive C. |
| `-RepoPath` | the repository this script is in | Any working tree of the repository. |
| `-Branch` | `dev` | What the worktree must have checked out. |
| `-StartPoint` | `main` | Where `-Branch` starts when it does not exist yet. |
| `-EngineRoot` | looked up from the template's engine association | |
| `-Force` | off | Replace files that differ from the template, keeping `<name>.bak-<timestamp>`. |
| `-KeepCurrentHead` | off | Accept an existing worktree on another branch or a detached commit. |

It checks before touching anything, never removes, resets or switches an existing worktree, and is
safe to run again. Exit codes: `0` the host matches the template; `1` a check failed; `2` usable but
different from the template (or `-WhatIf` found work to do).

## Working with two checkouts

git does not let one branch be checked out in two worktrees at once, so the two copies of the plugin
have different jobs:

- **Day-to-day work happens in the host's worktree**, on `dev`: edit, build, test and commit there.
- **DevTest's copy stays on `main`.** To see the work in DevTest, fast-forward it with DevTest's editor
  closed, then build DevTest as usual:

  ```powershell
  git -C I:\UnrealProject_Moon\DEV_58\DevTest\Plugins\DreamNarrative merge --ff-only dev
  ```

`dnc test` prints a note when DevTest's copy is behind `dev`.

## Building and running

`Tools/dnc.ps1 test` owns building and running and picks the host up by itself when it exists. The
editor target (`DreamNarrativeTestHostEditor`) uses the **shared** build environment: engine modules
already built are linked against as they are, so the first build costs one plugin build, not an engine
build. The runner builds with `-NoEngineChanges`: if an engine module is out of date it stops before
the first action instead of rebuilding engine DLLs that other editors hold open.

Never build the game target (`DreamNarrativeTestHost`) casually: a game target against a source engine
is monolithic with a unique build environment, which compiles the whole engine into this project.

## How the host differs from DevTest

| | DevTest | Host |
| --- | --- | --- |
| Plugins | DreamNarrative plus about thirty others | DreamNarrative and what its descriptor needs |
| Content | the project's own, `/Game/Narrative/ring` among it | none until a test run builds its own |
| Startup map | a full showcase level | `/Engine/Maps/Entry` |
| MoonToon ramp atlases | loaded from DevTest's MoonToon plugin | `None` (that plugin is not there) |
| Narrative settings | DevTest's `DefaultDreamNarrative.ini`, if any | the plugin's defaults |
