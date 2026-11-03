# Localization

Dialogue, choices, quest titles, journals and objectives, archives, character names, chapter titles and
stage text are localizable. The namespace is `DNA/<FileName>`; the keys are:

| Text | Key |
| --- | --- |
| Line | its `#id`, else `Dialogue.<n>` |
| Choice | its `#id`, else `Dialogue.c<n>` |
| Quest | `quest.<Quest>.title`, `quest.<Quest>.journal.<accepted\|going\|completed\|failed>`, `quest.<Quest>.objective<n>` or `quest.<Quest>.<id>` |
| Archive | `archive.<Archive>.title`, `archive.<Archive>.entry<n>` or `archive.<Archive>.<id>` |
| Character | `character.<Character>` |
| Chapter | `chapter.<Chapter>` |
| Stage text | `stage.<Stage>.text<n>` or `stage.<Stage>.<id>` |

A character without `display:`, or a quest, archive or chapter without `title:`, shows its name, and
that name is translated like a title. Texts are format patterns: an interpolation is `{0}`, `{1}` in the
order written, and a literal brace is escaped with `` ` ``. Two texts of one file under the same key are
DNS6007. Script assets live under Asset Directory; the engine's gather reads them as it reads any asset.

## Keys that hold still

Without `#id` a key is numbered by position (`Dialogue.3`, `Dialogue.c2`, `objective2`, `entry0`,
`text1`). Inserting a text before it moves every key after it onto its neighbour's text, together with
the translations, voices and save records that follow the key. Before recording or translating, close
the editor and run `dnc.ps1 freeze`. It writes each current ID unchanged into the source: `#Dialogue.3`
on lines and choices, `show: Text #objective2`, `entry #entry0`, `text 「…」 #text1`. A text added
afterwards takes the next number no `#id` in its dialogue, quest, archive or stage has written, so it
never lands on a frozen key; run `dnc.ps1 freeze` again to fix it as well. Then enable
`Require Line Ids` in project settings: lines without `#id` produce DNS6006. IDs can contain dots, for
example `#老周_初见.3`.

## Voices in other languages

Voices are not translation entries. Each language's recording is the engine's localized asset at the
same path under `Content/L10N/<culture>/`; a game loads it in place of the source-language voice (the
editor and PIE do not). See [Voice](../runtime/index.md).

## Notes for translators

`///` comment lines right above a line, a choice or any other text are its note:

```dna
dialogue 老周_初见 for 老周
  /// First meeting: curt, not polite small talk
  老周(皱眉): 你就是新来的守夜人?
```

## Extraction manifests

Each time a file is built (by `dnc build` or by the editor when a source is saved), its texts are written
to `<Project>/Intermediate/DLoc/extract/DreamNarrative/<source path>.json` for translation tools such
as DreamLoc. The plugin does not depend on any of them. A file outside the source directory has no
manifest; the same texts rewrite nothing.

```json
{
  "version": 1,
  "provider": "DreamNarrative",
  "source": "DNarrative/Ch01/灯塔.dna",
  "namespace": "DNA/灯塔",
  "entries": [
    { "key": "老周_初见.1", "src": "你就是新来的守夜人?", "at": "老周_初见#老周_初见.1", "kind": "line",
      "note": "First meeting: curt, not polite small talk", "stable": false,
      "context": { "speaker": "老周", "speakerDisplay": "老周", "dialogue": "老周_初见", "tags": "皱眉" } }
  ]
}
```

- `at`: where the text is, without a line number: `Dialogue#Id`, `quest.Quest#title`, `stage.Stage#text0`.
- `kind`: `line`, `choice`, `quest`, `archive`, `character`, `chapter` or `stage`.
- `stable`: `false` for a key numbered by position, left out otherwise. A translation tool can warn when
  such a key's text changes, since it may have moved onto another text; `dnc freeze` fixes it.
- `renamedFrom`: the key before a `was` rename of the dialogue, quest, character or chapter.
- `context`: `speaker`, `speakerDisplay`, `dialogue`, `previous` (the line before in source order),
  `tags`, `channel`, `for` (seconds), `voice` (the voice found, or the asset the convention looks for),
  `quest`, `archive`, `stage`, and `args`: what `{0}`, `{1}` stand for, as written.

The same note and context go to the engine's gather as the text's developer notes.

## Translating

DreamLoc reads the manifests: `dloc sync` turns them into one `.dloc` file per source and language,
next to the project's other translations, and its translation source shows them in the game. Translations
made with the `dnc loc-export` / `loc-import` CSV of earlier versions go in once with
`dloc import -In Translation.csv -Culture en`: a row whose source has changed since is kept as stale,
and keys that no longer exist are listed. Without DreamLoc, the script assets can still be gathered
through the engine's Localization Dashboard like any other asset.
