---
title: Gameplay Ability System
description: The bidirectional bridge and stage ability task.
---

```cpp
UDreamNarrativeGASBridge* Bridge = GetGameInstance()->GetSubsystem<UDreamNarrativeGASBridge>();
Bridge->WatchAbilitySystem(PlayerASC); // You can also watch a boss ASC for its phase events
```

| Declaration | Direction |
| --- | --- |
| `event Name gameplay Tag` | GameplayEvent → narrative event; parameters use `from magnitude`, `from instigator tags X` or `from target tags X` |
| `fact Name: bool extern gameplay Tag` | Tag presence/count → fact |
| Quest `gameplay: Tag` | Quest completion → GameplayEvent on watched ability systems |
| Stage `gameplay: Tag` | GameplayEvent → play on the avatar; Target/Instigator bind as `target`/`instigator` |

Use the `PlayNarrativeStage` ability task (`UAbilityTask_PlayNarrativeStage`) to play directly from an
ability. It emits `OnFinished` on completion and `OnInterrupted` on interruption. If the ability ends
first, the stage stops and the world is restored.
