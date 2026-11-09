---
title: Gameplay Ability System
description: 双向桥与演出能力任务。
---

```cpp
UDreamNarrativeGASBridge* Bridge = GetGameInstance()->GetSubsystem<UDreamNarrativeGASBridge>();
Bridge->WatchAbilitySystem(PlayerASC); // You can also watch a boss ASC for its phase events
```

| 写法 | 方向 |
| --- | --- |
| `event Name gameplay Tag` | GameplayEvent → 剧情事件；参数使用 `from magnitude`、`from instigator tags X` 或 `from target tags X` |
| `fact Name: bool extern gameplay Tag` | 标签存在/计数 → fact |
| 任务 `gameplay: Tag` | 任务完成 → 向被监视的能力系统发送 GameplayEvent |
| 演出 `gameplay: Tag` | GameplayEvent → 在化身上播放演出；Target/Instigator 绑定为 `target`/`instigator` |

使用 `PlayNarrativeStage` 能力任务（`UAbilityTask_PlayNarrativeStage`）直接从能力中播放演出。它在完成时发出 `OnFinished`，在中断时发出 `OnInterrupted`。如果能力先结束，演出会停止，世界恢复原状。
