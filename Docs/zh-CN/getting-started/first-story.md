---
title: 第一个故事
description: 从一段对话到一个能测的任务。
---

在 `DNarrative/` 下建一个 `灯塔.dna`:

```dna
character 守夜人 player
character 老周

fact 见过老周: bool
fact 找到的零件: int extern range 0..3

dialogue 老周_初见 for 老周 if not 见过老周
  老周: 你就是新来的守夜人?
  - 我去修。
      accept 修灯
  - 先歇一会儿。
  set 见过老周 = true

quest 修灯
  title: 修好灯塔
  objective 找到的零件 >= 3
    show: 零件 {找到的零件}/3

test 零件齐了任务就完成
  talk 老周
    老周: 你就是新来的守夜人?
    - 我去修。
  set 找到的零件 = 3
  expect 修灯.completed
```

保存,编辑器重建脚本;`Tools/dnc.ps1 test` 跑测试。游戏里:

```cpp
UDreamNarrativeStory* Story = GetGameInstance()->GetSubsystem<UDreamNarrativeStory>();
Story->LoadAllScripts();
UDreamNarrativeDialogueRunner* Runner = Story->TalkTo(TEXT("老周"));
Runner->OnLine.AddDynamic(this, &AMyHUD::ShowLine);
Runner->OnChoices.AddDynamic(this, &AMyHUD::ShowChoices);
```

完整的例子在插件的 `Examples/Lighthouse/灯塔.dna`:章节、频道、事件、演出、资料库、test 块都用到了。
