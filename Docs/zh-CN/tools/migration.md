---
title: 从 DreamGameplayTask 迁移
description: 把蓝图任务定义转换为 .dna 任务。
---

`dn.migratedgt` 反射项目的 `DreamTask` 蓝图子类的默认值，无需依赖 DGT 链接。输出为 `<SourceDirectory>/Migrated/DreamGameplayTask.dna`：

- 每个任务对应一个 `quest`：标题、描述作为接受时的日志、High/Urgent 使用 main 优先级、时限、子任务与自动接受。
- 每个条件在 `<Quest>_计数` 中成为一个由游戏写入的计数 fact，并对应一条目标。`Any` 会把多个条件合并成一条 `or` 目标。
- 不受支持的行为（包括蓝图图表逻辑与 N 选 M 的完成方式）会列在迁移日志中，供手动改写进 `on accept`、`on complete` 或 `on fail` 块。
