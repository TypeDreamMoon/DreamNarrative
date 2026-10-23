# Dialogue

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

- `for Character` declares a character entry point. If several entries match, the one with the highest
  `priority n` wins; by default priority is the number of condition terms. `once` and `cooldown 30s`
  control whether an entry can open again.
- Line suffixes include `if Condition`, `once`, `for 3s` (the duration for automatic advancement),
  `@Stage` (a stage attached to the line), and `#id` (a stable line ID).
- Commands include `set`, `accept / complete / fail Quest`, `advance Chapter`, `play Stage [async]`,
  `emit Event(Parameter = Value)`, `wait 2s`, and `-> Dialogue`.
- Use `{Expression}` for interpolation. Conditions can use `visited(Dialogue)`, `random(1, 6)`,
  `chance(0.3)`, `chapter == Chapter`, and `Quest.active`.
