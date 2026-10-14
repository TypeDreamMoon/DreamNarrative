---
title: test 与 explore
description: 和故事写在一起的测试,与穷举走法的探索器。
---

```dna
test 零件齐了灯就亮
  given
    chapter = 风暴
    见过老周 = true
  accept 修灯
  set 找到的零件 = 3
  expect 灯修好了
  expect chapter == 天亮
  archive 守夜日志 = 2

test 风暴广播
  emit 风暴逼近(等级 = 4)
    channel 电台 strict
      无线电: 沿海广播,风暴等级 4。

explore 全路径
  start 老周_初见
  depth 40
  eventually 灯修好了
  softlock 修灯
```

- 步骤:`talk 角色`、`start 对话`、`emit 事件 [hold]`、`set`、`given`、`expect`、`wait`、`jump`、`save`、`newgame`、
  `select 章节`、`accept / complete / fail 任务`、`journal 任务: 文本`、`archive 资料 = n`、`rolls`。
- 期望:说话人与台词逐行写;`absent` 不该出现;`- 选项` 选它;`strict` 要求逐行对上。
- `explore`:探索器把所有选择与随机两头走一遍,检查 `always` / `eventually` / `softlock`;反例给出最近的一条复现 test 块。
- 录制:`dn.record start` / `stop` 把一次手玩录成 test 块,回放红时给出逐行差异。
