---
title: Director and restoration
description: Apply a sampled pose each frame and restore world state however playback ends.
---

- A pose is a pure function of stage and time (`SampleDnaStage`). The director applies it each frame;
  timeline scrubbing uses the same function, so preview and gameplay agree. One-shot actions such
  as montage, VFX and Level Sequence playback are cues triggered when time crosses them.
- The **director** (`UDreamNarrativeStageDirector`, an actor component) records original state before
  first changing it. Completion, stopping, replacement, pawn loss or unpossession, owner destruction
  and leaving the level all restore state in reverse order. Normal completion first executes the
  authored `restore` block.
- The **watchdog**, a world subsystem, stops every stage on world teardown. It also handles stalled
  directors while world time advances, or playback that substantially exceeds its end.
- The Stage subsystem is the ledger's stage host. Blocking dialogue `play` waits; line-attached `@`
  follows that line and stops on departure; quest `play` does not wait. It uses the last-started
  director or automatically adds one to the first local player controller. Without a controller,
  duration still runs on the ledger clock.
- `stage: pause` channels pause during independent stages. Restore stops all stages.
- Camera control applies only to the **local** player.
- UI hooks include `OnText` for subtitles/floating text, `OnHudVisibility`, `OnDamageWindow` and
  `OnWarpTarget` for Motion Warping.

See [Stage language](index.md) for authoring and shared-state behavior.
