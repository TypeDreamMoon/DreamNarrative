<a id="tests-and-exploration"></a>

# test 与 explore

```dna
test 零件齐了灯就亮
  given
    chapter = 风暴
  accept 修灯
  set 找到的零件 = 3
  expect 灯修好了
  expect chapter == 天亮
```

- `test` 块与故事放在一起。步骤包括 `talk Character`、`emit Event`、`expect Condition`、`wait 10s`、`newgame` 和 `select Chapter`；`strict` 要求逐行精确匹配。
- `explore` 块枚举所有可能的路径并检查 `always`、`eventually` 和 `softlock` 断言。反例包含一个可复现的 test 块。
- `dn.record start` / `stop` 把一次手动游玩过程录制为 test 块。
- 使用插件的测试宿主项目运行 `Tools/dnc.ps1 test -Preset Quick|Explore|Smoke|All`。
