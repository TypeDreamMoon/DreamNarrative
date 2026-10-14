---
title: 概览
description: 剧情、任务、章节、对话与演出写在文本里;编辑器只是文本的投影。
---

DreamNarrative 是 Unreal Engine 5.8 的叙事插件。剧情、任务、章节、对话与演出都写在 `.dna` 文本里,编译器把它们变成资产,
运行时照着跑;故事视图与演出时间轴里的每次编辑都写回源码的那一行。

底下只有一本**账**:每个状态都是一条 fact。对话读写它,任务观察它,章节以它为门,存档、周目、选章只是它的作用域。

文本是唯一真相,换来的是图形工具给不了的东西:整个故事可以**静态检查**(死链、不可达分支、没人写的 fact、到不了的章节都是编译错误),
可以**写测试**(`test` 块和故事写在一起),可以**穷举**(探索器把每条走法走一遍找软锁)。

- [装上](getting-started/install.md)
  - [第一个故事](getting-started/first-story.md)
  - [语言](language/index.md)
  - [演出](stage/index.md)
  - [接进游戏](runtime/index.md)
  - [诊断码](diagnostics/index.md)
