---
title: dnc 与测试
description: 命令行、测试档位、探索器、录制回放。
---

```powershell
Tools/dnc.ps1 build                  # 编译源码目录下的所有 .dna(编辑器关着)
Tools/dnc.ps1 lint                   # 只检查
Tools/dnc.ps1 dump -File 灯塔.dna      # 看语法树
Tools/dnc.ps1 test -Preset Quick     # 逻辑层、规格套件、冒烟;也有 Explore、Smoke、All
```

- 测试在插件自带的测试宿主工程里跑,编辑器无头启动;报告(通过 / 失败 / 最慢的几个)写到 `Saved/DreamNarrativeTestReports`。
- **规格套件**:每个 `test` 块是一条自动化测试;覆盖矩阵检查运行时的每条路径都有块走到。
- **探索器**:`explore` 块把所有走法穷举,报软锁、`always` 的反例、不可达的内容。
- **冒烟**:真的 GameInstance、真的世界:演出在玩家控制器上演,打断后世界逐项读回比较。
