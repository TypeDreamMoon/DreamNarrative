// Copyright DreamMoon. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DreamNarrativeDialogue.h"
#include "DreamNarrativeDialogueRunner.h"
#include "DreamNarrativeLedger.h"
#include "DreamNarrativeScript.h"
#include "DreamNarrativeStory.h"
#include "Engine/GameInstance.h"
#include "Generation/DnaGenerator.h"
#include "Interfaces/IPluginManager.h"
#include "Lang/DnaModel.h"
#include "Lang/DnaParser.h"
#include "Lang/DnaValidator.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

/**
 * The dialogue slice, end to end: the Ring sample is parsed, validated and generated in memory,
 * registered with a fresh ledger, and its dialogue is walked with scripted choices. Facts must move
 * the way the source says, and the entry picked for a character must follow the ledger.
 */
namespace UE::DreamNarrative::RuntimeTests
{
	using namespace UE::DreamNarrative::Lang;

	UDreamNarrativeScript* CompileSample(FAutomationTestBase& Test, const TCHAR* RelativePath)
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("DreamNarrative"));
		if (!Plugin.IsValid())
		{
			Test.AddError(TEXT("Plugin not found"));
			return nullptr;
		}
		const FString Path = FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir() / RelativePath);
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Path))
		{
			Test.AddError(FString::Printf(TEXT("Cannot read %s"), *Path));
			return nullptr;
		}

		FDnaDiagnosticSink Diagnostics(Path);
		TSharedPtr<FDnaDocument> Document = MakeShared<FDnaDocument>();
		ParseDnaDocument(FDnaSourceText(Path, Text), FDnaParseOptions(), *Document, Diagnostics);
		FDnaProgram Program;
		BuildDnaProgram({ Document }, Program, Diagnostics);
		ValidateDnaProgram(Program, Diagnostics);
		FDnaGenerateOptions Options;
		Options.bSave = false;
		Options.bTransient = true;
		FDnaGenerateResult Result;
		GenerateDnaProgram(Program, Options, Result, Diagnostics);

		for (const FDnaDiagnostic& Diagnostic : Diagnostics.GetDiagnostics())
		{
			if (Diagnostic.Severity == EDnaSeverity::Error)
			{
				Test.AddError(FDnaDiagnosticSink::ToLogString(Diagnostic));
			}
		}
		if (Result.Scripts.Num() != 1)
		{
			Test.AddError(TEXT("Expected exactly one script"));
			return nullptr;
		}
		return Result.Scripts[0];
	}

	/** Walks the active runner: lines are advanced, choices are picked by label, until the end. */
	void Walk(FAutomationTestBase& Test, UDreamNarrativeDialogueRunner* Runner, const TArray<FString>& ChoicesByLabel)
	{
		int32 NextChoice = 0;
		for (int32 Guard = 0; Guard < 200 && Runner->GetState() != EDnaRunnerState::Ended; ++Guard)
		{
			switch (Runner->GetState())
			{
			case EDnaRunnerState::ShowingLine:
				Runner->Advance();
				break;
			case EDnaRunnerState::AwaitingChoice:
			{
				const FString Wanted = ChoicesByLabel.IsValidIndex(NextChoice) ? ChoicesByLabel[NextChoice] : FString();
				++NextChoice;
				int32 Pick = 0;
				for (const FDnaChoiceView& Choice : Runner->GetChoices())
				{
					if (Choice.Label.ToString() == Wanted) { Pick = Choice.Index; break; }
				}
				Runner->Choose(Pick);
				break;
			}
			case EDnaRunnerState::Waiting:
				Runner->NotifyWaitFinished();
				break;
			case EDnaRunnerState::PlayingStage:
				Runner->NotifyStageFinished();
				break;
			default:
				Runner->Abort();
				break;
			}
		}
		if (Runner->GetState() != EDnaRunnerState::Ended)
		{
			Test.AddError(TEXT("Dialogue did not end"));
		}
	}

	bool TranscriptHas(const UDreamNarrativeDialogueRunner* Runner, const TCHAR* Needle)
	{
		for (const FString& Entry : Runner->GetTranscript())
		{
			if (Entry.Contains(Needle)) { return true; }
		}
		return false;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaRuntimeDialogueTest, "DreamNarrative.Runtime.Dialogue", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDnaRuntimeDialogueTest::RunTest(const FString& Parameters)
{
	using namespace UE::DreamNarrative::RuntimeTests;

	UDreamNarrativeScript* Script = CompileSample(*this, TEXT("Tests/Corpus/Validate/Samples/ring.dna"));
	if (!Script)
	{
		return false;
	}

	// Subsystems are declared Within a game instance; an uninitialised one is a fine outer for a test.
	UGameInstance* GameInstance = NewObject<UGameInstance>(GetTransientPackage());
	UDreamNarrativeLedger* Ledger = NewObject<UDreamNarrativeLedger>(GameInstance);
	UDreamNarrativeStory* Story = NewObject<UDreamNarrativeStory>(GameInstance);
	Story->InitializeStandalone(Ledger);
	Story->RegisterScript(Script);

	// -- declarations landed
	TestTrue(TEXT("fact declared"), Ledger->HasFact(TEXT("换水次数")));
	TestTrue(TEXT("grouped fact declared"), Ledger->HasFact(TEXT("墨梅册.第一页")));
	TestEqual(TEXT("tag default"), Ledger->GetFact(TEXT("梅蕾状态")).Tag, FName(TEXT("含苞")));
	TestTrue(TEXT("dialogue indexed"), Story->HasDialogue(TEXT("凛_日常")));

	// -- entry selection follows the ledger: no water yet -> the first-water entry wins
	TestEqual(TEXT("first entry"), Story->SelectEntry(TEXT("凛")), FName(TEXT("凛_第一次换水")));

	// -- walk the first-water dialogue: facts move, chapter advances
	UDreamNarrativeDialogueRunner* Runner = Story->TalkTo(TEXT("凛"));
	if (!TestNotNull(TEXT("runner"), Runner)) { return false; }
	Runner->bManualWait = true;
	Walk(*this, Runner, {});
	TestTrue(TEXT("first line shown"), TranscriptHas(Runner, TEXT("凛: 水该换了。")));
	TestTrue(TEXT("player line shown"), TranscriptHas(Runner, TEXT("玩家: 我来。")));
	TestTrue(TEXT("stage requested"), TranscriptHas(Runner, TEXT("[play 换水]")));
	TestEqual(TEXT("water count"), Ledger->GetFactAsInt(TEXT("换水次数")), 1);
	TestEqual(TEXT("bud state"), Ledger->GetFact(TEXT("梅蕾状态")).Tag, FName(TEXT("初绽")));
	TestEqual(TEXT("chapter advanced"), Ledger->GetChapter(), FName(TEXT("第一章")));
	TestEqual(TEXT("visited once"), Ledger->GetVisited(TEXT("凛_第一次换水")), 1);

	// -- now the default entry applies (cycle is 0, so the every-third-cycle entry is out: 0 % 3 == 0 is true!)
	// 周期 is extern and starts at 0, which makes `周期 % 3 == 0` true; bump it so 凛_日常 is picked.
	Ledger->SetFact(TEXT("周期"), FDnaValue::FromInt(1), EDnaWriteSource::Game);
	TestEqual(TEXT("daily entry"), Story->SelectEntry(TEXT("凛")), FName(TEXT("凛_日常")));

	// -- the daily dialogue: first visit line, the once-choice, and the fact it sets
	Runner = Story->TalkTo(TEXT("凛"));
	if (!TestNotNull(TEXT("runner 2"), Runner)) { return false; }
	Runner->bManualWait = true;
	Walk(*this, Runner, { TEXT("你在等什么?") });
	TestTrue(TEXT("first-visit branch"), TranscriptHas(Runner, TEXT("凛: 你回来了。")));
	TestTrue(TEXT("once choice taken"), TranscriptHas(Runner, TEXT("[choose 你在等什么?]")));
	TestTrue(TEXT("choice body ran"), TranscriptHas(Runner, TEXT("凛: ……")));
	TestTrue(TEXT("fact set by choice"), Ledger->GetFactAsBool(TEXT("凛提到过等待")));
	TestFalse(TEXT("late-night line hidden at cycle 1"), TranscriptHas(Runner, TEXT("早点睡")));

	// -- second visit: the else branch, and the once option is gone
	Runner = Story->TalkTo(TEXT("凛"));
	if (!TestNotNull(TEXT("runner 3"), Runner)) { return false; }
	Runner->bManualWait = true;
	Walk(*this, Runner, { TEXT("没事。") });
	TestTrue(TEXT("else branch"), TranscriptHas(Runner, TEXT("凛: 嗯。")));
	TestFalse(TEXT("once option hidden"), TranscriptHas(Runner, TEXT("  - 你在等什么?")));

	// -- snapshot round trip keeps facts, chapter and bookkeeping
	const FDnaLedgerSnapshot Snapshot = Ledger->MakeSnapshot();
	Ledger->ResetToDefaults();
	TestEqual(TEXT("reset water"), Ledger->GetFactAsInt(TEXT("换水次数")), 0);
	Ledger->ApplySnapshot(Snapshot);
	TestEqual(TEXT("restored water"), Ledger->GetFactAsInt(TEXT("换水次数")), 1);
	TestEqual(TEXT("restored chapter"), Ledger->GetChapter(), FName(TEXT("第一章")));
	TestTrue(TEXT("restored once flag"), Ledger->WasChosenOnce(TEXT("凛_日常.c1")));

	// -- debug teleport applies implied facts along the chain
	Ledger->ResetToDefaults();
	TestTrue(TEXT("jump"), Ledger->JumpToChapter(TEXT("终局_花开")));
	TestEqual(TEXT("implied from 第一章"), Ledger->GetFactAsInt(TEXT("换水次数")), 1);
	TestEqual(TEXT("implied from 终局"), Ledger->GetFact(TEXT("梅蕾状态")).Tag, FName(TEXT("盛开")));
	TestTrue(TEXT("implied page"), Ledger->GetFactAsBool(TEXT("墨梅册.第三页")));

	// -- ownership: game code may not write a narrative fact (the refusal logs an error by design)
	AddExpectedError(TEXT("is owned by the narrative"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("ownership enforced"), Ledger->SetFact(TEXT("换水次数"), FDnaValue::FromInt(9), EDnaWriteSource::Game));
	TestTrue(TEXT("extern writable"), Ledger->SetFact(TEXT("周期"), FDnaValue::FromInt(9), EDnaWriteSource::Game));

	return !HasAnyErrors();
}

#endif
