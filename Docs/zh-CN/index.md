---
title: 概览
description: 用文本编写剧情、任务、章节、对话与演出；编辑器只是该源码的投影。
---

DreamNarrative 是 Unreal Engine 5.8 的叙事插件。剧情、任务、章节、对话与演出都写在 `.dna` 文本里，编译器把它们变成资产，运行时照着跑；故事视图与演出时间轴里的每次编辑都写回源码的那一行。

底下是一本 **fact 账本**。对话读写它，任务观察它，章节以它为门，存档、周目与选章都作用于它的作用域。

文本作为唯一真相来源，使整个故事可以被**静态检查**，包括死链、未声明或未写入的 fact，以及可达性。**测试**与故事写在一起，位于 `test` 块中。**探索器**枚举路径，以找出软锁和其他反例。

- [安装插件](getting-started/install.md)
- [编写第一个故事](getting-started/first-story.md)
- [语言](language/index.md)
- [演出语言](stage/index.md)
- [接入游戏](runtime/index.md)
- [诊断](diagnostics/index.md)
