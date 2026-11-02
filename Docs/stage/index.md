# Stage language

```dna
stage 灯亮
  duration 5.0s
  shots
    灯室       local (0, 0, 900) of 灯塔 look 灯塔 fov 50
  camera
    0.0s     blend from game 0.5s
    0.5s     blend -> 灯室 1.5s
    2.5s     cut -> 海面
    5.0s     blend back 0.6s
  mask
    2.4s..2.6s   fade white 0.1s      // The cut must coincide with maximum mask strength (DNS7001)
  light
    0.0s     ramp dark 0.5s to 0.3
    2.5s     restore 0.3s
  ui
    0.0s..5.0s   hide hud
  game
    0.0s..5.0s   input lock
    2.5s..2.7s   hitstop 0.2x
```

- Tracks include camera, light, mask, vfx, ui, anim, game, sequence, audio and blocking.
  See DNS7006 in the diagnostic reference for the verb list.
- On-screen `text` is translated under `stage.<Stage>.text<n>`, numbered from 0 in time order;
  `0.5s text 「…」 #id` names it `stage.<Stage>.<id>` instead, and `dnc freeze` writes the numbers in.
- The default time base is seconds. A stage with `base: line` follows its attached dialogue line,
  uses percentages, and can only be attached with `@StageName`.
- Shots are positioned by character name: the name is an actor tag, or can be bound through the
  director's `BindActor`. `rot (pitch, yaw, roll)` specifies an explicit orientation; an unbound anchor
  uses world coordinates.
- Declare how changed world state will be restored, either later on the same track or in a `restore`
  block (DNS7003). The director is transactional: completion, stopping, replacement, player death and
  leaving the level all restore the state it touched.
- The Stage subsystem sends dialogue `play`, line `@`, and quest `play` requests to the most recently
  started director, or automatically adds one to the first local player controller. Without a player
  controller, the stage still runs for its duration on the ledger clock, and the story still waits.
- With multiple directors, time and sunlight/skylight multipliers multiply; any stage can keep the HUD
  hidden; the most recently acquired camera is active. Finishing one stage preserves the others' claims,
  and the last finish restores the original state. Between changes, these states are left alone so the
  game can modify them. For HUDs other than AHUD, use the world subsystem
  `UDreamNarrativeStageArbiter::OnHudVisibility`, which represents all stages combined. Calling `Play`
  again in a director's completion callback gives the new request priority; the superseded outer request returns 0.
- Assets named by `montage`, `spawn` and `sequence play` are resolved at compile time. Names are searched
  under Stage Asset Roots; full paths are accepted directly. Resolved paths and references are stored
  in the script asset and included in packaging. DNS6001 warns if lookup fails: editor playback may
  still work, but the packaged game will probably lack the asset.
- The `audio` track emits events; the game plays the audio:
  - `0.5s sound /Game/SFX/打击 [at Character] [volume 0.8]` emits one director `OnSound` event.
    A range such as `0.0s..3.0s sound 风声` is a sustained sound; `OnSoundStopped` asks the game to stop it
    when the range or stage ends.
  - `0.0s music /Game/BGM/回忆 [fade 1.0s]` uses the world-wide music stack. The arbiter's
    `OnMusic(Music, Fade)` names the current track. A later stage overrides an earlier one, then reveals
    it again on completion. When the last stage ends, an empty music name means return to the game's
    own music. A ranged music entry lasts only for its range. A point entry with `keep`, such as
    `0.0s music /Game/BGM/首领 keep`, persists after its stage ends. Once no other stage overrides it,
    it becomes the game's music, and the arbiter stops issuing `OnMusic` for it. `keep` is valid only on point entries.
- The `blocking` track moves characters to `marks`. Marks use the same syntax as shots;
  `look Character` specifies whom to face on arrival:

  ```dna
  marks
    门口     local (500, 0, 0) of 灯塔 look 老周
  blocking
    0.0s..2.0s   move 守夜人 -> 门口 run        // Move over a range; a point teleports; navigate uses the navmesh
    2.0s         face 守夜人 -> 老周 over 0.3s  // Turn toward a mark or character
    0.0s..3.0s   look 老周 at 守夜人           // Look at the target until the range ends
  restore
    blocking back                              // Restore moved actors; omit this to leave them at their destinations
  ```

  The director interpolates position and rotation over time. The actor owns foot and head animation:
  implement `IDnaStageActor` in C++ or Blueprint to receive `StageMoveStarted`, `StageMoveFinished`
  and `StageLookAt`. A target that is not a mark is resolved as a character actor.
  Blocking position is a pure function of stage and time (`SampleDnaStageBlocking`). Enable
  **Preview in viewport** and scrub the timeline to move actors tagged with character names;
  disabling preview or changing stages restores them. `navigate` movement uses navigation and is excluded from preview.
- Camera shake: `0.5s shake /Game/镜头/BP_震动 [scale 1.5]` on the camera track uses a CameraShakeBase
  Blueprint and stops when the stage ends.
- To play from an ability, use the `PlayNarrativeStage` ability task. Alternatively, declare
  `gameplay: Tag` on the stage; the GAS bridge plays it on the avatar when that event arrives.
