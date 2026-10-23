# Tests and exploration

```dna
test 零件齐了灯就亮
  given
    chapter = 风暴
  accept 修灯
  set 找到的零件 = 3
  expect 灯修好了
  expect chapter == 天亮
```

- `test` blocks live alongside the story. Steps include `talk Character`, `emit Event`,
  `expect Condition`, `wait 10s`, `newgame` and `select Chapter`; `strict` requires exact line-by-line matching.
- `explore` blocks enumerate possible paths and check `always`, `eventually` and `softlock` assertions.
  Counterexamples include a reproducible test block.
- `dn.record start` / `stop` records a manual play session as a test block.
- Run `Tools/dnc.ps1 test -Preset Quick|Explore|Smoke|All` using the plugin's test host project.
