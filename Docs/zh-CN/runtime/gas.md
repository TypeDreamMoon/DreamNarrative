---
title: Gameplay Ability System
description: 双向的桥与能力任务。
---

```cpp
UDreamNarrativeGASBridge* Bridge = GetGameInstance()->GetSubsystem<UDreamNarrativeGASBridge>();
Bridge->WatchAbilitySystem(PlayerASC);                 // 也可以盯 Boss 的,收它的阶段事件
```

| 写法 | 方向 |
|---|---|
| `event 名 gameplay 标签` | GameplayEvent → 剧情事件,参数 `from magnitude` / `from instigator tags X` / `from target tags X` |
| `fact 名: bool extern gameplay 标签` | 标签计数 → fact |
| 任务 `gameplay: 标签` | 任务完成 → 给盯着的能力系统发 GameplayEvent |
| 演出 `gameplay: 标签` | GameplayEvent → 在化身上演这段,事件的 Target / Instigator 绑成 `target` / `instigator` |

能力里直接演:`PlayNarrativeStage` 能力任务(`UAbilityTask_PlayNarrativeStage`),演完 `OnFinished`,被打断 `OnInterrupted`;
能力先结束则演出随之停下、世界放回。
