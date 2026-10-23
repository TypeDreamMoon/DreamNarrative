# Diagnostic reference

Every diagnostic has the prefix `DNS` and four digits. Once published, a code must keep its meaning.
If the meaning changes, allocate a new code and deprecate the old one.
Every code below includes a summary and an explanation of its trigger, rationale and remedy.
Maintain this English reference alongside compiler changes; the documentation site synchronizes it
and proposes Chinese translations for review.

## Code groups

- [DNS1xxx — Lexer](dns1xxx.md)
- [DNS2xxx — Parser](dns2xxx.md)
- [DNS3xxx — Names and references](dns3xxx.md)
- [DNS4xxx — Expressions and types](dns4xxx.md)
- [DNS5xxx — Graph and reachability](dns5xxx.md)
- [DNS6xxx — Localization and assets](reference.md#localization-and-assets)
- [DNS7xxx — Stage](dns7xxx.md)
- [DNS8xxx — Generation and write-back](dns8xxx.md)
- [DNS9xxx — Toolchain](dns9xxx.md)

## Localization and assets

## DNS6001

A stage asset could not be found (warning).

**Explanation:** At compile time, assets in `montage`, `spawn` and `sequence play` are resolved by path
or name under Stage Asset Roots (default `/Game`). Resolved references are stored in the script for
packaging. Lookup can fail because of spelling, an excluded root or the wrong asset type, such as an
animation instead of a montage. Playback attempts lookup again, so it may work in the editor while
failing in a package. Correct the name or full path, or add the asset directory to Stage Asset Roots.

## DNS6002

A stage asset name matches several assets (warning).

**Explanation:** A short name matches more than one asset of the required type under Stage Asset
Roots. The compiler chooses the first and reports it; reorganizing directories may change the result.
Use a full content path such as `/Game/Path/AssetName` to select one explicitly.

## DNS6003

A line requires voice lookup, but no voice was found.

**Explanation:** Lookup is enabled by a character's `voice: Path`, `voice: table`, or a project
`Default Voice Lookup` other than None. Convention lookup replaces dots in the line ID with
underscores: `老周_初见.3` maps to `老周_初见_3`. The message identifies a missing conventional asset,
missing table row, invalid table reference or unresolved explicit line `voice` path. This warning can
be expected before recording is ready; enable `Missing Voice Is Error` before release to make it an error.

## DNS6004

A voice-table line ID has no matching dialogue line (warning).

**Explanation:** A line may have been renamed or removed, or its ID may be misspelled. This is checked
only when compiling the entire project: without command-line `-File`, or during rebuild on save.
Correct the ID or remove the unused row.

## DNS6005

A line ID occurs twice in voice tables.

**Explanation:** Duplicates within or across `*.voice.csv` files and DataTables all count. A dialogue
line can have only one voice reference; the plugin does not guess precedence. Remove the duplicate.

## DNS6006

A line lacks a stable `#id` required by the project.

**Explanation:** Without explicit IDs, dialogue and choice IDs are positional (`Dialogue.3`,
`Dialogue.c2`). Inserting a line shifts later IDs and can misalign translations, voice and saved once
records. With `Require Line Ids` enabled, every line needs `#id`. Run `dnc freeze` to write existing IDs
unchanged into source. Assign IDs to new lines or run `dnc freeze` again.
