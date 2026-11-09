<a id="dialogue"></a>

# 对话

```dna
dialogue 老周_初见 for 老周 if not 见过老周
  老周: 你就是新来的守夜人?
  老周(皱眉): 灯坏了三天了。
  - 我去修。
      老周: 灯室里缺三个零件。
      accept 修灯
  - 先歇一会儿。 once
  set 见过老周 = true
```

- `for Character` 声明一个角色入口。若有多条匹配，`priority n` 最高者胜出；默认优先级为条件项的数量。`once` 和 `cooldown 30s` 控制入口能否再次打开。
- 行尾修饰包括 `if Condition`、`once`、`for 3s`（自动推进的时长）、`@Stage`（附加到该行的演出）以及 `#id`（稳定的行 ID）。
- 紧贴在台词或选项上一行的 `///` 注释是给译者的备注。它们会进入该文本的[本地化清单](../tools/localization.md) 和引擎的采集；`//` 注释则不会。

  ```dna
    /// First meeting: curt, not polite small talk
    老周(皱眉): 你就是新来的守夜人?
  ```
- 命令包括 `set`、`accept / complete / fail Quest`、`advance Chapter`、`play Stage [async]`、`emit Event(Parameter = Value)`、`wait 2s` 和 `-> Dialogue`。
- 使用 `{Expression}` 进行插值。条件可以使用 `visited(Dialogue)`、`random(1, 6)`、`chance(0.3)`、`chapter == Chapter` 和 `Quest.active`。
