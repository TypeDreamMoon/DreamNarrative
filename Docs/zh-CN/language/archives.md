---
title: 资料库
description: 解锁了就一直在的文本。
---

```dna
archive 守夜日志 profile
  title: 守夜日志
  entry
    第一夜:灯坏了。
  entry if 灯修好了
    灯亮了。海上有一条船跟着光回了港。
```

条件第一次成立时解锁,解锁了就一直在;`profile` 的资料跨存档、跨周目(武器故事、图鉴)。
条目的文字翻译时的 key 是 `archive.<资料>.entry<n>`,按位置从 0 编号;写 `entry [if 条件] #id` 就改叫 `archive.<资料>.<id>`,`dnc freeze` 会把编号写进源码。
