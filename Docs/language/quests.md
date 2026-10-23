# Quests

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

Quests observe the ledger. After each batch of writes, they check `fail if`, latch satisfied objectives,
and complete when all required objectives are latched. Their state lives in the ledger, so it is
included in save and restore automatically.
Quests with `priority: hidden` start as soon as they become available, which suits clue chains and
hidden endings. `gameplay: Tag` makes the GAS bridge emit that GameplayEvent on completion.
