// Copyright DreamMoon. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DreamNarrativeDialogueRunner.h"
#include "DreamNarrativeLedger.h"
#include "DreamNarrativeStory.h"
#include "Fixtures/DnaTestWorld.h"

namespace
{
	FDnaEventPayload MakeEvent(const TCHAR* Name)
	{
		FDnaEventPayload Event;
		Event.Event = Name;
		return Event;
	}
}

// ---------------------------------------------------------------- holding each other still

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaChannelsHoldEachOtherTest, "DreamNarrative.Dialogue.ChannelsHoldEachOther", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDnaChannelsHoldEachOtherTest::RunTest(const FString& Parameters)
{
	using namespace UE::DreamNarrative::Tests;

	FDnaTestWorld World(*this);
	const bool bCompiled = World.CompileText(TEXT(
		"character 甲\n"
		"channel main\n"
		"  pauses: 闲聊\n"
		"channel 闲聊\n"
		"event 闲置\n"
		"dialogue 碎碎念 on 闲置 channel 闲聊\n"
		"  甲: 一。\n"
		"  甲: 二。\n"
		"dialogue 正事 for 甲\n"
		"  甲: 听我说。\n"));
	if (!bCompiled)
	{
		return false;
	}
	UDreamNarrativeStory& Story = World.Story();

	// A declared channel talks while the player plays, moving on by itself.
	World.Ledger().RaiseEvent(MakeEvent(TEXT("闲置")));
	UDreamNarrativeDialogueRunner* Chatter = Story.GetRunner(TEXT("闲聊"));
	if (!TestNotNull(TEXT("the chatter started"), Chatter))
	{
		return false;
	}
	TestFalse(TEXT("it leaves the input alone"), Story.IsInputLocked());
	TestEqual(TEXT("its first line"), Chatter->GetCurrentLine().Text.ToString(), FString(TEXT("一。")));
	TestEqual(TEXT("which goes on by itself"), World.Clock().NumPending(), 1);

	// The player's conversation holds it still: off the screen, off the clock.
	UDreamNarrativeDialogueRunner* Talk = Story.TalkTo(TEXT("甲"));
	if (!TestNotNull(TEXT("the talk started"), Talk))
	{
		return false;
	}
	TestTrue(TEXT("main locks the input"), Story.IsInputLocked());
	TestTrue(TEXT("the chatter holds still"), Chatter->IsPaused());
	TestEqual(TEXT("nothing of it on the clock"), World.Clock().NumPending(), 0);
	World.Clock().Advance(60.0);
	TestEqual(TEXT("time passing does not move it"), Chatter->GetCurrentLine().Text.ToString(), FString(TEXT("一。")));

	// Once main is quiet the chatter says the line it was cut at again, and goes on from there.
	Talk->Advance();
	TestEqual(TEXT("the talk ended"), Talk->GetState(), EDnaRunnerState::Ended);
	TestFalse(TEXT("the input is free again"), Story.IsInputLocked());
	TestFalse(TEXT("the chatter goes on"), Chatter->IsPaused());
	TestEqual(TEXT("with the same line"), Chatter->GetCurrentLine().Text.ToString(), FString(TEXT("一。")));
	TestTrue(TEXT("shown again"), Chatter->GetCurrentLine().bRepeat);
	TestEqual(TEXT("which counts as shown"), Chatter->GetLinesShown(), 2);
	TestEqual(TEXT("and runs on the clock again"), World.Clock().NumPending(), 1);
	World.Clock().AdvanceToNext();
	TestEqual(TEXT("then the next line"), Chatter->GetCurrentLine().Text.ToString(), FString(TEXT("二。")));
	TestFalse(TEXT("shown for the first time"), Chatter->GetCurrentLine().bRepeat);
	return !HasAnyErrors();
}

// ---------------------------------------------------------------- loading

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaLoadEndsEveryDialogueTest, "DreamNarrative.Dialogue.LoadEndsEveryDialogue", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDnaLoadEndsEveryDialogueTest::RunTest(const FString& Parameters)
{
	using namespace UE::DreamNarrative::Tests;

	FDnaTestWorld World(*this);
	const bool bCompiled = World.CompileText(TEXT(
		"character 甲\n"
		"channel 广播\n"
		"  busy: queue\n"
		"event 响\n"
		"event 又响\n"
		"dialogue 头一回 on 响 channel 广播 cooldown 30s\n"
		"  甲: 响了。\n"
		"dialogue 第二回 on 又响 channel 广播\n"
		"  甲: 又响了。\n"
		"dialogue 正事 for 甲\n"
		"  甲: 听我说。\n"));
	if (!bCompiled)
	{
		return false;
	}
	UDreamNarrativeStory& Story = World.Story();
	UDreamNarrativeLedger& Ledger = World.Ledger();
	const FDnaLedgerSnapshot Saved = Ledger.MakeSnapshot();

	Ledger.RaiseEvent(MakeEvent(TEXT("响")));
	Ledger.RaiseEvent(MakeEvent(TEXT("又响")));
	UDreamNarrativeDialogueRunner* Broadcast = Story.GetRunner(TEXT("广播"));
	UDreamNarrativeDialogueRunner* Talk = Story.TalkTo(TEXT("甲"));
	if (!TestNotNull(TEXT("the broadcast started"), Broadcast) || !TestNotNull(TEXT("the talk started"), Talk))
	{
		return false;
	}
	TestEqual(TEXT("the second waits its turn"), Story.GetQueueLength(TEXT("广播")), 1);

	// Dialogue positions are not saved: a load ends every dialogue and drops what waited.
	Ledger.ApplySnapshot(Saved);
	TestEqual(TEXT("the broadcast ended"), Broadcast->GetState(), EDnaRunnerState::Ended);
	TestEqual(TEXT("the talk ended"), Talk->GetState(), EDnaRunnerState::Ended);
	TestNull(TEXT("nothing on the broadcast channel"), Story.GetRunner(TEXT("广播")));
	TestEqual(TEXT("nothing waiting there"), Story.GetQueueLength(TEXT("广播")), 0);
	TestFalse(TEXT("the input is free"), Story.IsInputLocked());
	TestEqual(TEXT("nothing on the clock"), World.Clock().NumPending(), 0);
	TestEqual(TEXT("the ended dialogues are not visits of the loaded story"), Ledger.GetVisited(TEXT("头一回")), 0);

	// Cooldowns start over with the load: the first broadcast opens again at once.
	Ledger.RaiseEvent(MakeEvent(TEXT("响")));
	UDreamNarrativeDialogueRunner* Again = Story.GetRunner(TEXT("广播"));
	TestTrue(TEXT("its cooldown did not survive the load"), Again && Again != Broadcast && Again->GetNodeName() == TEXT("头一回"));
	return !HasAnyErrors();
}

// ---------------------------------------------------------------- what an event carries

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaEventArgsFitTheirParametersTest, "DreamNarrative.Ledger.EventArgsFitTheirParameters", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDnaEventArgsFitTheirParametersTest::RunTest(const FString& Parameters)
{
	using namespace UE::DreamNarrative::Tests;

	FDnaTestWorld World(*this);
	const bool bCompiled = World.CompileText(TEXT(
		"event 敲门\n"
		"  次: int range 1..5 = 1\n"
		"  谁: tag in (甲, 乙) = 甲\n"
		"  急: bool\n"));
	if (!bCompiled)
	{
		return false;
	}
	UDreamNarrativeLedger& Ledger = World.Ledger();
	TArray<FString> Heard;
	const FDelegateHandle Handle = Ledger.OnEventRaised.AddLambda([&Heard](const FDnaEventPayload& Event, EDnaWriteSource Source)
	{
		Heard.Add(Event.ToString() + (Source == EDnaWriteSource::Game ? TEXT(" by the game") : TEXT(" by the story")));
	});

	auto Raise = [&Ledger](const TCHAR* Name, TArray<TPair<const TCHAR*, FDnaValue>> Args)
	{
		FDnaEventPayload Event = MakeEvent(Name);
		for (const TPair<const TCHAR*, FDnaValue>& Arg : Args)
		{
			Event.Set(Arg.Key, Arg.Value);
		}
		Ledger.RaiseEvent(Event);
	};
	AddExpectedMessagePlain(TEXT("does not fit parameter"), ELogVerbosity::Warning);
	AddExpectedMessagePlain(TEXT("has no parameter"), ELogVerbosity::Warning);

	Raise(TEXT("敲门"), {});
	Raise(TEXT("敲门"), { { TEXT("次"), FDnaValue::FromFloat(9.7f) }, { TEXT("急"), FDnaValue::FromInt(1) } });
	Raise(TEXT("敲门"), { { TEXT("谁"), FDnaValue::FromTag(TEXT("丙")) } });
	Raise(TEXT("敲门"), { { TEXT("响度"), FDnaValue::FromInt(3) } });
	Raise(TEXT("来人"), { { TEXT("几个"), FDnaValue::FromInt(2) } });
	Ledger.OnEventRaised.Remove(Handle);

	TestEqual(TEXT("what each raise carried"), FString::Join(Heard, TEXT("\n")), FString(TEXT(
		"敲门(次 = 1, 谁 = 甲, 急 = false) by the game\n"
		"敲门(次 = 5, 谁 = 甲, 急 = true) by the game\n"
		"敲门(次 = 1, 谁 = 甲, 急 = false) by the game\n"
		"敲门(次 = 1, 谁 = 甲, 急 = false) by the game\n"
		"来人(几个 = 2) by the game")));
	return !HasAnyErrors();
}

#endif
