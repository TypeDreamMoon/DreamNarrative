# Dream Narrative

Quests, story progression and dialogue for Unreal Engine, authored as text.

The three are one system wearing three faces. Underneath them is a single **fact ledger** — "the player
met the mayor", "3 of 5 wolves killed", "the story is in chapter two". Dialogue reads the ledger for its
conditions and writes to it when a choice is made; quests are observers watching a handful of entries;
chapters are gates on a few big ones. Add a system later — achievements, tutorials, codex entries — and
it is another face on the same ledger, not another store of truth.

**Text source files are the single source of truth.** The graphical editors are projections: an edit in
the story graph or on the stage timeline is written back to the source file, comments and formatting
untouched. Assets are compiler output and nothing else.

That is what buys the feature no graphical dialogue tool can offer — the whole narrative is statically
checkable. Dead links, unreachable branches, orphaned evidence, a fact nobody ever writes, a chapter no
path can reach, a localization key used twice: all compile-time errors.

## Status

Version 0.1.0 — skeleton. Modules and boundaries are in place; the language, the runtime and the editors
are being built on top. Nothing here is usable in a project yet.

## Modules

| Module | Type | What lives in it |
|---|---|---|
| `DreamNarrativeCore` | Runtime | The fact ledger, the expression language, the event bus |
| `DreamNarrativeStory` | Runtime | Dialogue, quests, chapters |
| `DreamNarrativeStage` | Runtime | Multi-track performance evaluator, transactional restore |
| `DreamNarrativeGAS` | Runtime | Optional bridge to the Gameplay Ability System |
| `DreamNarrativeCompiler` | UncookedOnly | Lexer, parser, validator, generator, source printer, commandlet |
| `DreamNarrativeEditor` | Editor | Story graph view, stage timeline, runtime debug panel |

Story and Stage meet in exactly one place: a performance attaches to a narrative line by its id. Either
can be enabled without the other.

## Engine

Unreal Engine 5.8. Required engine plugins: GameplayTags, EnhancedInput, Niagara, LevelSequence — all
stock. GameplayAbilities is optional and only the `DreamNarrativeGAS` module touches it.

## Contact

- Author: DreamMoon — https://dmstudio.top
- GitHub: [TypeDreamMoon](https://github.com/TypeDreamMoon)
- Email: 1067823908@qq.com
