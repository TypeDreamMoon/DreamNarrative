# Channels and events

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

The built-in `main` channel always exists: it locks input and waits for player advancement.
Other channels run alongside it. `pauses: Channel` suspends another channel while speaking;
`stops:` interrupts it when speech begins.
When the game calls `RaiseEvent`, each channel selects the most suitable `on` dialogue for that event.
