---
title: 频道与事件
description: 边打边讲:几段对话并行,事件带参数。
---

```dna
channel 电台
  input: free          // 不锁输入
  advance: auto        // 自己往下说
  busy: queue          // 忙时:queue 排队 / interrupt 打断 / drop 丢掉
  pauses: 闲聊          // 它说话时让闲聊停下
  stage: pause         // 演出期间停下

event 风暴逼近
  等级: int range 1..5 = 1

dialogue 电台_风暴 on 风暴逼近 channel 电台 cooldown 30s
  无线电: 沿海广播,风暴等级 {风暴逼近.等级}。
```

- 主频道 `main` 一直在:锁输入、玩家推进、新来的打断旧的。声明的频道和它并行。
- `stops: 频道` 一开口就打断别的频道;写了 `resume` 的对话被打断后,同频道的排回队首,被别的频道打断的等那个频道说完,都从被打断的那一行接着说。
- 游戏 `RaiseEvent` 一个事件,每个频道挑最合适的一段 `on` 对话开;事件参数只在它打开的对话里读(`事件.参数`)。
- `event 名 gameplay 标签`:能力系统收到这个 GameplayEvent 就发这个事件,参数 `from magnitude | instigator tags X | target tags X`。
