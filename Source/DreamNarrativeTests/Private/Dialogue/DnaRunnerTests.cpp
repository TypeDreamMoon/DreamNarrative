// Copyright DreamMoon. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DreamNarrativeDialogueRunner.h"
#include "DreamNarrativeLedger.h"
#include "DreamNarrativeStory.h"
#include "Fixtures/DnaTestWorld.h"

// ---------------------------------------------------------------- wait

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaWaitRunsOnTheClockTest, "DreamNarrative.Dialogue.WaitRunsOnTheClock", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDnaWaitRunsOnTheClockTest::RunTest(const FString& Parameters)
{
	using namespace UE::DreamNarrative::Tests;

	FDnaTestWorld World(*this);
	const bool bCompiled = World.CompileText(TEXT(
		"character 甲\n"
		"dialogue 等待 for 甲\n"
		"  甲: 一。\n"
		"  wait 20m\n"
		"  甲: 二。\n"
		"  wait 1s\n"
		"  甲: 三。\n"));
	if (!bCompiled)
	{
		return false;
	}

	UDreamNarrativeDialogueRunner* Runner = World.Story().StartDialogue(TEXT("等待"));
	if (!TestNotNull(TEXT("runner"), Runner))
	{
		return false;
	}
	Runner->Advance();
	TestEqual(TEXT("waiting after the first line"), Runner->GetState(), EDnaRunnerState::Waiting);
	TestEqual(TEXT("the wait is on the clock"), World.Clock().GetTimeToNext(), 1200.0);

	World.Clock().Advance(1199.0);
	TestEqual(TEXT("still waiting a second early"), Runner->GetState(), EDnaRunnerState::Waiting);
	World.Clock().Advance(1.0);
	TestEqual(TEXT("the line after the wait"), Runner->GetState(), EDnaRunnerState::ShowingLine);
	TestEqual(TEXT("which is the second"), Runner->GetCurrentLine().Text.ToString(), FString(TEXT("二。")));

	// Ending a wait by hand leaves nothing on the clock to end the next one early.
	Runner->Advance();
	TestEqual(TEXT("second wait"), Runner->GetState(), EDnaRunnerState::Waiting);
	Runner->NotifyWaitFinished();
	TestEqual(TEXT("ended by hand"), Runner->GetState(), EDnaRunnerState::ShowingLine);
	TestEqual(TEXT("nothing left on the clock"), World.Clock().NumPending(), 0);

	// Aborting during a wait cancels it too.
	UDreamNarrativeDialogueRunner* Second = World.Story().StartDialogue(TEXT("等待"));
	if (!TestNotNull(TEXT("second runner"), Second))
	{
		return false;
	}
	Second->Advance();
	TestEqual(TEXT("one wait pending"), World.Clock().NumPending(), 1);
	Second->Abort();
	TestEqual(TEXT("abort cancels it"), World.Clock().NumPending(), 0);
	return true;
}

// ---------------------------------------------------------------- batches

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaSetsCommitAsOneBatchTest, "DreamNarrative.Dialogue.SetsCommitAsOneBatch", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDnaSetsCommitAsOneBatchTest::RunTest(const FString& Parameters)
{
	using namespace UE::DreamNarrative::Tests;

	FDnaTestWorld World(*this);
	const bool bCompiled = World.CompileText(TEXT(
		"character 甲\n"
		"fact 一: int\n"
		"fact 二: int\n"
		"fact 三: bool\n"
		"dialogue 批 for 甲\n"
		"  set 一 = 1\n"
		"  set 二 = 2\n"
		"  set 三 = true\n"
		"  甲: 好。\n"
		"  set 一 += 1\n"));
	if (!bCompiled)
	{
		return false;
	}

	// Observers subscribe to committed batches; each batch lists what changed in it.
	TArray<FString> Batches;
	TArray<FString> SeenAtFirstCommit;
	const FDelegateHandle Handle = World.Ledger().OnBatchCommitted.AddLambda([&](const TArray<FName>& Changed)
	{
		TArray<FString> Names;
		for (const FName& Name : Changed) { Names.Add(Name.ToString()); }
		Batches.Add(FString::Join(Names, TEXT(",")));
		if (Batches.Num() == 1)
		{
			// Everything the batch wrote is visible when observers hear about it.
			SeenAtFirstCommit.Add(World.Ledger().GetFact(TEXT("一")).ToString());
			SeenAtFirstCommit.Add(World.Ledger().GetFact(TEXT("二")).ToString());
			SeenAtFirstCommit.Add(World.Ledger().GetFact(TEXT("三")).ToString());
		}
	});

	World.Talk(TEXT("甲"));
	World.Ledger().OnBatchCommitted.Remove(Handle);

	TestEqual(TEXT("three sets in a row commit once, the later one on its own"), FString::Join(Batches, TEXT(" | ")), FString(TEXT("一,二,三 | 一")));
	TestEqual(TEXT("observers see the whole batch"), FString::Join(SeenAtFirstCommit, TEXT(" ")), FString(TEXT("1 2 true")));
	World.Expect(TEXT("一 == 2 and 二 == 2 and 三"));
	return !HasAnyErrors();
}

#endif
