---
title: 从 DreamGameplayTask 迁移
description: 把蓝图任务迁成 .dna 任务。
---

`dn.migratedgt` 用反射读项目里 `DreamTask` 的蓝图子类的默认值(项目不必链接 DGT),写到源码目录的 `Migrated/DreamGameplayTask.dna`:

- 每个任务一个 `quest`:标题、描述作接下时的日志、High / Urgent 为 main、时限、子任务与自动接;
- 每个条件一个游戏写的计数 fact(组名 `<任务>_计数`)与一条目标;`Any` 合成一条 `or` 目标;
- 迁不过来的(蓝图图表里的逻辑、N 选 M 的完成方式)列在日志里,照着改写进 `on accept` / `on complete` / `on fail`。
