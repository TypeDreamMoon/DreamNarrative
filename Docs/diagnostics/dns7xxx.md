# DNS7xxx — Stage

## DNS7001

A camera cut is not covered by a mask.

**Explanation:** `cut -> Shot` instantly changes view. At that instant a mask entry must be at full
strength: `distort`, `exposure` and `streaks` peak at the midpoint of their range; `fade black / white
Duration` peaks after fading in; `cut black` and `comic` stay at full strength throughout. Move the cut
to the peak, add a mask such as `1.9s..2.1s distort strength 0.8` around a 2.0s cut, or use a visible
transition such as `blend -> Shot Duration` or `whip -> Shot`. An intentional visible hard cut can use
`cut -> Shot hard`, which is exempt.

## DNS7002

An entry lies beyond the stage duration.

**Explanation:** In an absolute stage, both endpoints must be within `duration`; in a `base: line`
stage they must be within 100%. Later entries never execute. Correct the duration or timestamp.
An entry's own transition, such as the 0.4s in `blend back 0.4s`, may extend beyond the stage end;
the stage waits for it before releasing control.

## DNS7003

A lasting world change has no declared restoration.

**Explanation:** A persistent change needs a later inverse on the track or in `restore`: acquired
camera control needs `blend back`; `hide hud` needs `show hud`; `text` needs `clear`; `ramp dark` needs
`light restore`; `key Character on` needs `key Character off`. The director always provides emergency
restoration on completion or interruption, but that snaps back immediately. Declare a deliberate
ending on the track or in `restore`, or use a range that releases its state when the range ends.

## DNS7004

Time units do not match the stage's time base.

**Explanation:** Absolute stages use seconds for entries and transitions, such as `0.3s`. A
`base: line` stage derives its length from the attached dialogue line and uses percentages such as
`40%`; it cannot declare `duration`. Use the correct units. For a fixed-length stage, remove
`base: line`, add `duration`, and invoke it with `play`.

## DNS7005

A range ends before it starts.

**Explanation:** In `Start..End`, End must not precede Start. The two numbers were probably reversed.

## DNS7006

A track does not support this verb, or its arguments are invalid.

**Explanation:** Each track accepts its own verbs:

| Track | Verbs |
| --- | --- |
| camera | `blend from game / back / ->`, `cut ->`, `whip ->`, `push`, `shake` |
| light | `ramp dark`, `restore`, `key` |
| mask | `distort`, `exposure`, `streaks`, `comic`, `cut black`, `fade black / white` |
| vfx | `spawn`, `stop` |
| ui | `hide hud`, `show hud`, `text`, `clear` |
| anim | `montage` |
| game | `input lock`, `hitstop`, `time`, `damage window` |
| sequence | `play` |
| audio | `sound`, `music` |
| blocking | `move`, `face`, `look`; `back` only in `restore` |

The message shows the expected syntax. This code also covers invalid stage properties (only `base`
and `gameplay`), shots or marks missing `local (x, y, z)` (optionally followed by
`rot (pitch, yaw, roll)`), and invalid `restore` entries. Restoration accepts only operations that
release world state: `camera blend back`, `light restore`, `light key Character off`, `ui show hud`,
`ui clear`, `vfx stop` and `blocking back`.

## DNS7007

A verb requires a point in time but has a range, or vice versa.

**Explanation:** Camera transitions and cuts, `ramp dark`, `restore`, `show hud`, `clear`, `stop`,
`montage`, `sequence play` and `shake` happen at a point and cannot use `Start..End`. `distort`,
`exposure` and `streaks` need a range to rise to their midpoint peak and fall again. Match the timing
form to the verb. `music` with `keep` also requires a point, because it survives stage completion;
for music lasting only within a range, remove `keep`.

## DNS7008

A camera target is not a shot declared by this stage.

**Explanation:** Targets of `cut ->`, `blend ->` and `whip ->` must be in this stage's `shots` block:
`Name local (x, y, z) [of Character] [look Character] [fov Degrees]`. Check spelling and ownership;
shots declared in another stage are not visible here.

## DNS7009

An absolute-time stage has no duration.

**Explanation:** A stage using seconds requires `duration` so the director knows when to finish, when
dialogue can resume and when the watchdog should intervene. Add a declaration such as `duration 6.0s`.
For a line-relative stage, use `base: line` and attach it with `@StageName` instead.

## DNS7010

`play` targets a line-relative stage.

**Explanation:** A stage with `base: line` has no independent duration; its attached dialogue line
provides time. Attach it using `@StageName`, or convert it to absolute time by removing `base: line`,
declaring `duration`, and expressing times in seconds.
