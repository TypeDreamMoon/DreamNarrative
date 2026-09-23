// Copyright DreamMoon. All Rights Reserved.
//
// Quest behaviour a test block cannot reach. Asking a quest for what it cannot do: the story's own
// commands only warn (the explorer reports them), game code gets false back, and a test's step would
// fail. Loading a save into a session that already has a story going. The story before anything has
// been written. And when a quest tells the game that what it shows has moved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DreamNarrativeLedger.h"
#include "DreamNarrativeStory.h"
#include "DreamNarrativeTrace.h"
#include "Fixtures/DnaTestWorld.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaQuestRefusalsTest, "DreamNarrative.Quest.Refusals", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDnaQuestRefusalsTest::RunTest(const FString& Parameters)
{
	using namespace UE::DreamNarrative::Tests;

	FDnaTestWorld World(*this);
	if (!World.CompileText(TEXT(
		"fact 允许: bool\n"
		"quest 采药\n"
		"  available if 允许\n")))
	{
		return false;
	}
	UDreamNarrativeStory& Story = World.Story();

	AddExpectedMessagePlain(TEXT("cannot be accepted now"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 2);
	TestFalse(TEXT("an unavailable quest is not taken on"), Story.AcceptQuest(TEXT("采药")));
	TestEqual(TEXT("and stays inactive"), Story.GetQuestStateOf(TEXT("采药")), EDnaQuestState::Inactive);

	AddExpectedMessagePlain(TEXT("cannot be completed"), ELogVerbosity::Warning);
	TestFalse(TEXT("an inactive quest cannot be completed"), Story.CompleteQuest(TEXT("采药")));
	AddExpectedMessagePlain(TEXT("cannot be failed"), ELogVerbosity::Warning);
	TestFalse(TEXT("nor failed"), Story.FailQuest(TEXT("采药")));

	World.Given(TEXT("允许"), FDnaValue::FromBool(true));
	TestTrue(TEXT("available now"), Story.AcceptQuest(TEXT("采药")));
	TestTrue(TEXT("and completed by the game"), Story.CompleteQuest(TEXT("采药")));
	TestFalse(TEXT("a completed quest is not taken on again"), Story.AcceptQuest(TEXT("采药")));

	AddExpectedMessagePlain(TEXT("There is no quest called"), ELogVerbosity::Error);
	TestFalse(TEXT("an undeclared quest"), Story.AcceptQuest(TEXT("没有这个")));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaQuestLoadTest, "DreamNarrative.Quest.LoadingKeepsAcceptedHonest", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDnaQuestLoadTest::RunTest(const FString& Parameters)
{
	using namespace UE::DreamNarrative::Tests;

	FDnaTestWorld World(*this);
	if (!World.CompileText(TEXT(
		"fact 数: int extern\n"
		"fact 别的: int extern\n"
		"quest 数数\n"
		"  objective 数 >= 5\n")))
	{
		return false;
	}
	UDreamNarrativeStory& Story = World.Story();
	UDreamNarrativeLedger& Ledger = World.Ledger();

	// This session took the quest on with the count at 2, and remembers that.
	World.SetExtern(TEXT("数"), FDnaValue::FromInt(2));
	Story.AcceptQuest(TEXT("数数"));
	TestEqual(TEXT("taken on"), Story.GetQuestStateOf(TEXT("数数")), EDnaQuestState::Accepted);

	// A save from another playthrough: the same quest just taken on, with the count at 4.
	FDnaLedgerSnapshot Save = Ledger.MakeSnapshot();
	for (FDnaFactSnapshotEntry& Entry : Save.Facts)
	{
		if (Entry.Fact == FName(TEXT("数"))) { Entry.Value = FDnaValue::FromInt(4); }
	}

	// Loading it: the count this session saw does not count -- neither while the save goes in nor after.
	Ledger.ApplySnapshot(Save);
	TestEqual(TEXT("loaded as taken on"), Story.GetQuestStateOf(TEXT("数数")), EDnaQuestState::Accepted);
	World.SetExtern(TEXT("别的"), FDnaValue::FromInt(1));
	TestEqual(TEXT("still taken on after an unrelated write"), Story.GetQuestStateOf(TEXT("数数")), EDnaQuestState::Accepted);

	// Progress counts from what the save holds, so the very first change after a load is progress.
	Ledger.ApplySnapshot(Save);
	World.SetExtern(TEXT("数"), FDnaValue::FromInt(3));
	TestEqual(TEXT("the first change after a load makes it going"), Story.GetQuestStateOf(TEXT("数数")), EDnaQuestState::Going);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaQuestHiddenAtStartTest, "DreamNarrative.Quest.HiddenStartsWithTheStory", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDnaQuestHiddenAtStartTest::RunTest(const FString& Parameters)
{
	using namespace UE::DreamNarrative::Tests;

	FDnaTestWorld World(*this);
	if (!World.CompileText(TEXT(
		"fact 线索: bool extern\n"
		"quest 记忆\n"
		"  priority: hidden\n"
		"  objective 线索\n"
		"quest 支线\n"
		"  objective 线索\n")))
	{
		return false;
	}
	UDreamNarrativeStory& Story = World.Story();

	// Nothing has been written yet: the hidden quest runs from the start, the other waits to be taken on.
	TestEqual(TEXT("the hidden quest is running"), Story.GetQuestStateOf(TEXT("记忆")), EDnaQuestState::Accepted);
	TestEqual(TEXT("the side quest is not"), Story.GetQuestStateOf(TEXT("支线")), EDnaQuestState::Inactive);
	World.SetExtern(TEXT("线索"), FDnaValue::FromBool(true));
	TestEqual(TEXT("and completes on its own"), Story.GetQuestStateOf(TEXT("记忆")), EDnaQuestState::Completed);
	return !HasAnyErrors();
}

namespace UE::DreamNarrative::Tests
{
	/** Counts one trace event. */
	class FDnaEventCounter : public IDnaTrace
	{
	public:
		explicit FDnaEventCounter(const TCHAR* InEvent) : Event(InEvent) {}
		virtual void OnEvent(const TCHAR* Name) override { Count += FCString::Strcmp(Name, Event) == 0 ? 1 : 0; }
		const TCHAR* Event;
		int32 Count = 0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaQuestUpdateTest, "DreamNarrative.Quest.UpdatedWhenWhatItShowsMoves", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDnaQuestUpdateTest::RunTest(const FString& Parameters)
{
	using namespace UE::DreamNarrative::Tests;

	// Declared first so it outlives the world it listens to.
	FDnaEventCounter Updates(TEXT("quest.update"));
	FDnaTestWorld World(*this);
	if (!World.CompileText(TEXT(
		"fact 狼: int extern\n"
		"fact 灯: int extern\n"
		"fact 天气: int extern\n"
		"quest 猎狼\n"
		"  journal\n"
		"    going: 天黑前回来,灯还剩 {灯}。\n"
		"  objective 狼 >= 5\n"
		"    show: 猎狼 {狼}/5\n")))
	{
		return false;
	}
	UDreamNarrativeStory& Story = World.Story();
	World.Ledger().SetTrace(&Updates);

	Story.AcceptQuest(TEXT("猎狼"));
	TestEqual(TEXT("taking it on is a state change, not an update"), Updates.Count, 0);
	World.SetExtern(TEXT("狼"), FDnaValue::FromInt(1));
	TestEqual(TEXT("the first wolf makes it going, once"), Updates.Count, 1);
	World.SetExtern(TEXT("狼"), FDnaValue::FromInt(2));
	TestEqual(TEXT("the count it shows moved"), Updates.Count, 2);
	World.SetExtern(TEXT("灯"), FDnaValue::FromInt(3));
	TestEqual(TEXT("the journal reads it"), Updates.Count, 3);
	World.SetExtern(TEXT("天气"), FDnaValue::FromInt(1));
	TestEqual(TEXT("nothing it shows reads the weather"), Updates.Count, 3);
	World.SetExtern(TEXT("狼"), FDnaValue::FromInt(5));
	TestEqual(TEXT("the objective got done"), Updates.Count, 4);
	TestEqual(TEXT("and the quest with it"), Story.GetQuestStateOf(TEXT("猎狼")), EDnaQuestState::Completed);
	World.SetExtern(TEXT("狼"), FDnaValue::FromInt(6));
	TestEqual(TEXT("a completed quest is not updated"), Updates.Count, 4);

	World.Ledger().SetTrace(nullptr);
	return !HasAnyErrors();
}

#endif
