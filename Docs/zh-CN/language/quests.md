---
title: 任务
description: 观察账本的状态机:目标、日志、奖励、子任务、超时。
---

```dna
quest 修灯
  title: 修好灯塔
  priority: main
  timeout: 20m
  available if 见过老周
  fail if 灯塔倒了
  journal
    accepted: 灯室缺三个零件。
    going: 还差 {3 - 找到的零件} 个。
    completed: 灯亮了。
  objective 找到的零件 >= 3
    show: 零件 {找到的零件}/3
  objective 带了油灯 optional
  on complete
    set 灯修好了 = true
    reward 物品 守夜人的钥匙
  subquest 送小雾回家 auto
  gameplay: Quest.Lighthouse.Done
```

- 状态:Inactive → Accepted → Going → Completed | Failed | TimedOut;失败、超时后可以重接。
- 每批写入之后检查 `fail if`、锁存达成的目标;必需目标齐了就完成。状态是账本里的隐藏 fact,存读档自然带上。
- `priority: hidden`:可接即自启(记忆链、隐藏结局);`side` 的进度在选章时保留。
- `gameplay:` 任务完成时 GAS 桥把这个 GameplayEvent 发给它盯着的能力系统。
