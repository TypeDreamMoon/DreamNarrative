# Game integration

```cpp
UDreamNarrativeStory* Story = GetGameInstance()->GetSubsystem<UDreamNarrativeStory>();
Story->LoadAllScripts();                       // All scripts under Asset Directory

UDreamNarrativeDialogueRunner* Runner = Story->TalkTo(TEXT("老周"));
Runner->OnLine.AddDynamic(this, &AMyHUD::ShowLine);        // FDnaLineView: speaker, text, duration, channel
Runner->OnChoices.AddDynamic(this, &AMyHUD::ShowChoices);  // Call Runner->Choose(Index) for a selection
// Player advancement: Runner->Advance()

Story->GetLedger()->SetFactInt(TEXT("找到的零件"), 2);        // An extern fact
FDnaEventPayload Storm; Storm.Event = TEXT("风暴逼近"); Storm.Set(TEXT("等级"), FDnaValue::FromInt(4));
Story->GetLedger()->RaiseEvent(Storm);
```

- To load several scripts manually, collect their assets and call `Story->RegisterScripts(Scripts)`
  (also available to Blueprint). It registers all fact defaults before evaluating hidden quests and
  archive conditions. `LoadAllScripts()` follows this sequence; use `RegisterScript()` for one script.
- Each declared channel has a runner; `Story->OnDialogueStarted` reports each one. The plugin emits
  events and provides no UI.
- Save with `Ledger->MakeSnapshot()` / `ApplySnapshot()`. Profile data has separate
  `MakeProfileSnapshot()` / `ApplyProfileSnapshot()` calls. Snapshots from a newer version are rejected
  in full, returning false.
- Scripts in Asset Directory are automatically included in Cook; Primary Asset or Always Cook setup
  is unnecessary. `Tools/CookSmoke.ps1` cooks, packages Development and Shipping builds, and launches
  the packaged game in the test host to verify it.
- Stage UI hooks include the director's `OnText` (subtitles and floating text), `OnHudVisibility`,
  `OnDamageWindow` and `OnWarpTarget` (for Motion Warping).
- GAS: call `UDreamNarrativeGASBridge::WatchAbilitySystem(ASC)`. `event Name gameplay Tag` converts a
  GameplayEvent into a narrative event; `fact Name: bool extern gameplay Tag` tracks tag presence/count.
- Console commands: `dn.load`, `dn.talk Character`, `dn.start Dialogue`, `dn.next`, `dn.choose n`,
  `dn.fact Name [Value]`, `dn.facts`, `dn.jump Chapter`, `dn.emit Event`, `dn.channels`, `dn.quests`,
  `dn.accept / complete / fail Quest`, `dn.newgame`, `dn.select Chapter`, `dn.checkpoints`, `dn.archive`,
  and `dn.record start|stop`.

## Voice

The plugin does not play audio. At compile time it resolves a voice asset and duration for each line;
at run time it tells the game when to start and stop, leaving playback to the game's audio system.

```dna
character 老周
  voice: /Game/VO/老周      // Convention: /Game/VO/老周/<LineId>, with dots replaced by underscores
character 小雾
  voice: table              // Look up the voice table
dialogue 老周_初见 for 老周
  老周: 你就是新来的? #老周_初见.1
  老周: 灯坏了三天了。 voice /Game/VO/特殊/灯坏了   // Override this line's asset
```

- Choose a lookup mode per character. Without `voice:`, the character uses `Default Voice Lookup`
  from project settings; its default, None, performs no lookup and reports no missing voice.
- Voice tables can be `*.voice.csv` files under the source directory, with `LineId` and `Voice`
  columns (other columns are ignored), or configured DataTables using `FDnaVoiceRow`, with line IDs
  as row names. Duplicate lines produce DNS6005; missing voice produces DNS6003; unused table rows produce DNS6004.
- Resolved voice assets become script references and are packaged with the script. Duration comes
  from the asset's `Duration`. Run `dnc freeze` before recording so line IDs stop changing.
- `Story->OnVoice(Runner, Event, Line, Speaker)` receives voice events from all channels; each runner
  also exposes `OnVoice`:
  - `Requested`: the line appeared; play `Line.Voice`. Redisplaying it after a pause requests playback again from the start.
  - `Stopped`: the line was cut short by player advancement, pause, dialogue termination, or the same
    speaker starting in another channel; stop playback.
  - `Finished`: the line finished before advancement, either through the automatic timer or a game call to `NotifyVoiceFinished`.
- `Speaker` is the actor bound with `Story->BindSpeaker(Character, Actor)`, or otherwise an actor
  tagged with the character name. Integrate lip sync and facial animation here: on `Requested`, play
  `Line.Voice` on `Speaker` (for example, `SpawnSoundAttached` on the head bone) and pass that audio
  component to the chosen lip-sync system, such as MetaHuman audio-driven animation, Audio2Face or
  OVRLipSync. On `Stopped`, stop the component and its lip sync. The plugin manages neither audio nor faces.
- Automatic channels advance after voice duration plus `Voice Tail Seconds`. If the game's audio
  system knows the end more accurately, call `Runner->NotifyVoiceFinished()`. Enable `Wait For Voice
  Finished` to wait for that notification instead; if none arrives by duration plus
  `Voice Wait Grace Seconds`, advancement proceeds anyway.
- A speaker has only one active voice at a time. Starting in another channel first issues `Stopped`
  for their previous voice; the previous line can remain on screen.
- `dnc.ps1 vo-script [-Out Path]` exports a recording script containing line ID, speaker, source text,
  dialogue, preceding line, emotion, voice status and expected conventional asset name. It is sorted
  by speaker and encoded as UTF-8 with BOM for spreadsheet applications. The first columns are
  `LineId,Voice`: fill in `Voice`, rename the file to `xxx.voice.csv`, and put it in the source directory
  to use it as a voice table. `dnc.ps1 vo-status` reports totals, coverage and missing lines by speaker.
