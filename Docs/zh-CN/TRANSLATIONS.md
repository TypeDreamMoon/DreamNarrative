# DreamNarrative 简体中文文档

> **首次迁移快照**：以下完整保留首次迁移时的导航、已迁入与缺译清单，不代表当前翻译状态。最新状态以 `python -B Docs/translation-status.py` 或文档站仓库的翻译状态 Issue 为准。本文件是中文维护记录，没有对应的英文源文件，不建立翻译记录，也不作为 AI 翻译目标。

[English](../README.md) · 简体中文

这些文档从原文档站迁入，目前均为**待核对的旧译文**，尚未确认与当前英文一致。英文仍是技术内容的原文；中文修订由插件仓库中的翻译 PR 审阅，合并后再同步到网站。

网站独有页面和 `docs-sync:notes` 补充保留在网站，不属于这里的翻译。

## 已迁入，待核对

- [更新记录](changelog/index.md)
- [DNS1xxx · 词法](diagnostics/dns1xxx.md)
- [DNS2xxx · 语法](diagnostics/dns2xxx.md)
- [DNS3xxx · 名字与引用](diagnostics/dns3xxx.md)
- [DNS4xxx · 表达式与类型](diagnostics/dns4xxx.md)
- [DNS5xxx · 图与可达性](diagnostics/dns5xxx.md)
- [DNS7xxx · 演出](diagnostics/dns7xxx.md)
- [DNS8xxx · 生成与写回](diagnostics/dns8xxx.md)
- [DNS9xxx · 工具链](diagnostics/dns9xxx.md)
- [诊断码](diagnostics/index.md)
- [第一个故事](getting-started/first-story.md)
- [装上](getting-started/install.md)
- [概览](index.md)
- [资料库](language/archives.md)
- [频道与事件](language/channels.md)
- [章节与周目](language/chapters.md)
- [对话](language/dialogue.md)
- [表达式](language/expressions.md)
- [角色与 fact](language/facts.md)
- [语言](language/index.md)
- [任务](language/quests.md)
- [test 与 explore](language/tests.md)
- [Gameplay Ability System](runtime/gas.md)
- [接进游戏](runtime/index.md)
- [时间轴、生成与导入](stage/editing.md)
- [演出语言](stage/index.md)
- [导演与恢复](stage/runtime.md)
- [dnc 与测试](tools/dnc.md)
- [编辑器](tools/editor.md)
- [本地化](tools/localization.md)
- [从 DreamGameplayTask 迁移](tools/migration.md)
- [VS Code](tools/vscode.md)

## 尚未翻译

以下链接指向英文原文；不会把英文副本标为中文译文。

- [contributing/architecture.md](../contributing/architecture.md)
- [diagnostics/reference.md](../diagnostics/reference.md)
- [getting-started/guide.md](../getting-started/guide.md)

## 翻译记录

`.translations/` 按英文源文件保存迁入记录。`sourceHash: null` 表示尚未核对；`importedSourceHash` 只记录迁入时看到的英文版本，不表示译文已经匹配该版本。这个导航页不是原文翻译，不建立伪造的审阅记录。

在插件根目录运行 `python -B Docs/translation-status.py` 可只读查看翻译状态；`--all` 列出全部原文，`--json` 输出结构化结果。无需文档站仓库，打包后没有 Git 的插件目录也可以运行。
