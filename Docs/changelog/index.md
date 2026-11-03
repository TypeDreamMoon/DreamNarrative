---
title: Changelog
description: What each version contains.
---

## Unreleased

- Localization: each build writes an extraction manifest per source file for translation tools
  (`Intermediate/DLoc/extract/DreamNarrative/`), with each text's key, source, location, kind,
  stability, `was` rename and context; `///` comments are notes for translators.
- Objective texts, archive entries and stage text take `#id`, and `dnc freeze` writes theirs too.
- Characters, quests, archives and chapters without a display name or title translate their name.
- DNS6007: two texts of one file under the same key.
- `dnc loc-export` / `loc-import` are gone: DreamLoc translates from the manifests and imports their CSV.
- Voices in other languages are the engine's localized assets under `Content/L10N/<culture>/`.

## 0.1.0

First public version:

- Language: facts and scopes, dialogue, channels and events, quests, chapters and playthroughs,
  archives, stages, and `test` / `explore` blocks, with an explained diagnostic reference.
- Runtime: ledger, dialogue runners, quests, chapters, transactional stage director and watchdog,
  bidirectional GAS bridge and ability task.
- Editor: source-writing story view, debugger, stage timeline, dialogue staging generator,
  Level Sequence import, DreamGameplayTask migration and rebuild on save.
- Tools: `dnc`, test host and four test presets, explorer, recording/replay, and VS Code extension.
