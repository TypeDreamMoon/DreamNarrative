<a id="quests"></a>

# 任务

```dna
quest 修灯
  title: 修好灯塔
  priority: main
  timeout: 20m
  journal
    accepted: 灯室缺三个零件。
    going: 还差 {3 - 找到的零件} 个。
    completed: 灯亮了。
  objective 找到的零件 >= 3
    show: 零件 {找到的零件}/3
  on complete
    set 灯修好了 = true
    reward 物品 守夜人的钥匙
  subquest 送小雾回家 auto
```

任务会观察账本。每批写入后，它们检查 `fail if`，锁定已满足的目标，并在所有必需目标都锁定后完成。它们的状态存于账本，因此会自动包含在保存和恢复中。带有 `priority: hidden` 的任务一经可用就会开始，这适合线索链和隐藏结局。`gameplay: Tag` 会让 GAS 桥在完成时发出该 GameplayEvent。目标的文本在 `quest.<Quest>.objective<n>` 下翻译，按位置从 1 开始编号；`show: Text #id` 转而将其命名为 `quest.<Quest>.<id>`，而 `dnc freeze` 会将编号写入。
