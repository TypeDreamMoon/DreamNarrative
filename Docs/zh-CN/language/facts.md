---
title: 角色与 fact
description: 一本账:四种类型、谁能写、存在哪。
---

```dna
character 守夜人 player
character 老周
  display: 老周

fact 见过老周: bool
fact 找到的零件: int extern range 0..3     // extern:游戏写,剧情读
fact 心情: tag in (平静, 不安) = 平静
fact 已见结局 profile                       // 一组;成员是 已见结局.A
  A: bool
  B: bool
```

- 类型只有 `bool`、`int`、`float`、`tag`;`range` 约束数值,`tag in (…)` 是有限集合。
- **所有权**:剧情自己的 fact 只能由 `.dna` 写;游戏代码要喂的写 `extern`。
- **作用域**:默认跟存档槽;`session` 不存盘;`profile` 跨存档、跨周目;`progress` 选章时保留。
- `gameplay 标签`:extern fact 跟着能力系统上这个标签的计数(GAS 桥)。
