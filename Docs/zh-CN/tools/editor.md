---
title: 编辑器
description: 故事视图、调试器、时间轴。
---

- **Narrative Story**:章节、任务、对话、事件、演出与它们之间的边(推进、跳转、接任务、发事件、打开对话、播演出);
  双击跳到源码(编辑器偏好里设链接模板,默认 `vscode://file/{file}:{line}`);右键改条件、连线、新建 / 删除对话、生成演出。
- **Narrative Debugger**:PIE 里的章节、检查点、每个 fact 与作用域、各频道在说什么(可单步、可选)、谁写了哪个 fact;能跳章、选章、开新周目。
- **Narrative Stage Timeline**:见 [时间轴、生成与导入](../stage/editing.md)。
- 所有编辑都有自己的撤销 / 重做,保存前不碰文件;保存保留文件原来的 BOM 与换行。
