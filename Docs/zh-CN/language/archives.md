<a id="archives"></a>

# 档案

```dna
archive 守夜日志 profile
  title: 守夜日志
  entry
    第一夜:灯坏了。
  entry if 灯修好了
    灯亮了。
```

条目在其条件首次变为真时解锁，之后保持解锁状态。条目的文本在 `archive.<Archive>.entry<n>` 下翻译，按位置从 0 开始编号；`entry [if Condition] #id` 则将其命名为 `archive.<Archive>.<id>`，而 `dnc freeze` 会写入这些编号。
