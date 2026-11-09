<a id="channels-and-events"></a>

# 频道与事件

```dna
channel 电台
  input: free          // Keep player input available
  advance: auto        // Advance automatically
  busy: queue          // Alternatives: interrupt the current dialogue, or drop the new one
  stage: pause         // Pause during a stage

event 风暴逼近
  等级: int range 1..5 = 1

dialogue 电台_风暴 on 风暴逼近 channel 电台 cooldown 30s
  无线电: 沿海广播,风暴等级 {风暴逼近.等级}。
```

内置的 `main` 频道始终存在：它会锁定输入并等待玩家推进。其他频道与其并行运行。`pauses: Channel` 会在说话时挂起另一个频道；`stops:` 会在语音开始时打断它。当游戏调用 `RaiseEvent` 时，每个频道都会为该事件选择最合适的 `on` 对话。
