# Characters and facts

```dna
character 守夜人 player
character 老周
  display: 老周

fact 见过老周: bool
fact 找到的零件: int extern range 0..3     // extern: the game writes; the story reads
fact 小雾的信任: int range 0..3
fact 已见结局 profile                       // A group; profile persists across saves and playthroughs
  A: bool
  B: bool
```

Facts have four types: `bool`, `int`, `float` and `tag`. A declaration such as `tag in (甲, 乙)` defines
a finite set. Story-owned facts can only be written by `.dna`; writes from game code are rejected.
Declare values supplied by the game as `extern`.

The default scope is the save slot. `session` is not persisted; `profile` persists across save slots;
`progress` retains its current value when selecting a chapter.
