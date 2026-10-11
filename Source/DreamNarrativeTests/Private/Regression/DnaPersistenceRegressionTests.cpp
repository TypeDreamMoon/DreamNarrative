// Copyright DreamMoon. All Rights Reserved.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "DreamNarrativeLedger.h"
#include "DreamNarrativeStory.h"
#include "DreamNarrativeScript.h"
#include "DreamNarrativeQuest.h"
#include "Fixtures/DnaTestWorld.h"
#include "Generation/DnaGenerator.h"
#include "Lang/DnaProject.h"
#include "Misc/Guid.h"
#include "Lang/DnaEditSession.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "HAL/FileManager.h"

using namespace UE::DreamNarrative::Tests;
using namespace UE::DreamNarrative::Lang;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaRegressionProfileLoad, "DreamNarrative.Regression.Persistence.ProfileLoadDoesNotAdvance", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDnaRegressionProfileLoad::RunTest(const FString& Parameters)
{
	FDnaTestWorld World(*this);
	if (!World.CompileText(TEXT(
		"fact Flags profile\n"
		"  Unlocked: bool\n"
		"chapter First\n"
		"  advance -> Second if Flags.Unlocked\n"
		"chapter Second after First\n"))) { return false; }
	World.Ledger().SetChapter(TEXT("First"));
	FDnaProfileSnapshot Profile;
	FDnaFactSnapshotEntry& Entry = Profile.Facts.AddDefaulted_GetRef();
	Entry.Fact = TEXT("Flags.Unlocked");
	Entry.Value = FDnaValue::FromBool(true);
	World.Ledger().ApplyProfileSnapshot(Profile);
	TestTrue(TEXT("profile value restored"), World.Ledger().GetFactAsBool(TEXT("Flags.Unlocked")));
	TestEqual(TEXT("loading profile must preserve current chapter"), World.Ledger().GetChapter(), FName(TEXT("First")));
	TestEqual(TEXT("loading profile must not create a checkpoint"), World.Ledger().GetChapterCheckpoints().Num(), 1);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaRegressionRenamedCheckpoint, "DreamNarrative.Regression.Persistence.RenamedChapterCheckpoint", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDnaRegressionRenamedCheckpoint::RunTest(const FString& Parameters)
{
	FDnaTestWorld Before(*this), After(*this);
	if (!Before.CompileText(TEXT("chapter OldChapter\n")) ||
		!After.CompileText(TEXT("chapter NewChapter was OldChapter\n"))) { return false; }
	Before.Ledger().SetChapter(TEXT("OldChapter"));
	const FDnaLedgerSnapshot Save = Before.Ledger().MakeSnapshot();
	After.Ledger().ApplySnapshot(Save);
	TestEqual(TEXT("current chapter migrated"), After.Ledger().GetChapter(), FName(TEXT("NewChapter")));
	TestTrue(TEXT("checkpoint is selectable after chapter rename"), After.Ledger().SelectChapter(TEXT("NewChapter")));
	TestTrue(TEXT("checkpoint names use the renamed chapter"), After.Ledger().GetChapterCheckpoints().Contains(TEXT("NewChapter")));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaRegressionRenamedQuestTimer, "DreamNarrative.Regression.Persistence.RenamedQuestTimer", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDnaRegressionRenamedQuestTimer::RunTest(const FString& Parameters)
{
	FDnaTestWorld Before(*this), After(*this);
	if (!Before.CompileText(TEXT("quest OldQuest\n  timeout: 10s\n")) ||
		!After.CompileText(TEXT("quest NewQuest was OldQuest\n  timeout: 10s\n"))) { return false; }
	TestTrue(TEXT("old quest accepted"), Before.Story().AcceptQuest(TEXT("OldQuest")));
	Before.Clock().Advance(3.0);
	const FDnaLedgerSnapshot Save = Before.Ledger().MakeSnapshot();
	After.Ledger().ApplySnapshot(Save);
	TestEqual(TEXT("quest state migrated"), After.Story().GetQuestStateOf(TEXT("NewQuest")), EDnaQuestState::Accepted);
	TestEqual(TEXT("remaining timeout migrated"), After.Ledger().GetTimerRemaining(::DreamNarrative::QuestStateFact(TEXT("NewQuest"))), 7.0);
	After.Clock().Advance(8.0);
	TestEqual(TEXT("renamed quest still times out"), After.Story().GetQuestStateOf(TEXT("NewQuest")), EDnaQuestState::TimedOut);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaRegressionRenamedDialogue, "DreamNarrative.Regression.Persistence.RenamedDialogueOnce", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDnaRegressionRenamedDialogue::RunTest(const FString& Parameters)
{
	FDnaTestWorld Before(*this), After(*this);
	if (!Before.CompileText(TEXT("character Speaker\ndialogue OldIntro for Speaker once\n  Speaker: Hello.\n")) ||
		!After.CompileText(TEXT("character Speaker\ndialogue NewIntro was OldIntro for Speaker once\n  Speaker: Hello.\n"))) { return false; }
	if (!Before.Talk(TEXT("Speaker"))) { return false; }
	TestEqual(TEXT("old dialogue visited"), Before.Ledger().GetVisited(TEXT("OldIntro")), 1);
	After.Ledger().ApplySnapshot(Before.Ledger().MakeSnapshot());
	TestEqual(TEXT("visit count migrated"), After.Ledger().GetVisited(TEXT("NewIntro")), 1);
	TestTrue(TEXT("consumed once dialogue must not be offered again"), After.Story().SelectEntry(TEXT("Speaker")).IsNone());
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaRegressionSameBasename, "DreamNarrative.Regression.Persistence.SameBasenameAssets", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDnaRegressionSameBasename::RunTest(const FString& Parameters)
{
	FDnaGenerateOptions Options;
	Options.bSave = false;
	Options.bTransient = true;
	Options.AssetDirectory = TEXT("/Game/DnaRegression_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	FDnaProgram Program;
	TArray<UDreamNarrativeScript*> Scripts;
	const TArray<FDnaTestSource> Sources = {
		{ TEXT("ChapterA/Intro.dna"), TEXT("fact ChapterAFlag: bool extern\n") },
		{ TEXT("ChapterB/Intro.dna"), TEXT("fact ChapterBFlag: bool extern\n") }
	};
	if (!CompileDnaSources(*this, Sources, Options, Program, Scripts)) { return false; }
	Options.bTransient = false;
	FDnaGenerateResult Result;
	FDnaDiagnosticSink Diagnostics;
	TestFalse(TEXT("colliding packages rejected before generation"), GenerateDnaProgram(Program, Options, Result, Diagnostics));
	TestEqual(TEXT("no assets generated on conflict"), Result.Scripts.Num(), 0);
	if (TestNotNull(TEXT("collision diagnostic"), Diagnostics.FirstError()))
	{
		TestEqual(TEXT("collision diagnostic code"), Diagnostics.FirstError()->Code, FString(TEXT("DNS8001")));
	}
	// Separate incremental compiles must protect an existing asset too.
	FDnaProgram First;
	TArray<UDreamNarrativeScript*> FirstScripts;
	if (!CompileDnaSources(*this, { Sources[0] }, Options, First, FirstScripts)) { return false; }
	if (!TestEqual(TEXT("first asset created"), FirstScripts.Num(), 1)) { return false; }
	Options.bTransient = true;
	FDnaProgram Second;
	if (!CompileDnaSources(*this, { Sources[1] }, Options, Second, Scripts)) { return false; }
	Options.bTransient = false;
	FDnaGenerateResult SecondResult;
	FDnaDiagnosticSink SecondDiagnostics;
	TestFalse(TEXT("incremental collision rejected"), GenerateDnaProgram(Second, Options, SecondResult, SecondDiagnostics));
	TestEqual(TEXT("existing source ownership retained"), FirstScripts[0]->SourcePath, Sources[0].Path);
	TestTrue(TEXT("existing fact retained"), FirstScripts[0]->Facts.ContainsByPredicate([](const FDnaFactDecl& Decl) { return Decl.Name == TEXT("ChapterAFlag"); }));
	FDnaGenerateResult Rebuild;
	FDnaDiagnosticSink RebuildDiagnostics;
	TestTrue(TEXT("same source may rebuild its asset"), GenerateDnaProgram(First, Options, Rebuild, RebuildDiagnostics));

	// An asset generated in another checkout, or from a file that moved away, is not a collision.
	const FString Root = GetDnaSourceRoot();
	TestTrue(TEXT("another checkout's source is stale"), IsStaleDnaSource(TEXT("Z:/ElsewhereCheckout/Narrative/Intro.dna"), Root / TEXT("ChapterB/Intro.dna")));
	TestTrue(TEXT("a moved-away source is stale"), IsStaleDnaSource(Root / TEXT("Gone_") + FGuid::NewGuid().ToString(EGuidFormats::Digits) / TEXT("Intro.dna"), Root / TEXT("ChapterB/Intro.dna")));
	TestFalse(TEXT("the same source is not stale"), IsStaleDnaSource(Root / TEXT("ChapterB/Intro.dna"), Root / TEXT("ChapterB/../ChapterB/Intro.dna")));
	FirstScripts[0]->SourcePath = TEXT("Z:/ElsewhereCheckout/Narrative/ChapterA/Intro.dna");
	FDnaGenerateResult Takeover;
	FDnaDiagnosticSink TakeoverDiagnostics;
	TestTrue(TEXT("a source from another checkout takes its asset over"), GenerateDnaProgram(Second, Options, Takeover, TakeoverDiagnostics));
	TestEqual(TEXT("takeover records the new source"), FirstScripts[0]->SourcePath, Sources[1].Path);
	TestTrue(TEXT("takeover rebuilds the content"), FirstScripts[0]->Facts.ContainsByPredicate([](const FDnaFactDecl& Decl) { return Decl.Name == TEXT("ChapterBFlag"); }));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaRegressionChoiceRename, "DreamNarrative.Regression.Persistence.RenamedChoiceAndCheckpoint", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDnaRegressionChoiceRename::RunTest(const FString& Parameters)
{
	FDnaTestWorld Before(*this), After(*this);
	if (!Before.CompileText(TEXT("chapter OldChapter\ndialogue OldIntro\n  - Pick. once\n  - Stable. once #stable_pick\n  - Leave.\n")) ||
		!After.CompileText(TEXT("chapter NewChapter was OldChapter\ndialogue NewIntro was OldIntro\n  - Pick. once\n  - Stable. once #stable_pick\n  - Leave.\n"))) { return false; }
	Before.Ledger().SetVisited(TEXT("OldIntro"), 2);
	Before.Ledger().MarkChosenOnce(TEXT("OldIntro.c1"));
	Before.Ledger().MarkChosenOnce(TEXT("stable_pick"));
	Before.Ledger().SetChapter(TEXT("OldChapter"));
	After.Ledger().ApplySnapshot(Before.Ledger().MakeSnapshot());
	TestTrue(TEXT("generated choice id follows renamed dialogue"), After.Ledger().WasChosenOnce(TEXT("NewIntro.c1")));
	TestTrue(TEXT("explicit choice id remains stable"), After.Ledger().WasChosenOnce(TEXT("stable_pick")));
	TestTrue(TEXT("old lookup still resolves"), After.Ledger().WasChosenOnce(TEXT("OldIntro.c1")));
	TestEqual(TEXT("old visit lookup resolves"), After.Ledger().GetVisited(TEXT("OldIntro")), 2);
	After.Ledger().SetVisited(TEXT("NewIntro"), 10);
	TestTrue(TEXT("renamed checkpoint selectable"), After.Ledger().SelectChapter(TEXT("NewChapter")));
	TestEqual(TEXT("checkpoint visit keys migrated"), After.Ledger().GetVisited(TEXT("NewIntro")), 2);
	TestTrue(TEXT("checkpoint choice keys migrated"), After.Ledger().WasChosenOnce(TEXT("NewIntro.c1")));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaRegressionSessionSave, "DreamNarrative.Regression.Persistence.SessionSave", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDnaRegressionSessionSave::RunTest(const FString& Parameters)
{
	const FString Path = FPaths::ProjectIntermediateDir() / (TEXT("DnaSave_") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".dna"));
	ON_SCOPE_EXIT { IFileManager::Get().Delete(*Path); };
	const FString Source = TEXT("dialogue Intro\n");
	for (bool bBom : { false, true })
	{
		if (!TestTrue(TEXT("create source"), FFileHelper::SaveStringToFile(Source, *Path, bBom ? FFileHelper::EEncodingOptions::ForceUTF8 : FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))) { return false; }
		FDnaEditSession Session;
		Session.Open(Path, Source);
		FText Error;
		TestTrue(TEXT("edit session"), Session.SetDialogueCondition(TEXT("Intro"), TEXT("true"), Error));
		TestTrue(TEXT("save unchanged disk succeeds"), Session.Save(Error));
		TestFalse(TEXT("successful save clears dirty state"), Session.IsDirty());
		TArray<uint8> Bytes;
		TestTrue(TEXT("read saved bytes"), FFileHelper::LoadFileToArray(Bytes, *Path));
		TestEqual(TEXT("UTF-8 BOM preserved"), Bytes.Num() >= 3 && Bytes[0] == 0xEF && Bytes[1] == 0xBB && Bytes[2] == 0xBF, bBom);
		TestTrue(TEXT("edit after save"), Session.SetDialogueCondition(TEXT("Intro"), TEXT("false"), Error));
		const FString Local = Session.GetText();
		TestTrue(TEXT("external writer"), FFileHelper::SaveStringToFile(Source + TEXT("// external\n"), *Path));
		TestFalse(TEXT("external change refuses save"), Session.Save(Error));
		TestTrue(TEXT("conflict keeps dirty state"), Session.IsDirty());
		TestEqual(TEXT("conflict keeps local edits"), Session.GetText(), Local);
		TestTrue(TEXT("remove own scratch file"), IFileManager::Get().Delete(*Path));
		TestFalse(TEXT("missing source refuses save"), Session.Save(Error));
		TestTrue(TEXT("missing source is not recreated"), !IFileManager::Get().FileExists(*Path));
	}
	return !HasAnyErrors();
}
#endif
