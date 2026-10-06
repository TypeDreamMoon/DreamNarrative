// Copyright DreamMoon. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DreamNarrativeDialogueRunner.h"
#include "DreamNarrativeLedger.h"
#include "DreamNarrativeStageSubsystem.h"
#include "DreamNarrativeStory.h"
#include "Fixtures/DnaTestWorld.h"

namespace UE::DreamNarrative::Tests::StageHostTests
{
	const TCHAR* Source = TEXT("Tests/Spec/Stage/Stages.dna");

	FString LineOf(const UDreamNarrativeDialogueRunner* Runner)
	{
		return Runner && Runner->GetState() == EDnaRunnerState::ShowingLine ? Runner->GetCurrentLine().Text.ToString() : FString();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaStageHostTest, "DreamNarrative.Stage.Host", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDnaStageHostTest::RunTest(const FString& Parameters)
{
	using namespace UE::DreamNarrative::Tests;
	using namespace UE::DreamNarrative::Tests::StageHostTests;

	FDnaTestWorld World(*this);
	UDreamNarrativeStageSubsystem* Stages = World.EnableStages();
	if (!World.CompilePluginFiles({ Source }))
	{
		return false;
	}
	UDreamNarrativeStory& Story = World.Story();
	TestTrue(TEXT("the host knows the stages"), Stages->HasStage(TEXT("开场")) && Stages->HasStage(TEXT("微推")));

	// -- a blocking `play` holds the dialogue for the stage's time, its blend back included
	UDreamNarrativeDialogueRunner* Runner = Story.TalkTo(TEXT("甲"));
	if (!TestNotNull(TEXT("甲 talks"), Runner))
	{
		return false;
	}
	Runner->Advance();
	TestEqual(TEXT("the stage holds the dialogue"), Runner->GetState(), EDnaRunnerState::PlayingStage);
	TestEqual(TEXT("one stage plays"), Stages->NumPlaying(), 1);
	World.Clock().Advance(2.2);
	TestEqual(TEXT("still, 2.2 s in (2 s and a 0.3 s blend back)"), Runner->GetState(), EDnaRunnerState::PlayingStage);
	World.Clock().Advance(0.2);
	TestEqual(TEXT("then the dialogue goes on"), LineOf(Runner), FString(TEXT("看到了吗?")));
	TestEqual(TEXT("and nothing plays"), Stages->NumPlaying(), 0);
	Runner->Advance();

	// -- the game moves on first: the stage stops, and its end does not move the dialogue again
	Runner = Story.TalkTo(TEXT("甲"));
	Runner->Advance();
	Runner->NotifyStageFinished();
	TestEqual(TEXT("moved on at once"), LineOf(Runner), FString(TEXT("看到了吗?")));
	TestEqual(TEXT("the stage stopped"), Stages->NumPlaying(), 0);
	World.Clock().Advance(5.0);
	TestEqual(TEXT("and stays on its line"), LineOf(Runner), FString(TEXT("看到了吗?")));
	Runner->Advance();

	// -- cut off while the stage plays: the stage goes with it
	Runner = Story.TalkTo(TEXT("甲"));
	Runner->Advance();
	Runner->Abort();
	TestEqual(TEXT("an aborted dialogue takes its stage along"), Stages->NumPlaying(), 0);

	// -- async: the dialogue does not wait
	Runner = Story.TalkTo(TEXT("乙"));
	Runner->Advance();
	TestEqual(TEXT("async: straight on"), LineOf(Runner), FString(TEXT("你慢慢看。")));
	TestEqual(TEXT("while it plays"), Stages->NumPlaying(), 1);
	World.Clock().Advance(1.0);
	TestEqual(TEXT("for its second"), Stages->NumPlaying(), 0);
	Runner->Advance();

	// -- a stage on a line: plays while the line is up, stops when it goes
	World.Ledger().SetFact(TEXT("到了"), FDnaValue::FromBool(true), EDnaWriteSource::Game);
	Runner = Story.TalkTo(TEXT("甲"));
	TestEqual(TEXT("the line with the stage"), LineOf(Runner), FString(TEXT("就是这里。")));
	TestEqual(TEXT("its stage plays"), Stages->NumPlaying(), 1);
	Runner->Advance();
	TestEqual(TEXT("and stops with the line"), Stages->NumPlaying(), 0);
	Runner->Advance();
	World.Ledger().SetFact(TEXT("到了"), FDnaValue::FromBool(false), EDnaWriteSource::Game);

	// -- `stage: pause`: the chatter holds still while a stage plays and goes on after
	FDnaEventPayload Idle;
	Idle.Event = TEXT("闲着");
	World.Ledger().RaiseEvent(Idle);
	UDreamNarrativeDialogueRunner* Chatter = Story.GetRunner(TEXT("闲话"));
	if (!TestNotNull(TEXT("the chatter talks"), Chatter))
	{
		return false;
	}
	Runner = Story.TalkTo(TEXT("甲"));
	Runner->Advance();
	TestTrue(TEXT("the chatter holds still during the stage"), Chatter->IsPaused());
	World.Clock().Advance(1.0);
	TestEqual(TEXT("time does not move it"), LineOf(Chatter), FString(TEXT("今天天气不错。")));
	World.Clock().Advance(1.5);
	TestFalse(TEXT("after the stage it goes on"), Chatter->IsPaused());
	TestEqual(TEXT("from its line"), LineOf(Chatter), FString(TEXT("今天天气不错。")));
	Runner->Advance();
	Story.StopAllDialogue();

	// -- loading a save ends what plays
	Runner = Story.TalkTo(TEXT("乙"));
	Runner->Advance();
	TestEqual(TEXT("an async stage plays"), Stages->NumPlaying(), 1);
	World.Ledger().ApplySnapshot(World.Ledger().MakeSnapshot());
	TestEqual(TEXT("a load ends it"), Stages->NumPlaying(), 0);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaStageNoHostTest, "DreamNarrative.Stage.NoHost", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDnaStageNoHostTest::RunTest(const FString& Parameters)
{
	using namespace UE::DreamNarrative::Tests;
	using namespace UE::DreamNarrative::Tests::StageHostTests;

	// Without Stage the story only tells the game: nobody listening, a blocking `play` goes straight on.
	FDnaTestWorld World(*this);
	if (!World.CompilePluginFiles({ Source }))
	{
		return false;
	}
	UDreamNarrativeDialogueRunner* Runner = World.Story().TalkTo(TEXT("甲"));
	if (!TestNotNull(TEXT("甲 talks"), Runner))
	{
		return false;
	}
	Runner->Advance();
	TestEqual(TEXT("nobody plays it: straight on"), LineOf(Runner), FString(TEXT("看到了吗?")));
	TestNull(TEXT("no host"), World.Ledger().GetStageHost());
	return !HasAnyErrors();
}

#endif
