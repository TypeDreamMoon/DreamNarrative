# Architecture and decision record

> Project started on 2026-08-31. This document records architectural decisions and their rationale.
> Append new decisions to the history below. Drafts and milestone schedules live in local `Plan/`
> and are not committed. Historical proposals are identified as such; current authoring instructions
> are in the [guide](../getting-started/guide.md).

## 1. One ledger, three views

Quests, story progression and dialogue are three views of one **fact ledger**. This is the fact-system
approach also seen in Valve's dialogue system and the Witcher series' FactDB.

- **Dialogue** reads the ledger for conditions and writes it for choice consequences.
- **Quests** observe a set of entries, mark completion, broadcast events and write more facts.
- **Story progression** uses major ledger switches to select the world's current phase.

`DreamNarrativeCore` therefore contains the ledger, expressions and event bus; Story and Stage provide
their behavior around it. Quest progress can appear in dialogue conditions, and choices can advance
chapters without connecting separate stores of state. Building three isolated systems would make
later cross-system conditions expensive.

**Core is the common store of narrative progression.** Save data and a future networking layer have
one progression model to handle. Transient playback state still belongs to its runner or director.

### Difference from a push model

Conditions are expressions that reevaluate when facts change, not named counters on quest instances
that game code must update explicitly. `DreamGameplayTask` uses the latter push model. Here, game
code writes the ledger without knowing any quest, and progress is centralized rather than scattered
across quest instances.

## 2. Text is the source of truth

DSL files are authoritative; assets are compiler output; graphical editors are projections. An edit
in a graph writes back to source while retaining comments and formatting.

Treating assets as authoritative and text as import/export requires fingerprints, manual-edit
detection and Revert/Adopt behavior, a cost already incurred in DreamShader. This new system has no
legacy assets to migrate and does not need a decompiler. Git differences remain readable.

The formatting-preserving source printer is therefore a critical foundation for every graphical
editor. It needs parse → print → parse equivalence tests.

## 3. Language

- Expressions are designed for narrative work, independently of DreamUILang. Story and Stage must
  nevertheless use the same expression language within this repository.
- Unicode, including **Chinese identifiers**, is a first-class requirement: facts and dialogue can
  use authors' own language. Keywords remain English, consistently with the Dream family.
- Vocabulary covers both puzzle narratives (chapters, clue chains, recurring dialogue) and RPGs
  (journals, objectives, steps, rewards, main and side quests).
- Diagnostics use `DNS`; see the [reference](../diagnostics/reference.md). Parser, validator and
  generator share the FText sink `FDnaDiagnosticSink`. DreamShader's separate FText/FString paths
  avoided changing 561 existing error sites; this new implementation has no such compatibility cost.
- The syntax decision of 2026-09-05 combines screenplay-style text with a Yarn-like flat node model.
  Indentation gives structure, `Speaker:` introduces speech, consecutive `-` lines form choices,
  and `->` jumps. Logic appears in leading keywords and trailing modifiers. One `dialogue` block
  corresponds to one graph node; nesting beyond three levels warrants a warning recommending a
  split. This supports one-to-one graph projection, line-based write-back and readable scripts.
- Grammar is line-based. The first token classifies each logical line; indentation is the only
  cross-line structure. Write-back, VS Code lexical diagnostics and Git differences all benefit.
- Characters must be declared with `character`, enabling undeclared-speaker diagnostics. Text such
  as “Attention: the door opened” must explicitly choose speech or narration instead of being
  silently treated as a character line.
- `dialogue Name for Character if Condition [priority n]` selects the matching entry with highest
  specificity; default priority is the number of condition terms. This supports recurring NPC
  dialogue without a giant `if` tree.
- Outside literal text, full-width punctuation is equivalent to its half-width form. Full-width
  indentation spaces remain errors with a quick fix; punctuation itself is normal input for authors.
- Stable IDs may be omitted while writing, then frozen before localization or recording. The initial
  design called this `dnc stamp` and `localization: on`; the implemented interface is `dnc freeze`
  and project setting `Require Line Ids`. Named declarations use `was OldName` for redirects.
- Syntax drafts and the two original golden examples remain in local `Plan/`. The maintained
  language documentation is now under [language](../language/index.md).

## 4. Static validation as QA

The validator is an essential QA tool for solo development. It can inspect dead links, unreachable
branches, orphaned evidence, undeclared facts, chapter reachability, completable clue chains and
duplicate localization keys.

That requires knowing who can write each fact. If C++ silently changes story-owned state, reachability
analysis becomes unsound. Central declarations and explicit game ownership through `extern` establish
the boundary on which validation relies.

## 5. Boundary between Story and Stage

The division resembles the Witcher's logic/presentation split: ScreenPlay, FlowGraph and FactDB on
the Story side; Timeline on the Stage side.

Stable IDs connect the two. Stages attach to narrative lines without making either feature own the
other, so the modules can be enabled independently.

### Three roles of a stable ID

A dialogue line ID is a save key, a localization key and a stage attachment point.

- Names serve as IDs, with rename redirects to preserve existing saves.
- Lines use stable IDs and FText from the beginning. Asset compilation registers localizable text
  through `AsLocalizable_Advanced`. A custom DSL cannot join the engine's source-macro gathering
  directly because its recognized macro list is fixed, but generated assets can use normal package
  gathering. Establishing this early avoids replacing the localization workflow later.

## 6. Time bases and voice authority

The design follows the Timeline Scaling idea used in The Witcher 3: a line can last 12 seconds in one
language and 14 in another. Presentation events should store start and duration as percentages of
that line's voice duration, rather than fixed seconds.

Lines therefore have a voice reference and duration field from the start, falling back to a
character-count estimate when no voice is available. Dialogue staging uses a line-relative time
base; animation-led events such as an ability use absolute seconds.

## 7. Separate static definitions from dynamic state

Assets define quests and dialogue. Player progression belongs in snapshot structures linked by ID.
Writing runtime progression back into assets would introduce a second source of truth.

## 8. Debug jumps shape the data model

Testing chapter eight cannot require manually playing seven chapters first. Each chapter declares
its required entry state as `implied` facts, which a debug jump can supply. This belongs in the schema
from the outset rather than being added after content depends on it.

## 9. Networking is reserved in the design, not implemented

The ledger's ownership and replication boundaries are shaped for a server-authoritative model.
Version one is single-player. Reserving the shape is inexpensive compared with replacing the ledger
when multiplayer becomes necessary.

## 10. The plugin has no UI

Dialogue boxes and journals are interfaces and events; projects render their own UI. A future
DreamGUI adapter would be optional, following DreamMap's optional-dependency approach. Temporary
demonstration UI belongs in the test project, not the plugin.

## 11. Engine and gameplay modules

The runtime uses its own small evaluator rather than depending on StateTree. UE 5.8 authoring APIs
may change, and both a dialogue graph waiting for input and a set of concurrent quest observers fit
poorly into a state tree. Future flow nodes may expose StateTree attachment points.

| Module | Relationship |
| --- | --- |
| GameplayTags | Core dependency; IDs can be exported as tags at compile time |
| EnhancedInput | Direct dependency for Stage input locking and dialogue choice routing |
| LevelSequence | Embedded playback, one-way camera import, and event tracks triggering narrative; a playback wrapper is a direct dependency |
| Niagara | Direct dependency for the vfx track; `.dfs` references are a DreamFX enhancement |
| GAS | Separate adapter: tags/GameplayEvent → facts/events, quest completion → GameplayEvent, ability → stage |
| SaveGame | Snapshot structures and serialization interfaces; no mandatory plugin-owned save file |
| Sequencer | Not the main Stage representation; binding redirects, REINST and toolkit lifetime assumptions conflict with per-character, redirectable, interruptible playback |

The ledger currently uses dotted `FName` keys internally; GameplayTag export is a separate step.

## 12. Relationship to DreamGameplayTask

DreamGameplayTask is the Blueprint-oriented quest product (UE 5.0–5.6, released as v3.1.0). It and
DreamNarrative use push and observation models respectively. They do not bridge quest state, which
would create two progression stores and two save models. It has three roles here:

1. **Requirements reference:** condition templates, timeouts, subquest chains, priorities and the
   debugger are established user needs that inform the RPG vocabulary.
2. **Implementation reference:** state-machine semantics, handle lifetimes and debugger structure
   can be adapted to the new model.
3. **Migration source:** the Blueprint product remains independent; an optional importer converts
   its quest definitions to the DSL.

## 13. Synchronous callbacks and shared state

Events broadcast synchronously. Listeners can stop, replace or pause the operation currently
broadcasting, or start another operation. New callback sites must follow these rules:

- **Recheck the generation after broadcasting.** Dialogue has an execution generation; stages have
  playback IDs and request sequence numbers. Record the current generation before notifying and
  verify it, running state and pause state afterward. A superseded stack frame must stop touching
  new state. A new `Play` in a completion callback wins and the replaced outer request returns 0.
  Stopping or replacing dialogue in `OnChoiceMade` cancels that choice. Pausing suspends the choice,
  including its once record, until resumption.
- **Deliver one paired completion per nonzero playback ID or ticket.** If startup synchronously
  completes, remember completion by ID and deliver it after startup returns. Each director playback
  reports StageActivity once on start and once on finish, to the caller-specified ledger (the Stage
  subsystem supplies its own), defaulting to the GameInstance ledger. `stage: pause` listens to this.
- **Pausing does not advance.** Stage or wait completion during a pause records readiness only.
  Execution resumes after the last pause cause clears. `StopAllDialogue` removes dialogue-induced
  pauses; independent stages still pause channels that declare `stage: pause`.
- **Restore and debug jumps are not progress.** They do not execute chapter advancement, auto-start
  quests or unlock archives. At `OnSnapshotRestored`, before observers establish baselines, external
  mirrors refresh; for example, the GAS bridge rereads session tags from watched ability systems.
- **Non-fact world changes also notify.** Chapter and visited-count changes coalesce into one
  `OnWorldStateChanged` at batch end, so quests, archives and chapter rules reevaluate, including
  rules such as `advance -> X if visited(Finale) >= 1`.

Concurrent stages share world time, sunlight/skylight, HUD visibility and player view. Each world's
`UDreamNarrativeStageArbiter` owns these shared resources instead of each director independently
remembering an original value. Time and light multipliers multiply; any claimant hides the HUD;
camera ownership forms a stack with the last claimant on top and the original game view underneath.
Releasing the last claimant restores the pre-stage baseline regardless of finish order. Between
changes the arbiter leaves state untouched so the game can also change it. Projects without AHUD
should use the arbiter's `OnHudVisibility` for combined visibility; the director event reports only
that director's own claim.

## Decision history

### 2026-08-31 — Initial architecture

Sections 1–12 were approved and the v0.1.0 skeleton was implemented.

### 2026-09-05 — Syntax and scope

Six language decisions were added: screenplay syntax with flat nodes, line-based grammar, explicit
characters, entry selection, full-width punctuation and stable-ID stamping. Syntax proposal v0.1
and the Ring/RPG golden examples were reviewed and stored in local `Plan/`. Inspection of UE 5.8
confirmed Chinese GameplayTag names are valid: `InvalidTagCharacters` includes quotes, comma,
newline and tab; ini reading/writing uses UTF-8 without BOM. Facts therefore need no alternate key
scheme when exported as tags.

The extension is exclusively `.dna`; the target is UE 5.8 with no 5.6/5.7 compatibility layer.
Generated assets embed the original source; `dnc extract` recovers it without a decompiler.
Expression v1 was frozen: logic, comparisons, arithmetic including `%`, `visited`, `random`,
`chance`, quest state, `chapter`, and tag `in (…)`; no strings or ternary operator. Dialogue staging
generation follows the Stage timeline editor. The VS Code repository follows a working dialogue
pipeline and stable syntax. Ring was deferred from scope.

### 2026-09-08 — Dialogue pipeline

Implemented lexer, CST parser, validator, generator, commandlet, minimal ledger, expressions,
dialogue runner, `dn.` console commands, `dnc` and corpus tests. `visited(x)` increments on departure
(jump, finish or abort), so a node sees prior visits while executing, as in Yarn. Diagnostics use one
FText sink. Ledger keys are dotted `FName` values; GameplayTag export waits for tag-ini generation.

### 2026-09-23 — Testing strategy

Use both C++ tests and `.dna` test blocks, with a specification suite covering language capabilities.
The layers are logic, real-game smoke, automatic exploration and recording/replay. Do not run tests
automatically on editor save. Use a dedicated host project with the plugin attached as a Git worktree;
write quest tests before implementing quests. A timed-out quest reports `.failed`, while its
`on timeout` behavior is handled separately.

### 2026-09-24 — Specification tests and exploration

Test blocks gained steps, expected lines, choices, `absent` and forced random `rolls`. `Tests/Spec`
uses runtime `IDnaTrace` instrumentation against `coverage.json`; a missing coverage cell fails.
The explorer models ledger states and actions: talk to a character, start a game-only dialogue,
or assign critical values to extern facts. It explores both outcomes of random choices.
Monotone numbers compared only against constants are capped at the farthest comparison boundary.
Default checks cover runtime errors, nonterminating/trapped dialogue, save/restore and unreachable
content. Properties are `always`, `eventually` and `softlock`; failures include pasteable reproducers
(DNS51xx, DNS9201). `IDnaRandomSource` supplies controlled draws and runners can restart.

### 2026-09-24 — Recording and replay

`dn.record start Name` through `dn.record stop` writes a test under the content root's
`Tests/Recorded/`. Nondefault initial state becomes `given`; talking, starting dialogue, game fact
writes and jumps become steps; observed lines use loose matching; draws become `rolls`; final changed
facts and chapter become `expect`.

`given` can reconstruct visited counts, once choices and chapter. Chapter is set last, and initial
setup does not fire advancement rules. Runtime observer notifications cover talk/start requests,
choices, fact writers and jumps; recording does not occupy the single trace slot. Content-root test
blocks register as `DreamNarrative.Project.*`. A failed recording is rerun with identical inputs into
`.actual`, with line differences for review before replacing the original.

### 2026-09-24 — Quests

The compiler gives each quest two hidden facts, using `@` names unavailable in source: a state
(`Inactive / Accepted / Going / Completed / Failed / TimedOut`) and a latched-objective bitset.
Saves, advancement, exploration and recording therefore need no parallel quest store; `was` also
renames the hidden facts.

After each committed batch, active quests check `fail if`, latch satisfied objectives, and complete
when all required ones are latched. Facts used by objectives or journals trigger `OnQuestUpdated`
for UI refresh. After acceptance, a change to an objective fact moves Accepted to Going. Restore
establishes a baseline and is not itself a change. `.available` means inactive, failed or timed out,
with `available if` satisfied. Invalid accept/finish requests warn and return false.

Hidden quests auto-start on first availability; failure does not auto-retry, avoiding endless
start/fail loops. Timers store name and remaining seconds in snapshots, use the ledger clock and
stop while paused. Timeout executes only `on timeout`. `on` blocks compile to synchronous instruction
sequences in one batch and cannot `wait`; rewards are data broadcast for the game to grant.
Validation added DNS2011–2014. Tests gained game-authority `accept`, `complete`, `fail`, journal
assertions, and `given quest Name = State [RemainingDuration]`. Exploration can advance time to the
next expiry and encodes timer activity rather than every remaining time value; main quests default
to softlock checks. Recording captures game-side acceptance/completion. The RPG example gained a
return quest offer after choosing to think about it on the first mayor conversation.

### 2026-10-06 — Channels and events

Action-game radio, reports and banter need parallel speech. Each channel runs one dialogue; channels
run independently. Built-in `main` locks input, waits for the player and replaces its old dialogue;
only its pause/stop targets are configurable. Declared channels default to free input and automatic
line duration (`for`, or a character-count estimate), with `queue`, `interrupt` or `drop` busy policy.
`pauses` resumes from the suspended line; `stops` interrupts other channels, whose dialogues can
resume afterward if declared `resume`.

Events from game code or `emit` carry declared parameters without entering saved ledger state.
Each channel selects a matching `on` dialogue by condition, once, cooldown and priority. Only the
dialogue opened by the event sees its parameters. Once and cooldown also govern character entries;
cooldowns use the ledger clock, are not saved, and clear when restore stops all channels.

Tests gained `emit Event(Parameter = Value) [hold]` and `channel Name [strict]` expectations. Normally
each step drains all channels; `hold` leaves speech at the first line for the next step to interrupt
or pause. Exploration raises listened-to events with relevant parameter values and drains channels
after each action. Recording emits game-side events with their resulting dialogue under the same
step, recording a line only once when replayed after a pause. Console commands gained `dn.emit`,
`dn.channels` and channel selection for `dn.next` / `dn.choose`. Added DNS2015–2020 and DNS3010–3013.
The action-game golden example used the NieR:Automata prologue structure with newly written dialogue;
characters were to be replaced with originals before release, with later examples following other AAA structures.

### 2026-10-06 — GAS event bridge

Mappings live in `.dna` so text, validation and exploration remain authoritative. `event Name gameplay
Tag` listens to that GameplayEvent tag and descendants. Parameters read `from magnitude`,
`from instigator tags Tag` or `from target tags Tag`: bool tests presence; tag extracts the next
segment. `fact Name: bool extern gameplay Tag` mirrors presence, while int mirrors count.
The word `from` avoids the ambiguity between `<-` and `x <-1`.

The GameInstance bridge watches systems passed to `WatchAbilitySystem`, rebinding when later scripts
register. Unregistered project tags warn once and create no listener. Added DNS2021, DNS2022 and
DNS4007. Test tags live in the host's `Config/Tags`, not in the shipped plugin.

### 2026-10-06 — Scopes, checkpoints, playthroughs and archives

Facts occupy the save slot, unsaved `session`, or cross-slot/playthrough `profile`. Profile snapshots
use `MakeProfileSnapshot` / `ApplyProfileSnapshot`; the game owns persistence. `progress` is retained
within the slot when selecting a chapter.

First entry into a chapter through advancement, `advance`, game SetChapter, or new-playthrough entry
creates a checkpoint. Reentry does not replace it; debug jumps and restore create none. Chapter
selection restores it while retaining progress facts and their timers. The compiler marks side-quest
hidden facts as progress, keeping the ledger independent of quest concepts. New playthrough restores
an empty slot, clearing checkpoints but preserving profile, restarting hidden quests and entering
the first chapter. Snapshot version 2 added checkpoints while retaining older-save support.
Loading a save written before a fact became profile does not overwrite its profile value.

Archive entries latch through a hidden `@archive:Name` fact, with profile scope when requested;
restore does not count as an unlock. Tests gained `newgame`, `select Chapter`, `archive Name: Text`
and `archive Name = Count`; save/restore comparisons include profile and checkpoints. Exploration
includes profile state and new playthrough, returning to its start while preserving profile, but
does not model chapter selection. Recording captures both and includes profile in `given`.
Added `dn.newgame`, `dn.select`, `dn.checkpoints`, `dn.archive`, DNS2023, DNS2024 and DNS3014.

### 2026-10-07 — Source printer and editor projections

The printer preserves physical lines and their newline characters. Unedited files are byte-identical,
verified across the corpus including writing each line back to itself. Edited lines retain indent,
trailing comments and pre-comment whitespace; new lines inherit indentation and newline style.
Expressions receive only precedence-required parentheses.

`FDnaEditSession` changes conditions, adds `->` connections and creates/deletes dialogue. It reparses
after each operation and rejects newly introduced parse errors. Undo/redo uses its own text stack,
not UE transactions; saving preserves BOM presence. The story graph uses the compiler's program
and carries file/line information on chapters, quests, dialogues, events, stages, archives and their
advancement, jump, acceptance, emission, event-entry and playback edges.

Narrative Story double-clicks use the configured link template (default `vscode://file/{file}:{line}`),
with editing through context menus. Narrative Debugger shows PIE progression, checkpoints, facts,
scopes, channels and writers, with step/choose/jump/select/new-playthrough controls. Panels and the
commandlet share source-directory loading.

### 2026-10-07 — Stage runtime

Stages compile into `UDnaStageSection`, containing shots, time-sorted entries and restore operations.
Times use seconds or a fraction of the attached line. `SampleDnaStage` is a pure stage/time function
shared by director playback and timeline preview. One-shot actions such as montage, VFX and Level
Sequence playback are cues triggered when time crosses them.

The actor component `UDreamNarrativeStageDirector` records original state before its first change
and restores touched state in reverse order after completion, stopping, replacement, pawn loss or
unpossession, or world teardown handled by the watchdog. Normal completion first runs authored
restore transitions, then releases remaining state. Only local player cameras are controlled:
remote controllers route view changes back to clients and can recursively reenter those calls.

Core exposes a stage-host interface through the ledger; Stage implements it without a Story dependency.
Blocking dialogue `play` waits; line-attached `@` stops when the line leaves; quest `play` does not wait.
Without Stage, notifications still reach the game. Independent stages pause `stage: pause` channels;
line-attached ones do not. Without a controller, stage duration still runs on the ledger clock.
DNS7001–7010 validate cuts at mask peaks using the runtime envelope, restoration, units and time bases.

### 2026-10-07 — Stage timeline

The timeline shows one stage, one row per track. Dragging items, range ends and transition handles
commits one source-line edit on release. Seconds use hundredths with at least one decimal place;
fractions use thousandths. The rest of the line stays intact, preserving the verb column when space
permits. Converting seconds ↔ line-relative time uses a supplied line duration and updates `duration`
and `base: line`. Pose and optional viewport camera preview share the runtime's sampling and
actor-tag-based shot evaluation.

### 2026-10-07 — GAS output and ability task

A quest's `gameplay: Tag` emits a GameplayEvent to all watched systems on completion, but not failure
or timeout; use `emit` in an `on` block for other moments. A stage's mapping plays it on the watched
system's avatar when that event arrives, binding Target and Instigator as `target` and `instigator`.
`UAbilityTask_PlayNarrativeStage` reports `OnFinished` or `OnInterrupted`; ending the ability stops
the stage and restores the world.

### 2026-10-07 — Sequence import and dialogue staging

`dn.importsequence` creates a stage from Level Sequence camera cuts: one shot per cut, an end shot
and blend for moving cameras, and `cut -> Shot hard` between cuts. Shots support explicit
`rot (pitch, yaw, roll)` and character anchors, with world coordinates when unbound. FOV is available
from a same-named camera actor in the level.

**Generate staging** creates a line-relative close-up, over-shoulder or medium shot for each
speaker/listener pair, sometimes with a slow push. It attaches with `@` to each corresponding line.
The same seed yields identical text; reinvoking rerolls, replacing only previously generated stages
whose names begin `演出_<Dialogue>_` and their attachments. Manually staged lines are preserved.

### 2026-10-07 — DreamGameplayTask migration

`dn.migratedgt` reflects default values from `DreamTask` Blueprint subclasses without linking DGT.
Each becomes a quest with title, description as accepted journal, main priority for High/Urgent,
timeout, subquests and automatic acceptance. Each condition becomes an extern count fact in
`<Quest>_计数` and an objective. `Any` combines conditions with `or`; Custom N-of-M becomes all with a
review note. Blueprint graph logic is not converted; notes identify work to rewrite in `on` blocks.
Output goes to `<SourceDirectory>/Migrated/DreamGameplayTask.dna`.

### 2026-10-11 — External review fixes

Source basenames must be unique in an asset directory; DNS8001 rejects conflicts before modifying
assets. A remembered source outside the current source root or no longer existing may be replaced
by the new source. Panel saves check all files for external changes before writing any, offering
reload/discard or overwrite; writes use a same-directory temporary file and replacement.

Renames migrate quest timers, chapter checkpoints (first wins when old names converge; deleted
chapters are dropped), visited counts and generated once choices. Future-version slot and profile
snapshots are rejected in full. Blueprint-callable `RegisterScripts` registers all defaults before
evaluation. Stage assets resolve to paths and soft references at compile time; Cook follows those
references. Missing and ambiguous assets warn with DNS6001/6002. The editor adds all scripts under
Asset Directory to Cook, and the director preloads playback assets asynchronously. Section 13
defines callback and shared-state contracts. `Tools/CookSmoke.ps1` cooks, packages Development and
Shipping, and launches both to verify script and stage assets.

### 2026-10-11 — Voice, stage audio, blocking and localization

`dnc freeze` writes existing automatic dialogue/choice IDs (`Dialogue.N`, `Dialogue.cN`) as explicit
` #id`, rereads and verifies, and reverts mismatches. Later insertions no longer shift IDs; dotted IDs
are supported. `bRequireLineIds` adds DNS6006 for missing IDs after recording or translation begins.

Characters select conventional `voice: Path`, `voice: table`, or the project default (initially no
lookup). Conventional paths replace dots in line IDs with underscores. Tables come from
`*.voice.csv` or configured DataTables with `FDnaVoiceRow` and line IDs as row names; duplicates give
DNS6005. Explicit line `voice Path` takes precedence. Compilation stores voice soft references and
asset `Duration`; missing voice gives DNS6003 (optionally an error), unused table rows DNS6004.

The plugin emits audio requests, not audio playback. Runner `OnVoice` requests on line appearance,
stops on leaving/skipping/pausing/aborting, and accepts game `NotifyVoiceFinished`. Story forwards
events by speaker and stops a speaker's earlier voice when another channel starts that speaker.
Automatic channels use voice duration plus `VoiceTailSeconds`, or wait for game notification with
`bWaitForVoiceFinished`, bounded by `VoiceWaitGraceSeconds`. Entry preloads the dialogue's voices.
`dnc vo-script` exports a recording CSV; `dnc vo-status` reports coverage by speaker.

Stage audio likewise emits events: director `OnSound` / `OnSoundStopped` for sounds, and arbiter
`OnMusic` for a world-wide last-started-first stack. `keep` music becomes game-owned after its stage.
Camera `shake` affects only the local player and stops with the stage. Blocking `move`, `face` and
`look` target marks or actors; ranges interpolate movement, points teleport, and `navigate` uses
navigation. `IDnaStageActor` owns foot/head animation. Actors remain at their destinations unless
`restore` contains `blocking back`.

`SampleDnaStageBlocking` is shared by playback and preview: each segment starts from prior segment
endpoints and targets the position sampled at its start, using common targets and easing. Preview
moves level actors and restores them afterward; navigation is a cue outside sampling. Runtime
segments start from actors' actual positions so game or navigation moves do not snap back.

`dnc loc-export` uses the engine gatherer's namespace/key rules to export translation CSV with
speaker, dialogue, previous line, emotion and source position. `dnc loc-import` writes the target
archive through `FLocTextHelper`, as dashboard PO import does. It rejects ungathered keys and changed
source rows; the dashboard then compiles text normally. CookSmoke probes gained stage audio and
dialogue voice. DNS7006/7007 verb lists were extended accordingly.
