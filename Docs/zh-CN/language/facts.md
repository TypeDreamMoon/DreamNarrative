<a id="characters-and-facts"></a>

# 角色与 fact

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

fact 有四种类型：`bool`、`int`、`float` 和 `tag`。诸如 `tag in (甲, 乙)` 这样的声明定义了一个有限集合。归故事所有的 fact 只能由 `.dna` 写入；来自游戏代码的写入会被拒绝。将游戏提供的值声明为 `extern`。

默认作用域是存档槽。`session` 不会被持久化；`profile` 会跨存档槽持久化；`progress` 在选择章节时会保留其当前值。
