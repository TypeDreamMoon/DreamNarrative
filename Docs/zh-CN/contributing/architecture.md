<a id="architecture-and-decision-record"></a>

# 架构与决策记录

> 项目于 2026-08-31 启动。本文档记录架构决策及其理由。请将新决策追加到下面的历史记录中。草稿与里程碑计划保存在本地 `Plan/` 中，不纳入提交。历史性的提案会明确标注为历史提案；当前的编写说明见[指南](../getting-started/guide.md)。

<a id="1-one-ledger-three-views"></a>

## 1. 一个账本，三种视图

任务、剧情推进和对话是同一个 **账本** 的三种视图。这也是 Valve 的对话系统与《巫师》系列的 FactDB 中所见的 fact 系统思路。

- **对话** 从账本读取条件，并为选项后果写入账本。
- **任务** 观察一组条目、标记完成、广播事件并写入更多 fact。
- **剧情推进** 利用账本中的重大开关来选择世界当前所处的阶段。

因此 `DreamNarrativeCore` 包含账本、表达式和事件总线；Story 与 Stage 在其周围提供各自的行为。任务进度可以出现在对话条件中，选项也能推进章节，而无需连接彼此分离的状态存储。若构建三套相互隔离的系统，后续的跨系统条件将变得代价高昂。

**Core 是叙事推进的公共存储。** 存档数据与未来的网络层都只需处理一套推进模型。瞬时的播放状态仍归属于其运行器或导演。

<a id="difference-from-a-push-model"></a>

### 与推送模型的区别

条件是在 fact 变化时重新求值的表达式，而不是任务实例上需要由游戏代码显式更新的具名计数器。`DreamGameplayTask` 采用的是后一种推送模型。在这里，游戏代码写入账本时无需了解任何任务，进度集中管理，而非分散在各个任务实例中。

<a id="2-text-is-the-source-of-truth"></a>

## 2. 文本是权威来源

DSL 文件是权威来源；资产是编译器的输出；图形编辑器只是投影。在图中的编辑会写回源文件，同时保留注释与格式。

若将资产视为权威、文本仅作导入/导出，就需要指纹、手动编辑检测以及 Revert/Adopt 行为，这些成本在 DreamShader 中已经付出过。新系统没有需要迁移的历史资产，也不需要反编译器。Git 差异仍保持可读。

因此，保留格式的源码打印器是每个图形编辑器的关键基础。它需要解析 → 打印 → 解析的等价性测试。

<a id="3-language"></a>

## 3. 语言

- 表达式是为叙事工作设计的，独立于 DreamUILang。尽管如此，Story 与 Stage 在本仓库内必须使用同一套表达式语言。
- Unicode（包括 **中文标识符**）是一等需求：fact 与对话可以使用作者自己的语言。关键字仍保持英文，与 Dream 系列保持一致。
- 词汇表同时覆盖解谜叙事（章节、线索链、重复出现的对话）与 RPG（日志、目标、步骤、奖励、主线与支线任务）。
- 诊断使用 `DNS`；参见[参考](../diagnostics/reference.md)。解析器、校验器与生成器共用 FText 接收器 `FDnaDiagnosticSink`。DreamShader 中分离的 FText/FString 路径避免了修改 561 处既有错误位置；这一新实现没有此类兼容性成本。
- 2026-09-05 的语法决策将剧本式文本与类似 Yarn 的扁平节点模型结合起来。缩进提供结构，`Speaker:` 引入台词，连续的 `-` 行构成选项，`->` 表示跳转。逻辑体现在行首关键字与行尾修饰符中。一个 `dialogue` 块对应一个图节点；超过三层的嵌套会触发警告并建议拆分。这支持一对一的图投影、基于行的写回以及可读的脚本。
- 语法基于行。首个 token 决定每个逻辑行的类别；缩进是唯一的跨行结构。写回、VS Code 词法诊断和 Git 差异都能从中受益。
- 角色必须用 `character` 声明，从而支持未声明说话者的诊断。诸如“Attention: the door opened”这样的文本必须显式选择台词或旁白，而不是被静默地当作角色行处理。
- `dialogue Name for Character if Condition [priority n]` 选择特异度最高的匹配条目；默认优先级为条件项的数量。这样无需庞大的 `if` 树即可支持重复出现的 NPC 对话。
- 在字面文本之外，全角标点与其半角形式等价。全角缩进空格仍会被判为错误并提供快速修复；标点本身则是作者的正常输入。
- 编写时可以省略稳定 ID，在本地化或录音之前再将其冻结。最初的设计称之为 `dnc stamp` 和 `localization: on`；实际实现的接口是 `dnc freeze`，项目设置为 `Require Line Ids`。具名声明使用 `was OldName` 进行重定向。
- 语法草稿和两个最初的黄金示例仍保留在本地 `Plan/` 中。维护中的语言文档现在位于[语言](../language/index.md)。

<a id="4-static-validation-as-qa"></a>

## 4. 静态校验作为 QA

校验器是单人开发中不可或缺的 QA 工具。它可以检查死链、不可达分支、孤立证据、未声明的 fact、章节可达性、可完成的线索链以及重复的本地化键。

这需要知道谁能写入每个 fact。如果 C++ 悄悄修改了属于剧情的状态，可达性分析就会变得不可靠。集中声明以及通过 `extern` 显式标明游戏侧所有权，确立了校验所依赖的边界。

<a id="5-boundary-between-story-and-stage"></a>

## 5. Story 与 Stage 的边界

这种划分类似于《巫师》中逻辑与表现的分离：Story 一侧是 ScreenPlay、FlowGraph 和 FactDB；Stage 一侧是 Timeline。

稳定 ID 将两者连接起来。演出挂接到叙事行上，而不使任一功能拥有另一方，因此两个模块可以独立启用。

<a id="three-roles-of-a-stable-id"></a>

### 稳定 ID 的三重角色

对话行的 ID 既是存档键、本地化键，也是演出的挂接点。

- 名称即作为 ID，并通过重命名重定向来保留已有的存档。
- 行从一开始就使用稳定 ID 与 FText。资产编译通过 `AsLocalizable_Advanced` 注册可本地化文本。自定义 DSL 无法直接接入引擎的源码宏收集，因为其可识别的宏列表是固定的，但生成的资产可以使用常规的包收集。尽早确立这一点，可避免日后更换本地化工作流。

<a id="6-time-bases-and-voice-authority"></a>

## 6. 时间基准与语音权威

该设计沿用《巫师 3》中的时间轴缩放（Timeline Scaling）思路：同一句台词在一种语言中可能持续 12 秒，在另一种语言中则是 14 秒。表现事件应把开始时间和持续时间存为该句台词语音时长的百分比，而不是固定的秒数。

因此，台词从一开始就带有语音引用和时长字段，在没有语音时回退到按字符数估算。对话演出使用相对于台词的时间基准；由动画驱动的事件（例如技能）则使用绝对秒数。

<a id="7-separate-static-definitions-from-dynamic-state"></a>

## 7. 将静态定义与动态状态分离

资产定义任务和对话。玩家进度应放在通过 ID 关联的快照结构中。把运行时进度写回资产会引入第二个事实来源。

<a id="8-debug-jumps-shape-the-data-model"></a>

## 8. 调试跳转塑造数据模型

测试第八章不能要求先手动玩完前七章。每个章节都以 `implied` 事实声明其所需的进入状态，调试跳转可以提供这些事实。这应当从一开始就纳入 schema，而不是等内容依赖它之后再添加。

<a id="9-networking-is-reserved-in-the-design-not-implemented"></a>

## 9. 联网在设计上预留，但未实现

账本的所有权与复制边界按服务器权威模型来设计。第一版是单人游戏。预留这一形态的成本，远低于将来需要多人游戏时再替换账本。

<a id="10-the-plugin-has-no-ui"></a>

## 10. 插件不提供 UI

对话框和日志是接口与事件；项目自行渲染 UI。未来的 DreamGUI 适配器将是可选的，遵循 DreamMap 的可选依赖做法。临时的演示 UI 应放在测试项目中，而不是插件中。

<a id="11-engine-and-gameplay-modules"></a>

## 11. 引擎与玩法模块

运行时使用自己的小型求值器，而不依赖 StateTree。UE 5.8 的编辑 API 可能变化，而且等待输入的对话图和一组并发的任务观察者都难以放进状态树。未来的流程节点可能会暴露 StateTree 接入点。

| 模块 | 关系 |
| --- | --- |
| GameplayTags | 核心依赖；ID 可在编译时导出为标签 |
| EnhancedInput | 演出输入锁定和对话选项路由的直接依赖 |
| LevelSequence | 内嵌播放、单向相机导入，以及触发叙事的事件轨道；播放封装器为直接依赖 |
| Niagara | vfx 轨道的直接依赖；`.dfs` 引用是 DreamFX 的增强功能 |
| GAS | 独立适配器：标签/GameplayEvent → 事实/事件，任务完成 → GameplayEvent，技能 → 演出 |
| SaveGame | 快照结构与序列化接口；没有插件强制的自有存档文件 |
| Sequencer | 不是主要的演出表示形式；绑定重定向、REINST 和工具包生命周期假设，与按角色、可重定向、可中断的播放相冲突 |

账本目前在内部使用带点的 `FName` 键；导出为 GameplayTag 是单独的步骤。

<a id="12-relationship-to-dreamgameplaytask"></a>

## 12. 与 DreamGameplayTask 的关系

DreamGameplayTask 是面向蓝图的任务产品（UE 5.0–5.6，已发布 v3.1.0）。它与 DreamNarrative 分别采用推送模型和观察模型。二者不桥接任务状态，因为那会产生两套进度存储和两套存档模型。它在这里有三种角色：

1. **需求参考：** 条件模板、超时、子任务链、优先级和调试器都是既有的用户需求，它们为 RPG 词汇提供了依据。
2. **实现参考：** 状态机语义、句柄生命周期和调试器结构可以适配到新模型。
3. **迁移来源：** 蓝图产品保持独立；一个可选的导入器可将其任务定义转换为 DSL。

<a id="13-synchronous-callbacks-and-shared-state"></a>

## 13. 同步回调与共享状态

事件同步广播。监听者可以停止、替换或暂停当前正在广播的操作，也可以启动另一个操作。新增回调点必须遵循以下规则：

- **广播后重新检查代次。** 对话有执行代次；演出有播放 ID 和请求序列号。在通知前记录当前代次，并在通知后校验它、运行状态和暂停状态。被取代的栈帧必须停止触碰新状态。完成回调中新的 `Play` 胜出，被替换的外层请求返回 0。在 `OnChoiceMade` 中停止或替换对话会取消该选择。暂停会挂起该选择（包括其 once 记录），直到恢复。
- **每个非零播放 ID 或票据只投递一次成对的完成通知。** 如果启动同步完成，则按 ID 记住完成，并在启动返回后再投递。每次导演播放都在开始时报告一次 StageActivity、在结束时报告一次，发送给调用方指定的账本（演出子系统提供自己的账本），默认使用 GameInstance 账本。`stage: pause` 监听此事件。
- **暂停不会推进。** 暂停期间的演出完成或等待完成只记录就绪状态。执行在最后一个暂停原因清除后恢复。`StopAllDialogue` 移除对话引起的暂停；独立演出仍会暂停声明了 `stage: pause` 的通道。
- **恢复和调试跳转不算进度。** 它们不会执行章节推进、自动启动任务或解锁档案。在 `OnSnapshotRestored` 时，观察者建立基线之前，外部镜像会刷新；例如 GAS 桥接会从被监视的能力系统重新读取会话标签。
- **非事实的世界变化也会通知。** 章节变化和访问次数变化会在批次结束时合并为一次 `OnWorldStateChanged`，从而让任务、档案和章节规则重新求值，包括 `advance -> X if visited(Finale) >= 1` 这样的规则。

并发演出共享世界时间、日光/天光、HUD 可见性和玩家视角。每个世界的 `UDreamNarrativeStageArbiter` 拥有这些共享资源，而不是由每个导演各自记住原始值。时间与光照倍率相乘；任一占用者都会隐藏 HUD；相机所有权形成栈，最后占用者位于顶部，原始游戏视角位于其下。无论完成顺序如何，释放最后一个占用者都会恢复到演出前的基线。在变更之间，仲裁器不触碰状态，以便游戏也能修改它。没有 AHUD 的项目应使用仲裁器的 `OnHudVisibility` 来实现组合可见性；导演事件只报告该导演自己的占用。

<a id="decision-history"></a>

## 决策历史

<a id="2026-08-31--initial-architecture"></a>

### 2026-08-31 — 初始架构

第 1–12 节已获批准，并实现了 v0.1.0 骨架。

<a id="2026-09-05--syntax-and-scope"></a>

### 2026-09-05 — 语法与范围

新增了六项语言决策：采用扁平节点的剧本语法、基于行的语法、显式角色、入口选择、全角标点以及稳定 ID 标记。语法提案 v0.1 与 Ring/RPG 黄金示例已评审并存储在本地 `Plan/` 中。对 UE 5.8 的检查确认中文 GameplayTag 名称是有效的：`InvalidTagCharacters` 包含引号、逗号、换行和制表符；ini 读写使用不带 BOM 的 UTF-8。因此 fact 导出为标签时无需替代键方案。

扩展名专为 `.dna`；目标为 UE 5.8，不提供 5.6/5.7 兼容层。生成的资产内嵌原始源码；`dnc extract` 无需反编译器即可恢复它。表达式 v1 已冻结：逻辑、比较、算术（包括 `%`、`visited`、`random`、`chance`）、任务状态、`chapter` 以及标签 `in (…)`；不支持字符串或三元运算符。对话演出生成遵循演出时间轴编辑器。VS Code 仓库遵循可用的对话管线和稳定的语法。Ring 被推迟出范围。

<a id="2026-09-08--dialogue-pipeline"></a>

### 2026-09-08 — 对话管线

实现了词法分析器、CST 解析器、校验器、生成器、命令小程序、最小账本、表达式、对话运行器、`dn.` 控制台命令、`dnc` 以及语料测试。`visited(x)` 在离开时递增（跳转、完成或中止），因此节点在执行时看到的是先前的访问次数，与 Yarn 中一致。诊断使用单一的 FText 输出端。账本键为点分的 `FName` 值；GameplayTag 导出等待 tag-ini 生成。

<a id="2026-09-23--testing-strategy"></a>

### 2026-09-23 — 测试策略

同时使用 C++ 测试和 `.dna` 测试块，并提供一个覆盖语言能力的规范测试套件。层次包括逻辑、真实游戏冒烟、自动探索以及录制/回放。不要在编辑器保存时自动运行测试。使用专用宿主项目，并将插件作为 Git worktree 挂接；在实现任务之前先编写任务测试。超时的任务会报告 `.failed`，而其 `on timeout` 行为则单独处理。

<a id="2026-09-24--specification-tests-and-exploration"></a>

### 2026-09-24 — 规范测试与探索

测试块新增了步骤、期望行、选项、`absent` 以及强制随机 `rolls`。`Tests/Spec` 使用运行时 `IDnaTrace` 插桩来对照 `coverage.json`；缺少覆盖单元即失败。探索器对账本状态和动作建模：与角色交谈、启动仅游戏内对话，或为外部 fact 赋予关键值。它会探索随机选择的两种结果。仅与常量比较的单调数字会被截断在最远的比较边界。默认检查覆盖运行时错误、不终止/陷入的对话、保存/恢复和不可达内容。属性包括 `always`、`eventually` 和 `softlock`；失败会附带可粘贴的复现器（DNS51xx、DNS9201）。`IDnaRandomSource` 提供受控抽取，且运行器可以重新启动。

<a id="2026-09-24--recording-and-replay"></a>

### 2026-09-24 — 录制与回放

`dn.record start Name` 到 `dn.record stop` 会在内容根目录的 `Tests/Recorded/` 下写入一个测试。非默认初始状态会成为 `given`；交谈、启动对话、游戏 fact 写入和跳转会变成步骤；观察到的行使用宽松匹配；抽取会变成 `rolls`；最终变更的 fact 和章节会成为 `expect`。

`given` 可以重建访问次数、一次性选项和章节。章节最后设置，且初始设置不会触发推进规则。运行时观察者通知覆盖交谈/启动请求、选项、fact 写入器和跳转；录制不会占用唯一的追踪槽位。内容根目录的测试块注册为 `DreamNarrative.Project.*`。失败的录制会用相同输入重新运行到 `.actual`，并提供行差异供在替换原始文件之前审查。

<a id="2026-09-24--quests"></a>

### 2026-09-24 — 任务

编译器为每个任务提供两个隐藏 fact，使用源码中不可用的 `@` 名称：一个状态（`Inactive / Accepted / Going / Completed / Failed / TimedOut`）和一个锁定目标位集。因此保存、推进、探索和录制都不需要并行的任务存储；`was` 也会重命名这些隐藏 fact。

每个已提交批次之后，活动任务会检查 `fail if`，锁定已满足的目标，并在所有必需目标都锁定后完成。目标或日志使用的 fact 会触发 `OnQuestUpdated` 以刷新 UI。接受之后，目标 fact 的变化会将 Accepted 变为 Going。恢复会建立基线，本身不算一次变更。`.available` 表示未激活、失败或超时，同时 `available if` 已满足。无效的接受/完成请求会发出警告并返回 false。

隐藏任务在首次可用时自动开始；失败不会自动重试，从而避免无尽的开始/失败循环。计时器在快照中存储名称和剩余秒数，使用账本时钟，并在暂停时停止。超时只执行 `on timeout`。`on` 块会在一个批次中编译为同步指令序列，且不能 `wait`；奖励是数据广播，由游戏发放。校验新增了 DNS2011–2014。测试新增了游戏权威 `accept`、`complete`、`fail`、日志断言以及 `given quest Name = State [RemainingDuration]`。探索器可以将时间推进到下一次到期，并编码计时器活动而非每个剩余时间值；主线任务默认进行软锁检查。录制会捕获游戏侧的接受/完成。RPG 示例新增了一个回返任务提议：在第一次与市长对话时选择考虑后会出现。

<a id="2026-10-06--channels-and-events"></a>

### 2026-10-06 — 通道与事件

动作游戏中的无线电、报告和闲聊需要并行语音。每个频道运行一个对话；各频道独立运行。内置的 `main` 会锁定输入、等待玩家并替换其原有对话；只有它的暂停/停止目标可配置。已声明的频道默认采用自由输入和自动台词时长（`for`，或按字符数估算），并具有 `queue`、`interrupt` 或 `drop` 忙碌策略。`pauses` 从被挂起的行继续；`stops` 中断其他频道，若声明了 `resume`，这些频道的对话之后可以恢复。

来自游戏代码或 `emit` 的事件会携带声明的参数，但不会进入已保存的账本状态。每个频道会按条件、一次性、冷却和优先级选择匹配的 `on` 对话。只有由该事件打开的对话能看到其参数。一次性与冷却也约束角色条目；冷却使用账本时钟，不会保存，并在还原停止所有频道时清除。

测试新增了 `emit Event(Parameter = Value) [hold]` 和 `channel Name [strict]` 预期。通常每个步骤都会排空所有频道；`hold` 会将语音保留在第一行，供下一步中断或暂停。探索会以相关参数值触发被监听的事件，并在每个动作后排空频道。录制会在同一步骤下发出游戏侧事件及其结果对话，在暂停后重放时同一行只记录一次。控制台命令新增了 `dn.emit`、`dn.channels`，以及用于 `dn.next` / `dn.choose` 的频道选择。新增了 DNS2015–2020 和 DNS3010–3013。动作游戏黄金示例使用了 NieR:Automata 序章结构，并配以新编写的对话；角色原计划在发布前替换为原创角色，后续示例则遵循其他 AAA 结构。

<a id="2026-10-06--gas-event-bridge"></a>

### 2026-10-06 — GAS 事件桥接

映射位于 `.dna` 中，因此文本、校验和探索仍是权威来源。`event Name gameplay Tag` 会监听该 GameplayEvent 标签及其子标签。参数读取 `from magnitude`、`from instigator tags Tag` 或 `from target tags Tag`：bool 检查存在性；tag 提取下一段。`fact Name: bool extern gameplay Tag` 镜像存在性，而 int 镜像计数。使用 `from` 一词可避免 `<-` 与 `x <-1` 之间的歧义。

GameInstance 桥接会监视传递给 `WatchAbilitySystem` 的系统，并在后续脚本注册时重新绑定。未注册的项目标签会警告一次，且不会创建监听器。新增了 DNS2021、DNS2022 和 DNS4007。测试标签位于宿主的 `Config/Tags` 中，而不在随附插件中。

<a id="2026-10-06--scopes-checkpoints-playthroughs-and-archives"></a>

### 2026-10-06 — 作用域、检查点、周目与资料库

fact 位于存档槽、未保存的 `session`，或跨存档槽/周目的 `profile` 中。Profile 快照使用 `MakeProfileSnapshot` / `ApplyProfileSnapshot`；游戏负责持久化。选择章节时，`progress` 会保留在存档槽内。

首次通过推进、`advance`、游戏 SetChapter 或新周目进入章节时，会创建检查点。重新进入不会替换它；调试跳转和还原不会创建检查点。章节选择会还原该检查点，同时保留 progress fact 及其计时器。编译器会将支线任务的隐藏 fact 标记为 progress，使账本与任务概念保持独立。新周目会还原一个空存档槽，清除检查点但保留 profile，重新开始隐藏任务并进入第一章。快照版本 2 新增了检查点，同时保留对旧存档的支持。加载在某个 fact 变为 profile 之前写入的存档时，不会覆盖其 profile 值。

资料库条目通过隐藏的 `@archive:Name` fact 锁存，在需要时使用 profile 作用域；还原不计为解锁。测试新增了 `newgame`、`select Chapter`、`archive Name: Text` 和 `archive Name = Count`；保存/还原比较包含 profile 和检查点。探索包含 profile 状态和新周目，会在保留 profile 的同时回到其起点，但不模拟章节选择。录制会捕获两者，并在 `given` 中包含 profile。新增了 `dn.newgame`、`dn.select`、`dn.checkpoints`、`dn.archive`、DNS2023、DNS2024 和 DNS3014。

<a id="2026-10-07--source-printer-and-editor-projections"></a>

### 2026-10-07 — 源码打印器与编辑器投影

打印器会保留物理行及其换行符。未编辑的文件逐字节完全相同，这一点已在包括将每一行原样写回自身的整个语料库中验证。编辑过的行会保留缩进、尾随注释和注释前空白；新行则继承缩进和换行风格。表达式只会获得优先级所需的括号。

`FDnaEditSession` 会更改条件、添加 `->` 连接，并创建/删除对话。它会在每次操作后重新解析，并拒绝新引入的解析错误。撤销/重做使用自己的文本栈，而非 UE 事务；保存会保留 BOM 的存在状态。故事图使用编译器的程序，并在章节、任务、对话、事件、演出、资料库及其推进、跳转、接受、发出、事件入口和播放边上携带文件/行信息。

Narrative Story 中的双击会使用配置的链接模板（默认 `vscode://file/{file}:{line}`），并通过上下文菜单进行编辑。Narrative Debugger 会显示 PIE 进度、检查点、fact、作用域、频道和写入者，并提供单步/选择/跳转/选定/新周目控件。面板与 commandlet 共享源目录加载。

<a id="2026-10-07--stage-runtime"></a>

### 2026-10-07 — 演出运行时

演出会编译为 `UDnaStageSection`，其中包含镜头、按时间排序的条目和还原操作。时间使用秒数或所附加行的比例。`SampleDnaStage` 是一个纯演出/时间函数，由导演播放和时间轴预览共享。蒙太奇、VFX 和 Level Sequence 播放等一次性动作，是在时间越过它们时触发的提示。

Actor 组件 `UDreamNarrativeStageDirector` 会在首次更改前记录原始状态，并在完成、停止、替换、Pawn 丢失或解除占有，或由看门狗处理的世界销毁之后，按相反顺序还原被触及的状态。正常完成会先运行作者编写的还原过渡，然后释放剩余状态。仅控制本地玩家摄像机：远程控制器会将视图更改路由回客户端，并可能递归地重新进入这些调用。

Core 通过账本暴露演出宿主接口；Stage 实现它而不依赖 Story。阻塞式对话 `play` 会等待；行挂载的 `@` 在行离开时停止；任务 `play` 不等待。没有 Stage 时，通知仍会送达游戏。独立演出会暂停 `stage: pause` 频道；行挂载的演出则不会。没有控制器时，演出时长仍按账本时钟运行。DNS7001–7010 使用运行时包络、恢复、单位与时间基准在 mask 峰值处校验剪辑。

<a id="2026-10-07--stage-timeline"></a>

### 2026-10-07 — 演出时间轴

时间轴显示一个演出，每条轨道一行。拖动条目、范围端点与过渡手柄会在释放时提交一次源行编辑。秒使用百分之一秒且至少保留一位小数；分数使用千分之一。行的其余部分保持不变，在空间允许时保留动词列。秒 ↔ 行相对时间的转换使用提供的行长，并更新 `duration` 与 `base: line`。姿态与可选的视口摄像机预览共享运行时的采样和基于 actor 标签的镜头求值。

<a id="2026-10-07--gas-output-and-ability-task"></a>

### 2026-10-07 — GAS 输出与能力任务

任务的 `gameplay: Tag` 在完成时向所有被监视系统发出 GameplayEvent，但失败或超时不发出；其他时机请在 `on` 块中使用 `emit`。当该事件到达时，演出的映射会在被监视系统的 avatar 上播放它，将 Target 与 Instigator 绑定为 `target` 与 `instigator`。`UAbilityTask_PlayNarrativeStage` 报告 `OnFinished` 或 `OnInterrupted`；结束能力会停止演出并恢复世界。

<a id="2026-10-07--sequence-import-and-dialogue-staging"></a>

### 2026-10-07 — Sequence 导入与对话演出编排

`dn.importsequence` 从 Level Sequence 摄像机剪辑创建演出：每个剪辑一个镜头，移动摄像机会有结束镜头与混合，剪辑之间为 `cut -> Shot hard`。镜头支持显式 `rot (pitch, yaw, roll)` 与角色锚点，未绑定时使用世界坐标。FOV 可从关卡中同名的摄像机 actor 获取。

**生成演出编排** 为每个说话者/听者对创建一个行相对的特写、过肩或中景镜头，有时带缓慢推镜。它通过 `@` 挂载到每条对应的行。相同的种子产生完全相同的文本；重复调用会重新随机，仅替换名称以 `演出_<Dialogue>_` 开头的先前生成的演出及其挂载。手动编排的行会被保留。

<a id="2026-10-07--dreamgameplaytask-migration"></a>

### 2026-10-07 — DreamGameplayTask 迁移

`dn.migratedgt` 反映 `DreamTask` 蓝图子类的默认值，而不链接 DGT。每个子类变成一个任务，带有标题、作为已接受日志的描述、High/Urgent 的主优先级、超时、子任务与自动接受。每个条件在 `<Quest>_计数` 中变成一个 extern 计数 fact，并成为一个目标。`Any` 用 `or` 组合条件；Custom N-of-M 变成 all，并附带一条审查备注。蓝图图表逻辑不会被转换；备注会标明需要在 `on` 块中重写的工作。输出写入 `<SourceDirectory>/Migrated/DreamGameplayTask.dna`。

<a id="2026-10-11--external-review-fixes"></a>

### 2026-10-11 — 外部审查修复

源基名在资产目录中必须唯一；DNS8001 会在修改资产之前拒绝冲突。记忆的源若位于当前源根之外或已不存在，可被新源替换。面板保存会在写入任何文件之前检查所有文件的外部更改，提供重新加载/放弃或覆盖；写入使用同目录临时文件并进行替换。

重命名会迁移任务计时器、章节检查点（旧名称汇聚时先到者胜出；已删除的章节会被丢弃）、访问计数和生成的 once 选项。未来版本的存档槽与配置快照会被整体拒绝。蓝图可调用的 `RegisterScripts` 在求值之前注册所有默认值。演出资产在编译时解析为路径与软引用；Cook 会跟随这些引用。缺失和有歧义的资产会以 DNS6001/6002 警告。编辑器会将 Asset Directory 下的所有脚本加入 Cook，导演会异步预加载播放资产。第 13 节定义了回调与共享状态契约。`Tools/CookSmoke.ps1` 执行 Cook、打包 Development 与 Shipping，并启动两者以校验脚本与演出资产。

<a id="2026-10-11--voice-stage-audio-blocking-and-localization"></a>

### 2026-10-11 — 语音、演出音频、走位与本地化

`dnc freeze` 将现有的自动对话/选项 ID（`Dialogue.N`、`Dialogue.cN`）写为显式 ` #id`，重新读取并校验，不匹配则回滚。后续插入不再使 ID 偏移；支持带点号的 ID。`bRequireLineIds` 会在录音或翻译开始后为缺失的 ID 添加 DNS6006。

角色可选择常规的 `voice: Path`、`voice: table` 或项目默认值（初始不查找）。常规路径会将行 ID 中的点替换为下划线。表来自 `*.voice.csv` 或配置的 DataTable，使用 `FDnaVoiceRow` 和行 ID 作为行名；重复项会给出 DNS6005。显式的行 `voice Path` 优先。编译会存储语音软引用与资产 `Duration`；缺失语音会给出 DNS6003（可选为错误），未使用的表行给出 DNS6004。

插件发出的是音频请求，而非音频播放。Runner `OnVoice` 在行出现时发出请求，在离开/跳过/暂停/中止时停止，并接受游戏的 `NotifyVoiceFinished`。Story 按说话者转发事件，并在另一个频道开始该说话者时停止其较早的语音。自动频道使用语音时长加 `VoiceTailSeconds`，或用 `bWaitForVoiceFinished` 等待游戏通知，受 `VoiceWaitGraceSeconds` 限制。进入时会预加载该对话的语音。`dnc vo-script` 导出录音 CSV；`dnc vo-status` 按说话者报告覆盖率。

演出音频同样发出事件：导演的 `OnSound` / `OnSoundStopped` 用于声音，仲裁者的 `OnMusic` 用于全局的后启动先停止栈。`keep` 音乐在其演出结束后归游戏所有。摄像机 `shake` 只影响本地玩家，并随演出停止。走位 `move`、`face` 与 `look` 以标记或 actor 为目标；范围会插值移动，点会瞬移，`navigate` 使用导航。`IDnaStageActor` 负责脚/头动画。除非 `restore` 包含 `blocking back`，否则 actor 会停留在目的地。

`SampleDnaStageBlocking` 由播放和预览共享：每个段从先前段的端点出发，并以在其起点采样的位置为目标，使用共同的目标与缓动。预览会移动关卡 actor 并在之后恢复它们；导航是采样之外的提示。运行时段从 actor 的实际位置出发，因此游戏或导航的移动不会弹回。

`dnc loc-export` 使用引擎收集器的命名空间/键规则导出翻译 CSV，其中包含说话人、对话、上一行、情感和源位置。`dnc loc-import` 通过 `FLocTextHelper` 写入目标资料库，与仪表板的 PO 导入方式相同。它会拒绝未收集的键和已更改的源行；随后仪表板会正常编译文本。CookSmoke 探针新增了演出音频和对话语音。DNS7006/7007 的动词列表也相应扩展。
