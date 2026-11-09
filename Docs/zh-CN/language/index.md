---
title: 语言
description: 一个文件，混写所有顶层声明。
---

- 一个文件里可以混写 `file`、`character`、`fact`、`chapter`、`quest`、`dialogue`、`stage`、`channel`、`event`、`archive`、`test` 和 `explore` 块。
- **名字全项目唯一**：角色、fact、对话、任务、章节、演出、频道、事件、资料共用一个命名空间。
- 缩进表示从属；`//` 是注释；中文标识符与全角标点（`：`、`，`、`（）`）都认。
- 编辑器是文本的投影：故事视图、时间轴里的编辑写回源码，其余行逐字节不变。

从[角色与 fact](facts.md)、[对话](dialogue.md)、[频道与事件](channels.md)、[任务](quests.md)、[章节](chapters.md)、[资料](archives.md)和[表达式](expressions.md)开始。每个错误都有一个 `DNS` 码，记录在[诊断参考](../diagnostics/reference.md)中。
