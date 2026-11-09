<a id="game-integration"></a>

# 接进游戏

```cpp
UDreamNarrativeStory* Story = GetGameInstance()->GetSubsystem<UDreamNarrativeStory>();
Story->LoadAllScripts();                       // All scripts under Asset Directory

UDreamNarrativeDialogueRunner* Runner = Story->TalkTo(TEXT("老周"));
Runner->OnLine.AddDynamic(this, &AMyHUD::ShowLine);        // FDnaLineView: speaker, text, duration, channel
Runner->OnChoices.AddDynamic(this, &AMyHUD::ShowChoices);  // Call Runner->Choose(Index) for a selection
// Player advancement: Runner->Advance()

Story->GetLedger()->SetFactInt(TEXT("找到的零件"), 2);        // An extern fact
FDnaEventPayload Storm; Storm.Event = TEXT("风暴逼近"); Storm.Set(TEXT("等级"), FDnaValue::FromInt(4));
Story->GetLedger()->RaiseEvent(Storm);
```

- 要手动加载多个脚本，先收集它们的资产并调用 `Story->RegisterScripts(Scripts)`（蓝图也可用）。它会在评估隐藏任务和资料库条件之前注册所有 fact 默认值。`LoadAllScripts()` 遵循此顺序；只有一个脚本时请使用 `RegisterScript()`。
- 每个声明的频道都有一个 runner；`Story->OnDialogueStarted` 会报告每一个。插件只发出事件，不提供 UI。
- 用 `Ledger->MakeSnapshot()` / `ApplySnapshot()` 存档。档案数据有单独的 `MakeProfileSnapshot()` / `ApplyProfileSnapshot()` 调用。来自更新版本的快照会被整体拒绝，并返回 false。
- Asset Directory 中的脚本会自动包含进 Cook；无需设置 Primary Asset 或 Always Cook。`Tools/CookSmoke.ps1` 会执行 cook、打包 Development 和 Shipping 构建，并在测试主机中启动打包后的游戏进行验证。
- 演出 UI 钩子包括导演的 `OnText`（字幕和浮动文字）、`OnHudVisibility`、`OnDamageWindow` 和 `OnWarpTarget`（用于 Motion Warping）。
- GAS：调用 `UDreamNarrativeGASBridge::WatchAbilitySystem(ASC)`。`event Name gameplay Tag` 将 GameplayEvent 转换为叙事事件；`fact Name: bool extern gameplay Tag` 追踪 tag 的存在/计数。
- 控制台命令：`dn.load`、`dn.talk Character`、`dn.start Dialogue`、`dn.next`、`dn.choose n`、`dn.fact Name [Value]`、`dn.facts`、`dn.jump Chapter`、`dn.emit Event`、`dn.channels`、`dn.quests`、`dn.accept / complete / fail Quest`、`dn.newgame`、`dn.select Chapter`、`dn.checkpoints`、`dn.archive` 和 `dn.record start|stop`。

<a id="voice"></a>

## 语音

插件不播放音频。编译时会为每行解析语音资产和时长；运行时会告知游戏何时开始和停止，播放交由游戏的音频系统处理。

```dna
character 老周
  voice: /Game/VO/老周      // Convention: /Game/VO/老周/<LineId>, with dots replaced by underscores
character 小雾
  voice: table              // Look up the voice table
dialogue 老周_初见 for 老周
  老周: 你就是新来的? #老周_初见.1
  老周: 灯坏了三天了。 voice /Game/VO/特殊/灯坏了   // Override this line's asset
```

- 每个角色可选择一个查找模式。没有 `voice:` 时，角色使用项目设置中的 `Default Voice Lookup`；其默认值 None 不执行查找，也不报告缺失语音。
- 语音表可以是源目录下的 `*.voice.csv` 文件，包含 `LineId` 和 `Voice` 列（其他列会被忽略）；也可以是使用 `FDnaVoiceRow` 配置的 DataTable，以行 ID 作为行名。重复的行会产生 DNS6005；缺失语音会产生 DNS6003；未使用的表行会产生 DNS6004。
- 解析出的语音资产会成为脚本引用，并随脚本一起打包。时长来自资产的 `Duration`。录制前先运行 `dnc freeze`，让行 ID 不再变化。
- `Story->OnVoice(Runner, Event, Line, Speaker)` 接收所有频道的语音事件；每个 runner 也暴露 `OnVoice`：
  - `Requested`：该行已出现；播放 `Line.Voice`。暂停后重新显示该行会再次请求从头播放。
  - `Stopped`：该行因玩家推进、暂停、对话结束或同一说话者在另一频道开始而被中断；停止播放。
  - `Finished`：该行在推进之前结束，可能是通过自动计时器，也可能是游戏调用了 `NotifyVoiceFinished`。
- `Speaker` 是用 `Story->BindSpeaker(Character, Actor)` 绑定的 actor，否则就是带有该角色名 tag 的 actor。在此集成口型同步和面部动画：在 `Requested` 时，在 `Speaker` 上播放 `Line.Voice`（例如在头部骨骼上播放 `SpawnSoundAttached`），并将该音频组件传给所选的口型同步系统，如 MetaHuman 音频驱动动画、Audio2Face 或 OVRLipSync。在 `Stopped` 时，停止该组件及其口型同步。插件既不管理音频，也不管理面部。
- 自动频道在语音时长加上 `Voice Tail Seconds` 之后推进。如果游戏的音频系统能更准确地知道结束时间，可调用 `Runner->NotifyVoiceFinished()`。启用 `Wait For Voice Finished` 可改为等待该通知；如果到时长加上 `Voice Wait Grace Seconds` 仍未收到，则照常推进。
- 同一说话者同时只有一条活跃语音。在另一频道开始时，会先为其上一条语音发出 `Stopped`；上一行仍可留在屏幕上。
- 其他语言的语音是引擎的本地化资产：将每个放在 `Content/L10N/<culture>/` 下的相同路径（`/Game/VO/老周/老周_初见_1` → `/Game/L10N/en/VO/老周/老周_初见_1`）。行仍然引用源语言资产；游戏（独立运行或打包）会加载当前语言的本地化资产。编辑器和 PIE 播放源语言。时长来自源资产，因此当各语言长度不同时请启用 `Wait For Voice Finished`。
- `dnc.ps1 vo-script [-Out Path]` 导出一份录音脚本，包含行 ID、说话者、源文本、对话、前一行、情绪、语音状态和预期的常规资产名。它按说话者排序，并以带 BOM 的 UTF-8 编码，便于表格软件打开。前几列是 `LineId,Voice`：填写 `Voice`，将文件重命名为 `xxx.voice.csv`，并放入源目录即可作为语音表使用。`dnc.ps1 vo-status` 会按说话者报告总数、覆盖率和缺失行。
