# Localization

Dialogue, choices, titles, journals, archives and stage text are localizable. The namespace is
`DNA/<FileName>` and the key is a line ID or a stable name such as `stage.<Stage>.text<n>`.
Script assets live under Asset Directory; gather them through the engine's Localization Dashboard.
Give lines explicit `#id` suffixes to keep keys stable when lines move.

Without `#id`, IDs are positional (`Dialogue.3` or `Dialogue.c2` for choices). Inserting a line changes
the IDs that follow. Before recording or translating, close the editor and run `dnc.ps1 freeze`.
It writes each current ID unchanged into the source, preserving save and translation compatibility.
Then enable `Require Line Ids` in project settings: lines without `#id` produce DNS6006.
IDs can contain dots, for example `#老周_初见.3`.

For translators, `dnc.ps1 loc-export [-Out Path] [-Culture en] [-Target Game]` exports every localizable
text to a UTF-8 CSV with BOM. Columns start with `Namespace,Key,Source,Translation`, followed by
context: kind (dialogue, choice or other text), speaker, dialogue, preceding line, emotion, file and
line number. `-Culture` fills `Translation` with existing translations from the localization target.
After translation:

1. Gather the target (default `Game`) in the Localization Dashboard, including Asset Directory.
2. With the editor closed, run `dnc.ps1 loc-import -File Translation.csv -Culture en`. It writes into
   `Content/Localization/Game/en/Game.archive`. Keys not gathered by the target and rows whose source
   changed after export are rejected and listed individually; blank translations are skipped.
3. Return to the dashboard and run **Compile Text**.

The target's native culture comes from its `.locmeta`. For a target that has never been compiled,
specify it with `-NativeCulture`.
