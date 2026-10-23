---
title: Language
description: Mix all top-level declarations in one source file.
---

- A file may mix `file`, `character`, `fact`, `chapter`, `quest`, `dialogue`, `stage`, `channel`,
  `event`, `archive`, `test` and `explore` blocks.
- **Names are unique across the project.** Characters, facts, dialogue, quests, chapters, stages,
  channels, events and archives share one namespace.
- Indentation defines nesting; `//` begins a comment. Chinese identifiers and full-width punctuation
  (`：`, `，`, `（）`) are supported.
- The editor projects text: story-view and timeline edits write back to source, leaving unedited
  lines byte-identical.

Start with [characters and facts](facts.md), [dialogue](dialogue.md), [channels and events](channels.md),
[quests](quests.md), [chapters](chapters.md), [archives](archives.md) and [expressions](expressions.md).
Every error has a `DNS` code documented in the [diagnostic reference](../diagnostics/reference.md).
