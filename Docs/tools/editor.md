# Editor

- **Narrative Story** in the Tools menu shows chapters, quests, dialogues, events, stages and their
  edges. Double-click to open source using the editor preference link template (VS Code by default).
  Right-click to edit conditions, connect nodes, create or delete dialogue, or **Generate staging**:
  a line-relative shot for each speaker/listener pair; invoke it again to reroll.
- **Narrative Stage Timeline** shows a stage's tracks. Drag an item, range endpoint or transition
  handle; releasing updates only that source line. Switch time bases or scrub to preview a pose;
  the level viewport can follow the shot.
- **Narrative Debugger** shows PIE chapters, checkpoints, facts and scopes, channel speech and fact
  writers. It can jump to a chapter, select one, or start a new playthrough.
- Edits have their own undo/redo and do not touch files until saving. If a file changed externally,
  the panel asks whether to discard panel edits and reload or overwrite with the panel version;
  cancel leaves both unchanged.
- `dn.importsequence /Game/路径/LS_过场` imports Level Sequence camera cuts as a stage;
  `dn.migratedgt` migrates DreamGameplayTask quests into `.dna`.
