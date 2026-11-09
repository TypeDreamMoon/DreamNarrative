<a id="dreamnarrative-guide"></a>

# DreamNarrative 指南

在 `.dna` 文本中编写故事、任务、章节、对话和演出。编译器会将它们转换为资产，运行时执行它们，编辑器则呈现文本的投影。本指南涵盖安装、编写故事、游戏集成、测试与暂存。有关设计原理，请参阅[架构决策](../contributing/architecture.md)；有关每个错误代码，请参阅[诊断参考](../diagnostics/reference.md)；完整的原创示例请参阅 [Lighthouse](../../../Examples/Lighthouse/%E7%81%AF%E5%A1%94.dna)。示例中的中文标识符和对话是有意为之：该语言支持 Unicode 名称和多语言叙事文本。

<a id="read-the-guide"></a>

## 阅读指南

1. [安装插件](install.md)。
2. [编写你的第一个故事](first-story.md)，然后使用[语言参考](../language/index.md)。
3. [编排演出](../stage/index.md)并使用[时间轴与生成器](../stage/editing.md)。
4. [集成运行时与语音](../runtime/index.md)，以及可选的 [GAS 桥接](../runtime/gas.md)。
5. [编写测试并探索路径](../language/tests.md)；使用 [dnc](../tools/dnc.md) 运行它们。
6. 使用[编辑器](../tools/editor.md)、[本地化工具](../tools/localization.md)和 [VS Code 扩展](../tools/vscode.md)。

每个主题都有一个持续维护的源页面。本概览取代了此前合并的指南，且不会重复这些章节。
