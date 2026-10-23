---
title: 装上
description: 插件、项目设置与第一次编译。
---

1. 把插件放进项目的 `Plugins/DreamNarrative`。引擎:UE 5.8(启动器里的原版即可)。依赖的引擎插件:EnhancedInput、
   Niagara、GameplayTags;GameplayAbilities 可选,只有 `DreamNarrativeGAS` 模块用它。
2. 项目设置 → Plugins → Dream Narrative:
   - **Source Directory**:`.dna` 放在项目根下的哪个目录,默认 `DNarrative`;
   - **Asset Directory**:编译出的脚本资产放哪,默认 `/Game/Narrative`;
   - **Stage**:演出按名字找素材的根目录、mask 用的后处理材质、`ramp dark` 的默认亮度、看门狗的宽限秒数。
3. 编译:
   - 编辑器开着时,保存 `.dna` 就会重建(几个编辑器同开时只有先开的那个落盘;编不过就不落盘,错误进日志);
   - 编辑器关着时用命令行:`Tools/dnc.ps1 build`;只检查不写资产:`dnc.ps1 lint`;
   - 控制台 `dn.rebuild` 手动重建。

**模块：**

`Core`(账本、表达式、事件、演出宿主接口)· `Story`(对话、频道、任务、章节、资料)· `Stage`(导演、恢复事务、看门狗)·
`GAS`(可选)· `Compiler`(UncookedOnly)· `Editor` · `Tests`(UncookedOnly)。Story 与 Stage 互不依赖,只经账本上的演出宿主接起来。
