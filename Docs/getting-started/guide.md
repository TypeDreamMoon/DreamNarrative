# DreamNarrative guide

Write stories, quests, chapters, dialogue and stages in `.dna` text. The compiler turns them into assets,
the runtime executes them, and the editor presents a projection of the text.
This guide covers installation, authoring a story, game integration, testing and staging.
See the [architecture decisions](../contributing/architecture.md) for the reasoning, the
[diagnostic reference](../diagnostics/reference.md) for every error code, and
[Lighthouse](../../Examples/Lighthouse/灯塔.dna) for a complete original example. Chinese identifiers and dialogue in the
examples are intentional: the language supports Unicode names and multilingual narrative text.

## Read the guide

1. [Install the plugin](install.md).
2. [Write your first story](first-story.md), then use the [language reference](../language/index.md).
3. [Author a stage](../stage/index.md) and use the [timeline and generators](../stage/editing.md).
4. [Integrate the runtime and voice](../runtime/index.md), with the optional [GAS bridge](../runtime/gas.md).
5. [Write tests and explore paths](../language/tests.md); run them with [dnc](../tools/dnc.md).
6. Use the [editor](../tools/editor.md), [localization tools](../tools/localization.md) and [VS Code extension](../tools/vscode.md).

Each topic has one maintained source page. This overview replaces the former combined guide without
duplicating those chapters.
