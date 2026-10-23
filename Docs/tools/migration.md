---
title: Migrate from DreamGameplayTask
description: Convert Blueprint quest definitions into .dna quests.
---

`dn.migratedgt` reflects default values from the project's `DreamTask` Blueprint subclasses, without
requiring a DGT link dependency. Output is `<SourceDirectory>/Migrated/DreamGameplayTask.dna`:

- Each task becomes a `quest`: title, description as the accepted journal, main priority for
  High/Urgent, timeout, subquests and automatic acceptance.
- Each condition becomes a game-owned count fact in `<Quest>_计数` and an objective. `Any` combines
  conditions into one `or` objective.
- Unsupported behavior, including Blueprint graph logic and N-of-M completion, is listed in the
  migration log for manual rewriting in `on accept`, `on complete` or `on fail` blocks.
