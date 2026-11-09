---
title: dnc 与测试
description: 命令行工具、测试预设、探索与录制/回放。
---

```powershell
Tools/dnc.ps1 build                  # Compile all .dna files in Source Directory with the editor closed
Tools/dnc.ps1 lint                   # Check without writing assets
Tools/dnc.ps1 dump -File 灯塔.dna      # Inspect the syntax tree
Tools/dnc.ps1 test -Preset Quick     # Logic, specification and smoke; also Explore, Smoke and All
```

- 测试在插件的宿主工程中以无头编辑器运行。通过、失败与最慢测试的报告写入 `Saved/DreamNarrativeTestReports`。
- **规格套件**：每个 `test` 块是一条自动化测试；覆盖矩阵验证这些块覆盖了运行时路径。
- **探索器**：`explore` 块枚举所有路径，并报告软锁、`always` 反例与不可达内容。
- **冒烟测试**：使用真实的 GameInstance 和世界，在玩家控制器上播放演出，将其打断，再读回世界状态进行比较。

编写与录制测试请参见 [test 与 explore 块](../language/tests.md)。
