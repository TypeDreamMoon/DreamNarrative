// Copyright DreamMoon. All Rights Reserved.
//
//   DreamNarrative.Project.<path>.<name>   every test block under the project's content root, Tests/;
//                                          a recording (Tests/Recorded/) that plays differently now
//                                          leaves a .actual beside it and reports the difference
//   DreamNarrative.Replay.*                recording in the logic layer: what the recorder writes
//                                          plays back, and a changed story shows up as a difference

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DreamNarrativeLedger.h"
#include "DreamNarrativeRecorder.h"
#include "DreamNarrativeStory.h"
#include "Fixtures/DnaLogCapture.h"
#include "Fixtures/DnaTestWorld.h"
#include "HAL/FileManager.h"
#include "Lang/DnaModel.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Replay/DnaReplay.h"
#include "Spec/DnaSpecSuite.h"
#include "Spec/DnaTestBlocks.h"

/** What the story logs is reported with the rest of a test's failures, once, in order. */
class FDnaCollectingTestBase : public FAutomationTestBase
{
public:
	FDnaCollectingTestBase(const FString& InName, bool bInComplexTask)
		: FAutomationTestBase(InName, bInComplexTask)
	{
	}

	virtual bool SuppressLogErrors() override { return true; }
	virtual bool SuppressLogWarnings() override { return true; }
};

// ---------------------------------------------------------------- the project's tests

IMPLEMENT_CUSTOM_COMPLEX_AUTOMATION_TEST(FDnaProjectTest, FDnaCollectingTestBase, "DreamNarrative.Project", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

void FDnaProjectTest::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	using namespace UE::DreamNarrative::Tests;
	const FString Root = Replay::GetContentRoot();
	for (const FString& Relative : Replay::FindProjectTestFiles())
	{
		FString Display = FPaths::ChangeExtension(Relative, TEXT(""));
		Display.ReplaceInline(TEXT("."), TEXT("_"));
		Display.ReplaceInline(TEXT("/"), TEXT("."));
		for (const FString& Name : Spec::ScanBlockNamesInFile(Root / Relative, TEXT("test")))
		{
			OutBeautifiedNames.Add(Display + TEXT(".") + Name);
			OutTestCommands.Add(Relative + TEXT("|") + Name);
		}
	}
}

bool FDnaProjectTest::RunTest(const FString& Parameters)
{
	using namespace UE::DreamNarrative::Tests;
	using namespace UE::DreamNarrative::Lang;

	FString Relative, BlockName;
	if (!Parameters.Split(TEXT("|"), &Relative, &BlockName))
	{
		AddError(FString::Printf(TEXT("Malformed test command '%s'"), *Parameters));
		return false;
	}
	const FString Path = Replay::GetContentRoot() / Relative;
	const FString ActualPath = Path + TEXT(".actual");

	FDnaTestWorld World(*this);
	if (!Replay::CompileContentRoot(World))
	{
		return false;
	}
	const TArray<FString> Failures = Replay::RunCollecting(World, BlockName, Relative);
	for (const FString& Failure : Failures)
	{
		AddError(Failure);
	}
	if (Failures.Num() == 0)
	{
		// Green again: an .actual left from an earlier red run is out of date.
		IFileManager::Get().Delete(*ActualPath, /*RequireExists=*/false, /*EvenReadOnly=*/true, /*Quiet=*/true);
		return true;
	}
	if (!Relative.StartsWith(TEXT("Tests/Recorded/")))
	{
		return false;
	}

	// A recording: play it again with a recorder attached and show what changed.
	FDnaTestWorld Again(*this);
	if (!Replay::CompileContentRoot(Again))
	{
		return false;
	}
	const FDnaTestInfo* Info = Again.Program().Tests.Find(BlockName);
	FString Recorded;
	if (!Info || !Info->Node || !FFileHelper::LoadFileToString(Recorded, *Path))
	{
		return false;
	}
	const FString Actual = Replay::Rerecord(Again, *Info->Node, Relative);
	FFileHelper::SaveStringToFile(Actual, *ActualPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	const TArray<FString> Diff = Replay::DiffLines(Recorded, Actual);
	AddError(FString::Printf(TEXT("%s plays differently now. What was recorded (-) and what happens now (+):\n%s\n"
		"The recording as the story plays now is in %s; if the story changed on purpose, put it in place of the recording."),
		*Relative, Diff.Num() > 0 ? *FString::Join(Diff, TEXT("\n")) : TEXT("  (the same lines; see the failures above)"), *ActualPath));
	return false;
}

// ---------------------------------------------------------------- recording in the logic layer

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaRecordAndReplayTest, "DreamNarrative.Replay.RecordAndReplay", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDnaRecordAndReplayTest::RunTest(const FString& Parameters)
{
	using namespace UE::DreamNarrative::Tests;

	const TCHAR* Ring = TEXT("Tests/Corpus/Validate/Samples/ring.dna");
	TArray<FDnaTestSource> Story;
	if (!ReadPluginSources(*this, { Ring }, Story))
	{
		return false;
	}

	// -- play: the first water change and the once-choice happen before recording starts
	FDnaTestWorld Played(*this);
	if (!Played.Compile(Story))
	{
		return false;
	}
	Played.Talk(TEXT("凛"));
	Played.SetExtern(TEXT("周期"), FDnaValue::FromInt(1));
	Played.Talk(TEXT("凛"), { TEXT("你在等什么?") });
	Played.Story().StartRecording(TEXT("凛与店主"));
	Played.Talk(TEXT("凛"), { TEXT("没事。") });
	Played.SetExtern(TEXT("周期"), FDnaValue::FromInt(3));
	Played.Talk(TEXT("凛"));
	Played.Talk(TEXT("店主"), { TEXT("打听那株梅") });
	const FString Recorded = Played.Story().StopRecording();

	// -- what it wrote: the start it found, each move, what changed
	for (const TCHAR* Expected : {
		TEXT("test 凛与店主"), TEXT("    换水次数 = 1"), TEXT("    visited(凛_日常) = 1"), TEXT("    once #凛_日常.c1"), TEXT("    chapter = 第一章"),
		TEXT("  talk 凛\n    凛: 嗯。\n    - 没事。"), TEXT("  set 周期 = 3"), TEXT("  talk 凛\n    凛: 今天也换吧。"),
		TEXT("    - 打听那株梅\n    店主: 你屋里那株?以前也有人这么养过。\n      养到第四十七天,人走了。"),
		TEXT("  expect 店主提到过梅"), TEXT("  expect 换水次数 == 2"), TEXT("  expect chapter == 第一章") })
	{
		TestTrue(FString::Printf(TEXT("the recording has \"%s\""), Expected), Recorded.Contains(Expected));
	}

	// -- it plays back, from a fresh game, through the start it wrote down
	{
		FDnaTestWorld Replayed(*this);
		TArray<FDnaTestSource> Sources = Story;
		Sources.Add({ TEXT("Recorded.dna"), Recorded });
		if (Replayed.Compile(Sources))
		{
			const TArray<FString> Failures = Replay::RunCollecting(Replayed, TEXT("凛与店主"), TEXT("Recorded.dna"));
			TestEqual(TEXT("the recording plays back"), FString::Join(Failures, TEXT("\n")), FString());
		}
	}

	// -- a line changed since: the replay goes red, and playing it again shows the difference
	{
		TArray<FDnaTestSource> Changed = Story;
		const int32 Replaced = Changed[0].Text.ReplaceInline(TEXT("店主: 又来了。"), TEXT("店主: 你又来了。"));
		TestEqual(TEXT("the line to change is in the sample"), Replaced, 1);
		Changed.Add({ TEXT("Recorded.dna"), Recorded });

		FDnaTestWorld Replayed(*this);
		if (Replayed.Compile(Changed))
		{
			const TArray<FString> Failures = Replay::RunCollecting(Replayed, TEXT("凛与店主"), TEXT("Recorded.dna"));
			TestTrue(TEXT("the replay fails on the changed line"), Failures.ContainsByPredicate([](const FString& Failure) { return Failure.Contains(TEXT("店主: 又来了。")); }));
		}
		FDnaTestWorld Again(*this);
		if (Again.Compile(Changed))
		{
			const FString Actual = Replay::Rerecord(Again, *Again.Program().Tests.FindChecked(TEXT("凛与店主")).Node, TEXT("Recorded.dna"));
			const FString Diff = FString::Join(Replay::DiffLines(Recorded, Actual), TEXT("\n"));
			TestTrue(TEXT("the difference shows the line as recorded"), Diff.Contains(TEXT("-     店主: 又来了。")));
			TestTrue(TEXT("and as it is now"), Diff.Contains(TEXT("+     店主: 你又来了。")));
			TestEqual(TEXT("and nothing else"), Replay::DiffLines(Recorded, Actual).FilterByPredicate([](const FString& Line) { return !Line.StartsWith(TEXT("  ")); }).Num(), 2);
		}
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaRecordsQuestsTest, "DreamNarrative.Replay.RecordsQuests", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDnaRecordsQuestsTest::RunTest(const FString& Parameters)
{
	using namespace UE::DreamNarrative::Tests;

	TArray<FDnaTestSource> Story;
	if (!ReadPluginSources(*this, { TEXT("Tests/Corpus/Validate/Samples/rpg.dna") }, Story))
	{
		return false;
	}

	// Recording starts halfway through the wolf hunt, with its clock running.
	FDnaTestWorld Played(*this);
	if (!Played.Compile(Story))
	{
		return false;
	}
	Played.Talk(TEXT("村长"), { TEXT("我去。") });
	Played.SetExtern(TEXT("狼杀了"), FDnaValue::FromInt(3));
	Played.Wait(30.0);
	Played.Story().StartRecording(TEXT("猎狼后半"));
	Played.SetExtern(TEXT("狼杀了"), FDnaValue::FromInt(5));
	Played.Story().FailQuest(TEXT("猎狼_头狼"));
	const FString Recorded = Played.Story().StopRecording();

	for (const TCHAR* Expected : {
		TEXT("    quest 猎狼 = going 1170s"), TEXT("    狼杀了 = 3"), TEXT("    chapter = 第一章"),
		TEXT("  set 狼杀了 = 5"), TEXT("  fail 猎狼_头狼"),
		TEXT("  expect 猎狼.completed"), TEXT("  expect 猎狼_头狼.failed"), TEXT("  expect chapter == 第二章") })
	{
		TestTrue(FString::Printf(TEXT("the recording has \"%s\""), Expected), Recorded.Contains(Expected));
	}
	TestFalse(TEXT("the quests' own facts are not written as facts"), Recorded.Contains(TEXT("@")));

	FDnaTestWorld Replayed(*this);
	TArray<FDnaTestSource> Sources = Story;
	Sources.Add({ TEXT("Recorded.dna"), Recorded });
	if (Replayed.Compile(Sources))
	{
		const TArray<FString> Failures = Replay::RunCollecting(Replayed, TEXT("猎狼后半"), TEXT("Recorded.dna"));
		TestEqual(TEXT("the recording plays back"), FString::Join(Failures, TEXT("\n")), FString());
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaRecordsDrawsTest, "DreamNarrative.Replay.RecordsDraws", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDnaRecordsDrawsTest::RunTest(const FString& Parameters)
{
	using namespace UE::DreamNarrative::Tests;

	const FString Story = TEXT(
		"character 甲\n"
		"fact 点数: int\n"
		"fact 正面次数: int\n"
		"dialogue 掷 for 甲\n"
		"  set 点数 = random(1, 100)\n"
		"  if chance(50%)\n"
		"    甲: 正面。\n"
		"    set 正面次数 += 1\n"
		"  else\n"
		"    甲: 反面。\n");

	// Played on a seed of its own; the replay runs on the test world's, and must not depend on it.
	FDnaTestWorld Played(*this);
	if (!Played.CompileText(Story, TEXT("Draws.dna")))
	{
		return false;
	}
	Played.Ledger().SetRandomSeed(20260924);
	Played.Story().StartRecording(TEXT("掷三次"));
	Played.Talk(TEXT("甲"));
	Played.Talk(TEXT("甲"));
	Played.Talk(TEXT("甲"));
	const FString Recorded = Played.Story().StopRecording();

	int32 Rolls = 0;
	TArray<FString> Lines;
	Recorded.ParseIntoArrayLines(Lines);
	for (const FString& Line : Lines)
	{
		Rolls += Line.StartsWith(TEXT("  rolls ")) ? 1 : 0;
	}
	TestEqual(TEXT("each talk's two draws are written before it"), Rolls, 3);

	FDnaTestWorld Replayed(*this);
	if (Replayed.Compile({ { TEXT("Draws.dna"), Story }, { TEXT("Recorded.dna"), Recorded } }))
	{
		const TArray<FString> Failures = Replay::RunCollecting(Replayed, TEXT("掷三次"), TEXT("Recorded.dna"));
		TestEqual(TEXT("the same draws come out on replay"), FString::Join(Failures, TEXT("\n")), FString());
		TestEqual(TEXT("to the same points"), Replayed.Ledger().GetFactAsInt(TEXT("点数")), Played.Ledger().GetFactAsInt(TEXT("点数")));
	}
	return !HasAnyErrors();
}

#endif
