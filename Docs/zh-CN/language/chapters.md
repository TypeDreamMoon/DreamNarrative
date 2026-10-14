---
title: 章节与周目
description: 章节机、检查点、选章、新周目。
---

```dna
chapter 上岛
chapter 风暴 after 上岛
  implied
    见过老周 = true          // 传送 / 选章到这里时补上的 fact
  advance -> 天亮 if 灯修好了
chapter 天亮 after 风暴
```

- 当前章节本身就是账本里的一个值:`chapter == 风暴` 可以写进任何条件。
- 第一次进一章留**检查点**;`SelectChapter` 回到检查点,保留 `progress` 的 fact 与支线任务。
- `StartNewPlaythrough` 清掉存档槽、保留 `profile`,隐藏任务重新自启,进第一章。
- 调试:`dn.jump 章节` 沿 `after` 链补上 implied fact 再跳过去。
