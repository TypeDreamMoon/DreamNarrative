<a id="editor"></a>

# 编辑器

- 工具菜单里的 **Narrative Story** 显示章节、任务、对话、事件、演出及其连线。双击会按编辑器偏好里的链接模板（默认是 VS Code）打开源文件。右键可以编辑条件、连接节点、创建或删除对话，或 **Generate staging**：为每个说话者/听者对生成相对台词的镜头；再次调用可重新生成。
- **Narrative Stage Timeline** 显示演出的各条轨道。拖动条目、范围端点或过渡手柄；松开后只更新对应的那一行源文件。切换时间基准或拖动预览姿态；关卡视口可以跟随镜头。
- **Narrative Debugger** 显示 PIE 中的章节、检查点、fact 与作用域、各频道的发言以及 fact 的写入者。它可以跳转到某个章节、选择章节，或开始新周目。
- 编辑操作有自己的撤销/重做，保存前不会改动文件。如果文件在外部被修改，面板会询问是丢弃面板中的编辑并重新加载，还是用面板里的版本覆盖；取消则两者都不变。
- `dn.importsequence /Game/路径/LS_过场` 将 Level Sequence 的镜头切换导入为演出；`dn.migratedgt` 把 DreamGameplayTask 的任务迁移到 `.dna`。
