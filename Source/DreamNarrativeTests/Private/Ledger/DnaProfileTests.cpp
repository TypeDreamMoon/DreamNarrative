// Copyright DreamMoon. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DreamNarrativeLedger.h"
#include "DreamNarrativeStory.h"
#include "Fixtures/DnaTestWorld.h"

namespace
{
	const TCHAR* ProfileStory = TEXT(
		"fact 好感: int\n"
		"fact 结局 profile\n"
		"  A: bool\n"
		"fact 鱼: int extern progress\n"
		"chapter 一章\n"
		"  advance -> 二章 if 好感 >= 1\n"
		"chapter 二章 after 一章\n"
		"archive 图鉴 profile\n"
		"  entry if 鱼 >= 1\n"
		"    钓到第一条鱼。\n");
}

// ---------------------------------------------------------------- two snapshots

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaProfileSnapshotTest, "DreamNarrative.Ledger.ProfileSnapshot", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDnaProfileSnapshotTest::RunTest(const FString& Parameters)
{
	using namespace UE::DreamNarrative::Tests;

	FDnaTestWorld World(*this);
	if (!World.CompileText(ProfileStory))
	{
		return false;
	}
	UDreamNarrativeLedger& Ledger = World.Ledger();
	Ledger.SetFact(TEXT("结局.A"), FDnaValue::FromBool(true), EDnaWriteSource::Narrative);
	Ledger.SetFact(TEXT("好感"), FDnaValue::FromInt(3), EDnaWriteSource::Narrative);

	// The save slot's snapshot leaves the profile out; the profile's has only it.
	const FDnaLedgerSnapshot Save = Ledger.MakeSnapshot();
	const FDnaProfileSnapshot Profile = Ledger.MakeProfileSnapshot();
	TestFalse(TEXT("the save leaves the profile out"), Save.Facts.ContainsByPredicate([](const FDnaFactSnapshotEntry& Entry) { return Entry.Fact == TEXT("结局.A"); }));
	TestEqual(TEXT("the profile has its facts and nothing else"), Profile.Facts.Num(), 2);
	TestTrue(TEXT("among them what the profile archive unlocked"), Profile.Facts.ContainsByPredicate([](const FDnaFactSnapshotEntry& Entry)
	{
		return Entry.Fact == ::DreamNarrative::ArchiveFact(TEXT("图鉴"));
	}));

	// Loading a save slot, or clearing it, leaves the profile alone.
	Ledger.ApplySnapshot(FDnaLedgerSnapshot());
	TestTrue(TEXT("loading a save keeps the profile"), Ledger.GetFactAsBool(TEXT("结局.A")));
	TestEqual(TEXT("and puts the save slot back"), Ledger.GetFactAsInt(TEXT("好感")), 0);
	Ledger.ResetToDefaults();
	TestTrue(TEXT("so does going back to defaults"), Ledger.GetFactAsBool(TEXT("结局.A")));

	// A save written before a fact moved to the profile does not overwrite the profile's value.
	FDnaLedgerSnapshot Old;
	Old.SchemaVersion = 1;
	FDnaFactSnapshotEntry& Stale = Old.Facts.AddDefaulted_GetRef();
	Stale.Fact = TEXT("结局.A");
	Stale.Value = FDnaValue::FromBool(false);
	Ledger.ApplySnapshot(Old);
	TestTrue(TEXT("an old save does not reach into the profile"), Ledger.GetFactAsBool(TEXT("结局.A")));

	// The profile goes back on its own; what it does not carry goes to its default.
	Ledger.ApplyProfileSnapshot(FDnaProfileSnapshot());
	TestFalse(TEXT("an empty profile clears it"), Ledger.GetFactAsBool(TEXT("结局.A")));
	Ledger.ApplyProfileSnapshot(Profile);
	TestTrue(TEXT("and the saved one restores it"), Ledger.GetFactAsBool(TEXT("结局.A")));
	return !HasAnyErrors();
}

// ---------------------------------------------------------------- checkpoints

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaCheckpointTest, "DreamNarrative.Ledger.Checkpoints", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDnaCheckpointTest::RunTest(const FString& Parameters)
{
	using namespace UE::DreamNarrative::Tests;

	FDnaTestWorld World(*this);
	if (!World.CompileText(ProfileStory))
	{
		return false;
	}
	UDreamNarrativeLedger& Ledger = World.Ledger();
	TArray<FString> Selected;
	const FDelegateHandle Handle = Ledger.OnChapterSelected.AddLambda([&Selected](FName Chapter) { Selected.Add(Chapter.ToString()); });

	// A teleport and a starting state leave no checkpoint; the story moving on does.
	Ledger.JumpToChapter(TEXT("二章"));
	Ledger.SetChapter(TEXT("一章"), /*bLeaveCheckpoint=*/false);
	TestEqual(TEXT("no checkpoint from a teleport or a starting state"), Ledger.GetChapterCheckpoints().Num(), 0);
	AddExpectedMessagePlain(TEXT("has no checkpoint"), ELogVerbosity::Warning);
	TestFalse(TEXT("so nothing to select"), Ledger.SelectChapter(TEXT("一章")));

	Ledger.StartNewPlaythrough();
	TestEqual(TEXT("a new playthrough enters the first chapter"), Ledger.GetChapter(), FName(TEXT("一章")));
	Ledger.SetFact(TEXT("好感"), FDnaValue::FromInt(1), EDnaWriteSource::Narrative);
	TestEqual(TEXT("the rule moved the story on"), Ledger.GetChapter(), FName(TEXT("二章")));
	TestEqual(TEXT("leaving a checkpoint each time"), FString::JoinBy(Ledger.GetChapterCheckpoints(), TEXT(", "), [](const FName& Name) { return Name.ToString(); }),
		FString(TEXT("一章, 二章")));

	// The checkpoints go into the save and come back with it.
	const FDnaLedgerSnapshot Saved = Ledger.MakeSnapshot();
	TestEqual(TEXT("saved with the slot"), Saved.Checkpoints.Num(), 2);
	Ledger.ApplySnapshot(FDnaLedgerSnapshot());
	TestEqual(TEXT("an empty save has none"), Ledger.GetChapterCheckpoints().Num(), 0);
	Ledger.ApplySnapshot(Saved);
	TestEqual(TEXT("loading brings them back"), Ledger.GetChapterCheckpoints().Num(), 2);

	// Selecting keeps progress: the fish caught since.
	Ledger.SetFact(TEXT("鱼"), FDnaValue::FromInt(5), EDnaWriteSource::Game);
	TestTrue(TEXT("select the first chapter"), Ledger.SelectChapter(TEXT("一章")));
	TestEqual(TEXT("back at it"), Ledger.GetChapter(), FName(TEXT("一章")));
	TestEqual(TEXT("as it was"), Ledger.GetFactAsInt(TEXT("好感")), 0);
	TestEqual(TEXT("progress kept"), Ledger.GetFactAsInt(TEXT("鱼")), 5);
	TestEqual(TEXT("the game heard which"), FString::Join(Selected, TEXT(", ")), FString(TEXT("一章")));
	Ledger.OnChapterSelected.Remove(Handle);
	return !HasAnyErrors();
}

// ---------------------------------------------------------------- archives

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaArchiveUnlockTest, "DreamNarrative.Dialogue.ArchiveUnlocks", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDnaArchiveUnlockTest::RunTest(const FString& Parameters)
{
	using namespace UE::DreamNarrative::Tests;

	FDnaTestWorld World(*this);
	if (!World.CompileText(ProfileStory))
	{
		return false;
	}
	UDreamNarrativeStory& Story = World.Story();
	TestEqual(TEXT("nothing unlocked yet"), Story.GetArchive(TEXT("图鉴")).Entries.Num(), 0);
	World.Ledger().SetFact(TEXT("鱼"), FDnaValue::FromInt(1), EDnaWriteSource::Game);
	const FDnaArchiveView View = Story.GetArchive(TEXT("图鉴"));
	TestEqual(TEXT("unlocked when its condition held"), View.Entries.Num(), 1);
	TestEqual(TEXT("its text"), View.Entries.Num() > 0 ? View.Entries[0].ToString() : FString(), FString(TEXT("钓到第一条鱼。")));
	TestEqual(TEXT("out of how many"), View.Total, 1);
	World.Ledger().SetFact(TEXT("鱼"), FDnaValue::FromInt(0), EDnaWriteSource::Game);
	TestTrue(TEXT("and stays unlocked"), Story.IsArchiveEntryUnlocked(TEXT("图鉴"), 0));
	World.Ledger().StartNewPlaythrough();
	TestTrue(TEXT("a profile archive outlives the playthrough"), Story.IsArchiveEntryUnlocked(TEXT("图鉴"), 0));
	return !HasAnyErrors();
}

#endif
