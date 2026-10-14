---
title: 接进游戏
description: 子系统、runner、存档、控制台。
---

```cpp
UDreamNarrativeStory* Story = GetGameInstance()->GetSubsystem<UDreamNarrativeStory>();
Story->LoadAllScripts();

UDreamNarrativeDialogueRunner* Runner = Story->TalkTo(TEXT("老周"));
Runner->OnLine.AddDynamic(this, &AMyHUD::ShowLine);        // 说话人、文本、时长、频道
Runner->OnChoices.AddDynamic(this, &AMyHUD::ShowChoices);  // 选了就 Runner->Choose(Index)
// 玩家按键:Runner->Advance()

UDreamNarrativeLedger* Ledger = Story->GetLedger();
Ledger->SetFactInt(TEXT("找到的零件"), 2);                 // extern fact

FDnaEventPayload Storm;
Storm.Event = TEXT("风暴逼近");
Storm.Set(TEXT("等级"), FDnaValue::FromInt(4));
Ledger->RaiseEvent(Storm);
```

- 插件不带 UI,只发事件:`Story->OnDialogueStarted` 拿到每个频道的 runner;任务有 `OnQuestStateChanged`、`OnQuestUpdated`、`OnQuestRewards`;
  资料有 `OnArchiveUnlocked`。
- 存档:`Ledger->MakeSnapshot()` / `ApplySnapshot()`;跨周目的 `MakeProfileSnapshot()` / `ApplyProfileSnapshot()`。
- 时间:一切等待都走账本时钟——默认是游戏世界时间(暂停即停),测试里换成手动时钟。
- 控制台:`dn.load`、`dn.talk 角色`、`dn.start 对话`、`dn.next`、`dn.choose n`、`dn.fact 名 [值]`、`dn.facts`、`dn.jump 章节`、
  `dn.emit 事件`、`dn.channels`、`dn.quests`、`dn.newgame`、`dn.select 章节`、`dn.checkpoints`、`dn.archive`、`dn.record start|stop`。
