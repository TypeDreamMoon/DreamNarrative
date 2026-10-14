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

Version 0.1.0, beta. In place and covered by the automated suite (`Tools/dnc.ps1 test`):

- **The language** — facts, characters, dialogue with choices and conditions, channels that talk side by side,
  events with parameters, quests, chapters with checkpoints and new playthroughs, archives, stages, and `test` /
  `explore` blocks — with a validator whose every diagnostic is documented in the [diagnostic reference](Docs/diagnostics/reference.md).
- **The runtime** — the ledger with its save scopes, dialogue runners, quests, chapters; the stage director with
  transactional restore, a watchdog, and camera, light, mask, effect, HUD, animation, game-time and level
  sequence tracks; the Gameplay Ability System bridge both ways and an ability task that plays a stage.
- **The editor** — the story view (edits write back to the source), the stage timeline, the runtime debugger,
  a dialogue staging generator, level sequence import, DreamGameplayTask migration, and rebuild on save.
- **Tooling** — `dnc` build / lint / test, an exploring model checker, recording and replay, and a VS Code
  extension (`dreamnarrative-language-support`).

Start with the [English guide](Docs/getting-started/guide.md), the [documentation index](Docs/README.md),
and the [Lighthouse example](Examples/Lighthouse). Plugin documentation is the English source;
Chinese translations are maintained and reviewed in the documentation site repository.

## Modules

| Module | Type | What lives in it |
|---|---|---|
| `DreamNarrativeCore` | Runtime | The fact ledger, the expression language, the event bus, the stage host interface |
| `DreamNarrativeStory` | Runtime | Dialogue, channels, quests, chapters, archives |
| `DreamNarrativeStage` | Runtime | The stage director, its transactional restore and watchdog |
| `DreamNarrativeGAS` | Runtime | Optional bridge to the Gameplay Ability System |
| `DreamNarrativeCompiler` | UncookedOnly | Lexer, parser, validator, generator, source printer, edit session, commandlet |
| `DreamNarrativeEditor` | Editor | Story view, stage timeline, runtime debugger, importers, source watcher |
| `DreamNarrativeTests` | UncookedOnly | The automated tests and their fixtures |

Story and Stage meet in exactly one place: the story asks the ledger's stage host for a stage by name. Either
can be enabled without the other.

## Engine

Unreal Engine 5.8 (builds against the launcher's stock 5.8). Required engine plugins: GameplayTags,
EnhancedInput, Niagara — all stock. GameplayAbilities is optional and only the `DreamNarrativeGAS` module touches it.

## Contact

- Author: DreamMoon — https://dmstudio.top
- GitHub: [TypeDreamMoon](https://github.com/TypeDreamMoon)
- Email: 1067823908@qq.com

Documentation: [English](Docs/README.md) · [简体中文](Docs/zh-CN/README.md) (imported translations awaiting review).
