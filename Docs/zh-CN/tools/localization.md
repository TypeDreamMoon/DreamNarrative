<a id="localization"></a>

# 本地化

对话、选项、任务标题、日志与目标、资料库、角色名、章节标题和演出文字都可本地化。命名空间是 `DNA/<FileName>`；key 是：

| 文字 | key |
| --- | --- |
| 台词 | 它的 `#id`，否则 `Dialogue.<n>` |
| 选项 | 它的 `#id`，否则 `Dialogue.c<n>` |
| 任务 | `quest.<Quest>.title`、`quest.<Quest>.journal.<accepted\|going\|completed\|failed>`、`quest.<Quest>.objective<n>` 或 `quest.<Quest>.<id>` |
| 资料库 | `archive.<Archive>.title`、`archive.<Archive>.entry<n>` 或 `archive.<Archive>.<id>` |
| 角色 | `character.<Character>` |
| 章节 | `chapter.<Chapter>` |
| 演出文字 | `stage.<Stage>.text<n>` 或 `stage.<Stage>.<id>` |

没写 `display:` 的角色、没写 `title:` 的任务、资料库或章节显示的是名字本身，这个名字也照标题一样翻译。文字是格式串：插值按出现顺序写成 `{0}`、`{1}`，字面的花括号用 `` ` `` 转义。同一个文件里两条文字用了同一个 key 报 DNS6007。脚本资产在 Asset Directory 下，引擎采集照常能采到。

<a id="keys-that-hold-still"></a>

## 不会挪动的 key

没写 `#id` 的 key 按位置编号（`Dialogue.3`、`Dialogue.c2`、`objective2`、`entry0`、`text1`）。在前面插一条，后面每个 key 都挪到相邻那句上，跟着 key 走的译文、语音、存档记录一起错位。开始录音或翻译之前，关掉编辑器跑一次 `dnc.ps1 freeze`：它把每个现在的 ID 原样写进源码——台词和选项写 `#Dialogue.3`，目标写 `show: Text #objective2`，资料库写 `entry #entry0`，演出文字写 `text 「…」 #text1`。之后新加的文字取它所在对话、任务、资料库或演出里还没被 `#id` 占用的下一个编号，不会落到已冻结的 key 上；再跑一次 `dnc.ps1 freeze` 把它也定住。之后在项目设置里打开 `Require Line Ids`，没有 `#id` 的行报 DNS6006。ID 可以带点，例如 `#老周_初见.3`。

<a id="voices-in-other-languages"></a>

## 多语言配音

语音不进翻译表。每种语言的录音是引擎的本地化资产，放在 `Content/L10N/<culture>/` 下同样的路径；游戏用它替代源语言语音（编辑器和 PIE 不会）。参见 [语音](../runtime/index.md)。

<a id="notes-for-translators"></a>

## 给译者的备注

紧贴在台词、选项或任何其他文字上一行的 `///` 注释就是它的备注：

```dna
dialogue 老周_初见 for 老周
  /// First meeting: curt, not polite small talk
  老周(皱眉): 你就是新来的守夜人?
```

<a id="extraction-manifests"></a>

## 提取清单

每次编译一个文件（`dnc build`，或编辑器里保存源码时），它的文字都写到 `<Project>/Intermediate/DLoc/extract/DreamNarrative/<source path>.json`，给 DreamLoc 这类翻译工具读；插件本身不依赖它们。源码目录外的文件不写清单；内容没变不重写。

```json
{
  "version": 1,
  "provider": "DreamNarrative",
  "source": "DNarrative/Ch01/灯塔.dna",
  "namespace": "DNA/灯塔",
  "entries": [
    { "key": "老周_初见.1", "src": "你就是新来的守夜人?", "at": "老周_初见#老周_初见.1", "kind": "line",
      "note": "First meeting: curt, not polite small talk", "stable": false,
      "context": { "speaker": "老周", "speakerDisplay": "老周", "dialogue": "老周_初见", "tags": "皱眉" } }
  ]
}
```

- `at`：文字在哪，不写行号：`Dialogue#Id`、`quest.Quest#title`、`stage.Stage#text0`。
- `kind`：`line`、`choice`、`quest`、`archive`、`character`、`chapter` 或 `stage`。
- `stable`：按位置编号的 key 写 `false`，其余不写。这种 key 的原文变了可能是挪到了别的文字上，翻译工具可以借此提醒；`dnc freeze` 能把它定住。
- `renamedFrom`：对话、任务、角色、章节用 `was` 改名之前的 key。
- `context`：`speaker`、`speakerDisplay`、`dialogue`、`previous`（源码顺序上的上一句）、`tags`、`channel`、`for`（秒）、`voice`（找到的语音，或按约定该叫的资源名）、`quest`、`archive`、`stage`，以及 `args`：`{0}`、`{1}` 各代表什么，照源码原样。

同样的备注和上下文也作为文字的开发者备注交给引擎采集。

<a id="translating"></a>

## 翻译

DreamLoc 读这些清单：`dloc sync` 按源文件和语言把它们生成一个 `.dloc` 文件，和工程里别的译文放在一起，它的翻译源把译文交给游戏显示。旧版本 `dnc loc-export` / `loc-import` 的 CSV 译文用 `dloc import -In Translation.csv -Culture en` 一次导进去：导出之后原文改过的行算过期，已经不存在的 key 会列出来。不用 DreamLoc 时，脚本资产照样能用引擎的本地化面板采集。
