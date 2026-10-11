# DreamNarrative 使用指南

剧情、任务、章节、对话与演出都写在 `.dna` 文本里;编译器把它们变成资产,运行时照着跑,编辑器只是文本的投影。
本文从装上插件讲到写完一个故事、把它接进游戏、测它、给它配演出。决议与理由在 `Architecture.md`,每个诊断码的
解释在 `Diagnostics.md`,一个完整的原创例子在 `Examples/Lighthouse/灯塔.dna`。

## 1. 装上

1. 把插件放进项目的 `Plugins/DreamNarrative`(UE 5.8;原版引擎即可)。依赖的引擎插件:EnhancedInput、Niagara、
   GameplayTags;GameplayAbilities 可选,只有 `DreamNarrativeGAS` 模块用它。
2. 项目设置 → Plugins → Dream Narrative:
   - `Source Directory`:`.dna` 放在项目根下的哪个目录,默认 `DNarrative`;
   - `Asset Directory`:编译出的脚本资产放哪,默认 `/Game/Narrative`;
   - Stage 一栏:演出按名字找素材的根目录、mask 用的后处理材质、`ramp dark` 的默认亮度、看门狗的宽限秒数。
3. 编译:编辑器开着时保存 `.dna` 会自动重建(几个编辑器同开时只有先开的那个落盘,编不过就不落盘);
   命令行用 `Tools/dnc.ps1 build`(编辑器关着)。只检查不写资产用 `dnc.ps1 lint`。

## 2. 写一个故事

一个文件里可以混写所有顶层块;名字全项目唯一(角色、fact、对话、任务、章节、演出、频道、事件、资料共用一个命名空间)。
缩进表示从属,`//` 是注释,中文标识符与全角标点都认。

### 2.1 角色与 fact

```
character 守夜人 player
character 老周
  display: 老周

fact 见过老周: bool
fact 找到的零件: int extern range 0..3     // extern:游戏写,剧情读
fact 小雾的信任: int range 0..3
fact 已见结局 profile                       // 一组;profile:跨存档、跨周目
  A: bool
  B: bool
```

fact 只有 `bool / int / float / tag` 四种;`tag in (甲, 乙)` 是有限集合。剧情自己的 fact 只能由 `.dna` 写,游戏代码写会被拦下;
游戏要喂进来的写 `extern`。作用域:默认跟存档槽;`session` 不存盘;`profile` 跨存档;`progress` 选章时保留。

### 2.2 对话

```
dialogue 老周_初见 for 老周 if not 见过老周
  老周: 你就是新来的守夜人?
  老周(皱眉): 灯坏了三天了。
  - 我去修。
      老周: 灯室里缺三个零件。
      accept 修灯
  - 先歇一会儿。 once
  set 见过老周 = true
```

- `for 角色` 是这个角色的入口;同一角色有多段时,条件成立、优先级高(`priority n`,缺省是条件项数)的先;`once`、
  `cooldown 30s` 管入口能不能再开。
- 行尾修饰:`if 条件`、`once`、`for 3s`(自己往下说时停多久)、`@演出`(跟着这一行的演出)、`#id`(稳定行 ID)。
- 命令:`set`、`accept / complete / fail 任务`、`advance 章节`、`play 演出 [async]`、`emit 事件(参数 = 值)`、`wait 2s`、`-> 对话`。
- `{表达式}` 插值;`visited(对话)`、`random(1, 6)`、`chance(0.3)`、`chapter == 章节`、`任务.active` 都能写进条件。

### 2.3 频道与事件:边打边讲

```
channel 电台
  input: free          // 不锁输入
  advance: auto        // 自己往下说
  busy: queue          // 忙时排队;interrupt 打断;drop 丢掉
  stage: pause         // 演出期间停下

event 风暴逼近
  等级: int range 1..5 = 1

dialogue 电台_风暴 on 风暴逼近 channel 电台 cooldown 30s
  无线电: 沿海广播,风暴等级 {风暴逼近.等级}。
```

主频道 `main` 永远在:锁输入、玩家推进。别的频道和它并行;`pauses: 频道` 说话时让别人停下,`stops:` 一开口就打断。
游戏 `RaiseEvent` 一个事件,每个频道挑最合适的一段 `on` 对话开。

### 2.4 任务

```
quest 修灯
  title: 修好灯塔
  priority: main
  timeout: 20m
  journal
    accepted: 灯室缺三个零件。
    going: 还差 {3 - 找到的零件} 个。
    completed: 灯亮了。
  objective 找到的零件 >= 3
    show: 零件 {找到的零件}/3
  on complete
    set 灯修好了 = true
    reward 物品 守夜人的钥匙
  subquest 送小雾回家 auto
```

任务观察账本:每批写入后检查 `fail if`、锁存达成的目标,必需目标齐了就完成。状态存在账本里,存读档自然带上。
`priority: hidden` 的任务可接即自启(记忆链、隐藏结局)。`gameplay: 标签` 让 GAS 桥在完成时发这个 GameplayEvent。

### 2.5 章节与周目

```
chapter 上岛
chapter 风暴 after 上岛
  implied
    见过老周 = true          // 传送 / 选章到这里时补上的 fact
  advance -> 天亮 if 灯修好了
```

第一次进一章留检查点;`SelectChapter` 回到检查点但保留 `progress` 的 fact 与支线;`StartNewPlaythrough` 清掉存档槽、保留 profile。

### 2.6 资料库

```
archive 守夜日志 profile
  title: 守夜日志
  entry
    第一夜:灯坏了。
  entry if 灯修好了
    灯亮了。
```

条件第一次成立时解锁,解锁了就一直在。

## 3. 演出(Stage)

```
stage 灯亮
  duration 5.0s
  shots
    灯室       local (0, 0, 900) of 灯塔 look 灯塔 fov 50
  camera
    0.0s     blend from game 0.5s
    0.5s     blend -> 灯室 1.5s
    2.5s     cut -> 海面
    5.0s     blend back 0.6s
  mask
    2.4s..2.6s   fade white 0.1s      // 切镜头必须落在遮罩最强处(DNS7001)
  light
    0.0s     ramp dark 0.5s to 0.3
    2.5s     restore 0.3s
  ui
    0.0s..5.0s   hide hud
  game
    0.0s..5.0s   input lock
    2.5s..2.7s   hitstop 0.2x
```

- 轨道:camera、light、mask、vfx、ui、anim、game、sequence;动词表见 `Diagnostics.md` 的 DNS7006。
- 时间基:默认秒;`base: line` 的演出跟着挂上的台词走,时间写百分比,只能用 `@演出名` 挂在台词上。
- 机位按角色名摆放:角色名就是 actor 的 tag(或用导演的 `BindActor` 绑定);`rot (pitch, yaw, roll)` 给出明确朝向;
  不绑定的锚点就是世界坐标。
- 改了世界的东西要写怎么放回去(轨道后面,或 `restore` 块,DNS7003)。导演是事务式的:演完、被停、被替换、玩家
  死了、关卡走了,碰过的东西都会放回原样。
- 谁来演:Stage 子系统把对话的 `play`、台词的 `@`、任务的 `play` 放到导演组件上——最后开始的那个,或者第一个本地
  玩家控制器上自动加的一个。没有玩家控制器时,演出在账本时钟上走完它的时长,剧情照样等。
- 几个导演同时演:时间倍率、太阳和天光的比例相乘,HUD 有一个演出藏着就藏着,镜头是最后接管的那个;谁先结束都
  留着别人的,最后一个结束时还原。两次变化之间不碰这些状态,游戏自己也可以改。HUD 不是 AHUD 的,听世界子系统
  `UDreamNarrativeStageArbiter` 的 `OnHudVisibility`(所有演出合起来)。导演的结束回调里再 `Play`:新的那次优先,
  被取代的外层请求返回 0。
- 资源:`montage`、`spawn`、`sequence play` 写的资源编译时就找(名字在 Stage Asset Roots 下找,路径直接认),找到的写成
  路径并记进脚本资产,打包时一起带上;找不到是 DNS6001 警告——编辑器里也许还能演,包里多半没有。
- 能力里演:`PlayNarrativeStage` 能力任务;或在演出里写 `gameplay: 标签`,GAS 桥收到这个事件就在化身上演。

## 4. 接进游戏

```cpp
UDreamNarrativeStory* Story = GetGameInstance()->GetSubsystem<UDreamNarrativeStory>();
Story->LoadAllScripts();                       // 资产目录下的所有脚本

UDreamNarrativeDialogueRunner* Runner = Story->TalkTo(TEXT("老周"));
Runner->OnLine.AddDynamic(this, &AMyHUD::ShowLine);        // FDnaLineView:说话人、文本、时长、频道
Runner->OnChoices.AddDynamic(this, &AMyHUD::ShowChoices);  // 选了就 Runner->Choose(Index)
// 玩家按键:Runner->Advance()

Story->GetLedger()->SetFactInt(TEXT("找到的零件"), 2);        // extern fact
FDnaEventPayload Storm; Storm.Event = TEXT("风暴逼近"); Storm.Set(TEXT("等级"), FDnaValue::FromInt(4));
Story->GetLedger()->RaiseEvent(Storm);
```

- 手动加载多个脚本:先收齐资产,再调 `Story->RegisterScripts(Scripts)`(蓝图也能调)。它先注册全部 fact 默认值,
  再评估隐藏任务和档案条件。`LoadAllScripts()` 就是这么做的;只有一个脚本时仍可用 `RegisterScript()`。
- 声明过的频道各有自己的 runner:`Story->OnDialogueStarted` 拿到每一个。插件不带 UI,只发事件。
- 存档:`Ledger->MakeSnapshot()` / `ApplySnapshot()`;profile 另有 `MakeProfileSnapshot()` / `ApplyProfileSnapshot()`。
  比当前版本新的快照会被整份拒绝(返回 false)。
- 打包:资产目录下的脚本自动加进 Cook,不用配 Primary Asset 或 Always Cook。`Tools/CookSmoke.ps1` 在测试宿主上做一遍
  Cook、打包 Development / Shipping,并启动打出来的游戏检查。
- 演出的 UI 部分:导演的 `OnText`(字幕、浮字)、`OnHudVisibility`、`OnDamageWindow`、`OnWarpTarget`(给 Motion Warping)。
- GAS:`UDreamNarrativeGASBridge::WatchAbilitySystem(ASC)`;`event 名 gameplay 标签` 把 GameplayEvent 变成剧情事件,
  `fact 名: bool extern gameplay 标签` 跟着标签计数。
- 控制台:`dn.load`、`dn.talk 角色`、`dn.start 对话`、`dn.next`、`dn.choose n`、`dn.fact 名 [值]`、`dn.facts`、`dn.jump 章节`、
  `dn.emit 事件`、`dn.channels`、`dn.quests`、`dn.accept / complete / fail 任务`、`dn.newgame`、`dn.select 章节`、
  `dn.checkpoints`、`dn.archive`、`dn.record start|stop`。

## 5. 测

```
test 零件齐了灯就亮
  given
    chapter = 风暴
  accept 修灯
  set 找到的零件 = 3
  expect 灯修好了
  expect chapter == 天亮
```

- `test` 块和故事写在一起;`talk 角色`、`emit 事件`、`expect 条件`、`wait 10s`、`newgame`、`select 章节`……
  `strict` 要求逐行对上。
- `explore` 块让探索器把所有走法走一遍:`always` / `eventually` / `softlock` 断言,找到反例会给出复现用的 test 块。
- `dn.record start` / `stop` 把一次手玩录成 test 块。
- 跑:`Tools/dnc.ps1 test -Preset Quick|Explore|Smoke|All`(用插件自带的测试宿主工程)。

## 6. 编辑器

- **Narrative Story**(工具菜单):章节、任务、对话、事件、演出与它们之间的边;双击跳到源码(编辑器偏好里设链接模板,
  默认 VS Code);右键改条件、连线、新建 / 删除对话、**生成演出**(每对说话人与听者一个跟着台词的镜头,再点一次重摇)。
- **Narrative Stage Timeline**:一条演出的各轨;拖条目、拖范围的尾、拖过程把手,松手时只改源码里那一行;切换时间基;
  拖播放头看那一刻的样子,可让关卡视口沿机位飞。
- **Narrative Debugger**:PIE 里的章节、检查点、每个 fact 与它的作用域、各频道在说什么、谁写了哪个 fact;能跳章、选章、开新周目。
- 所有编辑都有自己的撤销 / 重做,存盘前不碰文件。存盘时如果文件在外面被改过,面板会问:放弃面板的改动、重读文件,
  还是用面板的改动覆盖;取消则两边都不动。
- `dn.importsequence /Game/路径/LS_过场` 把关卡序列的镜头切换写成一个演出;`dn.migratedgt` 把 DreamGameplayTask 的任务迁成 `.dna`。

## 7. 本地化

台词、选项、标题、日志、资料、演出文字都是可本地化文本:命名空间是 `DNA/<文件名>`,key 是行 ID(或 `stage.<演出>.text<n>`
这类稳定名字)。脚本资产在 `Asset Directory` 下,用引擎的本地化面板按资产采集即可;要 key 不随行移动,给行写上 `#id`。

## 8. VS Code

`dreamnarrative-language-support` 扩展:高亮、大纲、跨文件跳转与悬停、片段;命令「Check the Project's .dna Sources」
调用 `dnc.ps1 lint`,诊断进「问题」面板。
