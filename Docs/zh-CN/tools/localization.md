---
title: 本地化
description: 可本地化文本的命名空间与 key、给译者的备注、提取清单。
---

台词、选项、任务标题 / 日志 / 目标、资料、角色名、章节标题、演出屏幕字都是可本地化文本。命名空间是 `DNA/<文件名>`,key 如下:

| 文字 | key |
| --- | --- |
| 台词 | `#id`,没写就是 `对话.<n>` |
| 选项 | `#id`,没写就是 `对话.c<n>` |
| 任务 | `quest.<任务>.title`、`quest.<任务>.journal.<accepted\|going\|completed\|failed>`、`quest.<任务>.objective<n>` 或 `quest.<任务>.<id>` |
| 资料 | `archive.<资料>.title`、`archive.<资料>.entry<n>` 或 `archive.<资料>.<id>` |
| 角色 | `character.<角色>` |
| 章节 | `chapter.<章节>` |
| 演出屏幕字 | `stage.<演出>.text<n>` 或 `stage.<演出>.<id>` |

没写 `display:` 的角色、没写 `title:` 的任务 / 资料 / 章节显示的是名字本身,这个名字也照标题一样翻。文字是格式串:插值按出现顺序写成 `{0}`、`{1}`,字面的花括号用 `` ` `` 转义。同一个文件里两条文字用了同一个 key 报 DNS6007。脚本资产在 Asset Directory 下,引擎采集照常能采到。

## 不会挪动的 key

没写 `#id` 的 key 按位置编号(`对话.3`、`对话.c2`、`objective2`、`entry0`、`text1`)。前面插一条,后面每个 key 都挪到相邻那句上,跟着 key 走的译文、语音、存档记录一起错位。开始录音或翻译之前,关掉编辑器跑一次 `dnc.ps1 freeze`:它把每个现在的 ID 原样写进源码——台词和选项写 `#对话.3`,目标写 `show: 文本 #objective2`,资料写 `entry #entry0`,屏幕字写 `text 「…」 #text1`。之后新加的文字取它所在对话 / 任务 / 资料 / 演出里还没被 `#id` 占用的下一个编号,不会落到已冻结的 key 上;再跑一次 `dnc.ps1 freeze` 把它也定住。之后在项目设置里打开 `Require Line Ids`,没有 `#id` 的行报 DNS6006。`#id` 可以带点(`#老周_初见.3`)。

## 多语言配音

语音不进翻译表。每种语言的录音是引擎的本地化资产:放在 `Content/L10N/<语言>/` 下同样的路径(`/Game/VO/老周/老周_初见_1` → `/Game/L10N/en/VO/老周/老周_初见_1`)。台词仍然引用原语言的资源,游戏(独立运行或打包后)按当前语言加载对应的那个;编辑器和 PIE 里放的是原语言。时长取自原语言资源,各语言长短不一时打开 `Wait For Voice Finished`。

## 给译者的备注

紧贴在台词、选项或别的文字上一行的 `///` 注释就是它的备注:

```dna
dialogue 老周_初见 for 老周
  /// 第一次见面,语气冷淡,别翻成客套话
  老周(皱眉): 你就是新来的守夜人?
```

## 提取清单

每次编译一个文件(`dnc build`,或编辑器里保存源码时),它的文字都写到 `<工程>/Intermediate/DLoc/extract/DreamNarrative/<源码路径>.json`,给 DreamLoc 这类翻译工具读;插件本身不依赖它们。源码目录外的文件不写清单;内容没变不重写。

```json
{
  "version": 1,
  "provider": "DreamNarrative",
  "source": "DNarrative/Ch01/灯塔.dna",
  "namespace": "DNA/灯塔",
  "entries": [
    { "key": "老周_初见.1", "src": "你就是新来的守夜人?", "at": "老周_初见#老周_初见.1", "kind": "line",
      "note": "第一次见面,语气冷淡,别翻成客套话", "stable": false,
      "context": { "speaker": "老周", "speakerDisplay": "老周", "dialogue": "老周_初见", "tags": "皱眉" } }
  ]
}
```

- `at`:文字在哪,不写行号:`对话#行ID`、`quest.任务#title`、`stage.演出#text0`。
- `kind`:`line`、`choice`、`quest`、`archive`、`character`、`chapter` 或 `stage`。
- `stable`:按位置编号的 key 写 `false`,其余不写。这种 key 的原文变了可能是挪到了别的句子上,翻译工具可以借此提醒;`dnc freeze` 能把它定住。
- `renamedFrom`:对话、任务、角色、章节用 `was` 改名之前的 key。
- `context`:`speaker`、`speakerDisplay`、`dialogue`、`previous`(源码顺序上的上一句)、`tags`、`channel`、`for`(秒)、`voice`(找到的语音,或按约定该叫的资源名)、`quest`、`archive`、`stage`,以及 `args`:`{0}`、`{1}` 各是什么,照源码原样。

同样的备注和上下文也作为开发者备注交给引擎采集。

## CSV 导出导入

`dnc.ps1 loc-export [-Out 路径] [-Culture en] [-Target Game]` 把每条可本地化文本导成带 BOM 的 UTF-8 CSV,列是 `Namespace,Key,Source,Translation`,后面是上下文:类别、说话人、对话、上一句、情绪、文件与行号。写了 `-Culture` 就把本地化目标里已有的译文填进 `Translation`。译好之后:

1. 在本地化面板里采集目标(默认 `Game`),Asset Directory 要在采集范围里;
2. 关着编辑器跑 `dnc.ps1 loc-import -File 译文.csv -Culture en`,译文写进 `Content/Localization/Game/en/Game.archive`。目标没采集到的 key、导出之后原文改过的行不导入,逐条列出;没填译文的跳过;
3. 回到面板「Compile Text」。

目标的母语从它的 `.locmeta` 读,还没编译过的目标用 `-NativeCulture` 指定。
