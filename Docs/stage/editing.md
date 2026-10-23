---
title: Timeline, generation and import
description: Stage timeline editing, dialogue staging generation and Level Sequence import.
---

**Narrative Stage Timeline**, in the Tools menu, shows one stage's tracks. Drag items, range ends or
transition handles for blend, fade, ramp and push. Releasing changes only that source line: times use
the stage's units, the rest remains intact, and the verb retains its column when space permits.
Switch time bases using a supplied line duration. Scrub to inspect the pose, optionally letting the
level viewport follow the shot.

The **dialogue staging generator**, available as **Generate staging** in the story-view context menu,
creates a line-relative stage per speaker/listener pair: close-up, over-shoulder or medium shot,
sometimes with a slow push. It attaches with `@` to that pair's lines. The same seed produces the
same text; reinvoking rerolls the seed and replaces only prior generated content, preserving manually
staged lines.

**Level Sequence import:** `dn.importsequence /Game/路径/LS_过场 [StageName]` writes a stage under the
source directory's `Stages/`. Each camera cut becomes a shot. Moving cameras receive an end shot
and an in-cut blend; transitions between cuts use `cut -> Shot hard`.
