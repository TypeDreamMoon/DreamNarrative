---
title: dnc and testing
description: Command-line tools, test presets, exploration and recording/replay.
---

```powershell
Tools/dnc.ps1 build                  # Compile all .dna files in Source Directory with the editor closed
Tools/dnc.ps1 lint                   # Check without writing assets
Tools/dnc.ps1 dump -File 灯塔.dna      # Inspect the syntax tree
Tools/dnc.ps1 test -Preset Quick     # Logic, specification and smoke; also Explore, Smoke and All
```

- Tests run in the plugin's host project with a headless editor. Reports of passes, failures and
  slowest tests go to `Saved/DreamNarrativeTestReports`.
- **Specification suite:** each `test` block is an automation test; the coverage matrix verifies
  that blocks exercise runtime paths.
- **Explorer:** `explore` blocks enumerate paths and report softlocks, `always` counterexamples and
  unreachable content.
- **Smoke tests:** use a real GameInstance and world, play a stage on a player controller, interrupt
  it, and read back world state for comparison.

See [test and explore blocks](../language/tests.md) for authoring and recording tests.
