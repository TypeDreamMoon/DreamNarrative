// Copyright DreamMoon. All Rights Reserved.

#include "Fixtures/DnaTestWorld.h"
#include "DreamNarrativeDialogueRunner.h"
#include "DreamNarrativeLedger.h"
#include "DreamNarrativeScript.h"
#include "DreamNarrativeStory.h"
#include "Engine/GameInstance.h"
#include "Generation/DnaGenerator.h"
#include "Interfaces/IPluginManager.h"
#include "Lang/DnaExpr.h"
#include "Lang/DnaParser.h"
#include "Lang/DnaValidator.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"

namespace UE::DreamNarrative::Tests
{
	using namespace UE::DreamNarrative::Lang;

	FString GetPluginDir()
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("DreamNarrative"));
		return Plugin.IsValid() ? FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir()) : FString();
	}

	namespace
	{
		FString DescribeValue(const FDnaValue& Value)
		{
			return FString::Printf(TEXT("%s (%s)"), *Value.ToString(), ::DreamNarrative::LexToString(Value.Type));
		}

		bool SameValue(const FDnaValue& A, const FDnaValue& B)
		{
			return A.Type == B.Type && A.Equals(B);
		}

		FString DescribeChoices(const UDreamNarrativeDialogueRunner& Runner)
		{
			TArray<FString> Labels;
			for (const FDnaChoiceView& Choice : Runner.GetChoices())
			{
				Labels.Add(FString::Printf(TEXT("\"%s\" (#%s)"), *Choice.Label.ToString(), *Choice.Id.ToString()));
			}
			return Labels.Num() > 0 ? FString::Join(Labels, TEXT(", ")) : TEXT("(none)");
		}
	}

	TArray<FString> DiffSnapshots(const FDnaLedgerSnapshot& Expected, const FDnaLedgerSnapshot& Actual)
	{
		TArray<FString> Differences;
		if (Expected.SchemaVersion != Actual.SchemaVersion)
		{
			Differences.Add(FString::Printf(TEXT("schema version: %d, came back as %d"), Expected.SchemaVersion, Actual.SchemaVersion));
		}

		TMap<FName, FDnaValue> Left, Right;
		for (const FDnaFactSnapshotEntry& Entry : Expected.Facts) { Left.Add(Entry.Fact, Entry.Value); }
		for (const FDnaFactSnapshotEntry& Entry : Actual.Facts) { Right.Add(Entry.Fact, Entry.Value); }
		for (const TPair<FName, FDnaValue>& Pair : Left)
		{
			const FDnaValue* Other = Right.Find(Pair.Key);
			if (!Other)
			{
				Differences.Add(FString::Printf(TEXT("fact %s = %s did not come back"), *Pair.Key.ToString(), *DescribeValue(Pair.Value)));
			}
			else if (!SameValue(Pair.Value, *Other))
			{
				Differences.Add(FString::Printf(TEXT("fact %s = %s, came back as %s"), *Pair.Key.ToString(), *DescribeValue(Pair.Value), *DescribeValue(*Other)));
			}
		}
		for (const TPair<FName, FDnaValue>& Pair : Right)
		{
			if (!Left.Contains(Pair.Key))
			{
				Differences.Add(FString::Printf(TEXT("fact %s = %s appeared from nowhere"), *Pair.Key.ToString(), *DescribeValue(Pair.Value)));
			}
		}

		if (Expected.Chapter != Actual.Chapter)
		{
			Differences.Add(FString::Printf(TEXT("chapter %s, came back as %s"), *Expected.Chapter.ToString(), *Actual.Chapter.ToString()));
		}

		for (const TPair<FName, int32>& Pair : Expected.Visited)
		{
			const int32* Other = Actual.Visited.Find(Pair.Key);
			if (!Other || *Other != Pair.Value)
			{
				Differences.Add(FString::Printf(TEXT("visited(%s) = %d, came back as %s"), *Pair.Key.ToString(), Pair.Value, Other ? *FString::FromInt(*Other) : TEXT("nothing")));
			}
		}
		for (const TPair<FName, int32>& Pair : Actual.Visited)
		{
			if (!Expected.Visited.Contains(Pair.Key))
			{
				Differences.Add(FString::Printf(TEXT("visited(%s) = %d appeared from nowhere"), *Pair.Key.ToString(), Pair.Value));
			}
		}

		for (const TPair<FName, float>& Pair : Expected.Timers)
		{
			const float* Other = Actual.Timers.Find(Pair.Key);
			if (!Other || FMath::Abs(*Other - Pair.Value) > 0.001f)
			{
				Differences.Add(FString::Printf(TEXT("timer %s with %gs left, came back %s"), *Pair.Key.ToString(), Pair.Value,
					Other ? *FString::Printf(TEXT("with %gs left"), *Other) : TEXT("stopped")));
			}
		}
		for (const TPair<FName, float>& Pair : Actual.Timers)
		{
			if (!Expected.Timers.Contains(Pair.Key))
			{
				Differences.Add(FString::Printf(TEXT("timer %s came back running"), *Pair.Key.ToString()));
			}
		}

		const TSet<FName> OnceLeft(Expected.ChosenOnce);
		const TSet<FName> OnceRight(Actual.ChosenOnce);
		for (const FName& Id : OnceLeft.Difference(OnceRight))
		{
			Differences.Add(FString::Printf(TEXT("once-choice %s was taken, came back as not taken"), *Id.ToString()));
		}
		for (const FName& Id : OnceRight.Difference(OnceLeft))
		{
			Differences.Add(FString::Printf(TEXT("once-choice %s came back as taken"), *Id.ToString()));
		}
		return Differences;
	}

	// ---------------------------------------------------------------- lifetime

	FDnaTestWorld::FDnaTestWorld(FAutomationTestBase& InTest)
		: Test(InTest)
	{
		// Subsystems are declared Within a game instance; an uninitialised one is enough of an outer.
		GameInstance = NewObject<UGameInstance>(GetTransientPackage(), NAME_None, RF_Transient);
		LedgerObject = NewObject<UDreamNarrativeLedger>(GameInstance, NAME_None, RF_Transient);
		StoryObject = NewObject<UDreamNarrativeStory>(GameInstance, NAME_None, RF_Transient);
		StoryObject->InitializeStandalone(LedgerObject);

		ManualClock = MakeShared<FDnaManualClock>();
		LedgerObject->SetClock(ManualClock);
		LedgerObject->SetRandomSeed(DefaultSeed);
		LedgerObject->SetRandomSource(&Rolls);

		ExpressionPool = NewObject<UDreamNarrativeScript>(GetTransientPackage(), NAME_None, RF_Transient);
	}

	FDnaTestWorld::~FDnaTestWorld()
	{
		if (UDreamNarrativeDialogueRunner* Runner = StoryObject ? StoryObject->GetActiveRunner() : nullptr)
		{
			Runner->Abort();
		}
		if (LedgerObject && LedgerObject->GetRandomSource() == &Rolls)
		{
			LedgerObject->SetRandomSource(nullptr);
		}
	}

	void FDnaTestWorld::AddReferencedObjects(FReferenceCollector& Collector)
	{
		Collector.AddReferencedObject(GameInstance);
		Collector.AddReferencedObject(LedgerObject);
		Collector.AddReferencedObject(StoryObject);
		Collector.AddReferencedObjects(Scripts);
		Collector.AddReferencedObject(ExpressionPool);
	}

	// ---------------------------------------------------------------- compiling

	bool ReadPluginSources(FAutomationTestBase& Test, const TArray<FString>& RelativePaths, TArray<FDnaTestSource>& OutSources)
	{
		const FString PluginDir = GetPluginDir();
		if (PluginDir.IsEmpty())
		{
			Test.AddError(TEXT("The DreamNarrative plugin is not loaded"));
			return false;
		}
		for (const FString& Relative : RelativePaths)
		{
			FDnaTestSource& Source = OutSources.AddDefaulted_GetRef();
			Source.Path = FPaths::Combine(PluginDir, Relative);
			if (!FFileHelper::LoadFileToString(Source.Text, *Source.Path))
			{
				Test.AddError(FString::Printf(TEXT("Cannot read %s"), *Source.Path));
				return false;
			}
		}
		return true;
	}

	bool CompileDnaSources(FAutomationTestBase& Test, const TArray<FDnaTestSource>& Sources, const FDnaGenerateOptions& Options,
		FDnaProgram& OutProgram, TArray<UDreamNarrativeScript*>& OutScripts)
	{
		// The first pass collects every declared name, as the commandlet does, so the text-tail `if`
		// rule sees names from all the sources.
		TSet<FString> Names;
		for (const FDnaTestSource& Source : Sources)
		{
			CollectDnaDeclaredNames(FDnaSourceText(Source.Path, Source.Text), Names);
		}

		FDnaDiagnosticSink Diagnostics;
		TArray<TSharedPtr<FDnaDocument>> Documents;
		for (const FDnaTestSource& Source : Sources)
		{
			FDnaParseOptions ParseOptions;
			ParseOptions.ExternalNames = &Names;
			TSharedPtr<FDnaDocument> Document = MakeShared<FDnaDocument>();
			Diagnostics.SetFilePath(Source.Path);
			ParseDnaDocument(FDnaSourceText(Source.Path, Source.Text), ParseOptions, *Document, Diagnostics);
			Documents.Add(Document);
		}
		BuildDnaProgram(Documents, OutProgram, Diagnostics);
		ValidateDnaProgram(OutProgram, Diagnostics);

		if (!Diagnostics.HasErrors())
		{
			FDnaGenerateResult Result;
			GenerateDnaProgram(OutProgram, Options, Result, Diagnostics);
			OutScripts.Append(Result.Scripts);
		}

		for (const FDnaDiagnostic& Diagnostic : Diagnostics.GetDiagnostics())
		{
			if (Diagnostic.Severity == EDnaSeverity::Error)
			{
				Test.AddError(FDnaDiagnosticSink::ToLogString(Diagnostic));
			}
		}
		return !Diagnostics.HasErrors();
	}

	// ---------------------------------------------------------------- building

	bool FDnaTestWorld::Compile(const TArray<FDnaTestSource>& Sources)
	{
		if (bCompiled)
		{
			ReportError(TEXT("FDnaTestWorld compiles once; put every source in one Compile call"));
			return false;
		}
		bCompiled = true;

		FDnaGenerateOptions Options;
		Options.bSave = false;
		Options.bTransient = true;
		TArray<UDreamNarrativeScript*> Generated;
		const bool bOk = CompileDnaSources(Test, Sources, Options, ProgramModel, Generated);
		for (UDreamNarrativeScript* Script : Generated)
		{
			Scripts.Add(Script);
			StoryObject->RegisterScript(Script);
		}
		return bOk;
	}

	bool FDnaTestWorld::CompilePluginFiles(const TArray<FString>& RelativePaths)
	{
		TArray<FDnaTestSource> Sources;
		return ReadPluginSources(Test, RelativePaths, Sources) && Compile(Sources);
	}

	bool FDnaTestWorld::CompileText(const FString& Text, const FString& Path)
	{
		return Compile({ FDnaTestSource{ Path, Text } });
	}

	// ---------------------------------------------------------------- steps

	bool FDnaTestWorld::Drive(UDreamNarrativeDialogueRunner* Runner, const TArray<FString>& Choices, const FString& What)
	{
		bool bOk = true;
		int32 NextChoice = 0;
		for (int32 Guard = 0; Guard < 10000 && Runner->GetState() != EDnaRunnerState::Ended; ++Guard)
		{
			switch (Runner->GetState())
			{
			case EDnaRunnerState::ShowingLine:
				Runner->Advance();
				break;
			case EDnaRunnerState::AwaitingChoice:
			{
				if (!Choices.IsValidIndex(NextChoice))
				{
					ReportError(FString::Printf(TEXT("%s: choices are on offer but the test has no pick left. On offer: %s"), *What, *DescribeChoices(*Runner)));
					Runner->Abort();
					bOk = false;
					break;
				}
				const FString& Wanted = Choices[NextChoice++];
				int32 Pick = INDEX_NONE;
				for (const FDnaChoiceView& Choice : Runner->GetChoices())
				{
					const bool bById = Wanted.StartsWith(TEXT("#")) && Choice.Id == FName(*Wanted.Mid(1));
					if (bById || Choice.Label.ToString() == Wanted)
					{
						Pick = Choice.Index;
						break;
					}
				}
				if (Pick == INDEX_NONE)
				{
					ReportError(FString::Printf(TEXT("%s: \"%s\" is not on offer. On offer: %s"), *What, *Wanted, *DescribeChoices(*Runner)));
					Runner->Abort();
					bOk = false;
					break;
				}
				Runner->Choose(Pick);
				break;
			}
			case EDnaRunnerState::Waiting:
				// The wait sits on the manual clock; whatever else falls due first fires first.
				if (!ManualClock->AdvanceToNext())
				{
					ReportError(FString::Printf(TEXT("%s: the dialogue is waiting but nothing is on the clock"), *What));
					Runner->Abort();
					bOk = false;
				}
				break;
			case EDnaRunnerState::PlayingStage:
				Runner->NotifyStageFinished();
				break;
			default:
				ReportError(FString::Printf(TEXT("%s: the dialogue stopped in an unexpected state"), *What));
				Runner->Abort();
				bOk = false;
				break;
			}
		}
		if (Runner->GetState() != EDnaRunnerState::Ended)
		{
			ReportError(FString::Printf(TEXT("%s: the dialogue did not end"), *What));
			Runner->Abort();
			bOk = false;
		}
		if (bOk && NextChoice < Choices.Num())
		{
			TArray<FString> Unused;
			for (int32 Index = NextChoice; Index < Choices.Num(); ++Index) { Unused.Add(TEXT("\"") + Choices[Index] + TEXT("\"")); }
			ReportError(FString::Printf(TEXT("%s: the dialogue ended before these picks were offered: %s"), *What, *FString::Join(Unused, TEXT(", "))));
			bOk = false;
		}
		RecordDialogue(*Runner);
		return bOk;
	}

	void FDnaTestWorld::RecordDialogue(const UDreamNarrativeDialogueRunner& Runner)
	{
		LastDialogue = Runner.GetTranscript();
		Transcript.Append(LastDialogue);
	}

	void FDnaTestWorld::ReportError(const FString& Message)
	{
		const FString Full = Context.IsEmpty() ? Message : Context + TEXT(": ") + Message;
		if (ErrorCapture)
		{
			ErrorCapture->Add(Full);
			return;
		}
		Test.AddError(Full);
	}

	bool FDnaTestWorld::FinishStep(const FString& What, bool bStepOk)
	{
		const bool bSaveOk = !bCheckSaveAfterEachStep || CheckSaveRoundTrip(What);
		return bStepOk && bSaveOk;
	}

	bool FDnaTestWorld::Talk(FName Character, const TArray<FString>& Choices)
	{
		const FString What = FString::Printf(TEXT("talk %s"), *Character.ToString());
		Transcript.Add(FString::Printf(TEXT("[%s]"), *What));
		UDreamNarrativeDialogueRunner* Runner = StoryObject->TalkTo(Character);
		if (!Runner)
		{
			ReportError(FString::Printf(TEXT("%s: no entry of %s applies"), *What, *Character.ToString()));
			return false;
		}
		const bool bOk = Drive(Runner, Choices, What);
		return FinishStep(What, bOk);
	}

	bool FDnaTestWorld::Start(FName Node, const TArray<FString>& Choices)
	{
		const FString What = FString::Printf(TEXT("start %s"), *Node.ToString());
		Transcript.Add(FString::Printf(TEXT("[%s]"), *What));
		UDreamNarrativeDialogueRunner* Runner = StoryObject->StartDialogue(Node);
		if (!Runner)
		{
			ReportError(FString::Printf(TEXT("%s: no dialogue called %s"), *What, *Node.ToString()));
			return false;
		}
		const bool bOk = Drive(Runner, Choices, What);
		return FinishStep(What, bOk);
	}

	bool FDnaTestWorld::SetExtern(FName Fact, const FDnaValue& Value)
	{
		const FString What = FString::Printf(TEXT("game sets %s = %s"), *Fact.ToString(), *Value.ToString());
		Transcript.Add(FString::Printf(TEXT("[%s]"), *What));
		const bool bOk = LedgerObject->SetFact(Fact, Value, EDnaWriteSource::Game);
		if (!bOk)
		{
			ReportError(FString::Printf(TEXT("%s: the ledger refused the write"), *What));
		}
		return FinishStep(What, bOk);
	}

	bool FDnaTestWorld::Given(FName Fact, const FDnaValue& Value)
	{
		const FString What = FString::Printf(TEXT("given %s = %s"), *Fact.ToString(), *Value.ToString());
		Transcript.Add(FString::Printf(TEXT("[%s]"), *What));
		const bool bOk = LedgerObject->SetFact(Fact, Value, EDnaWriteSource::Narrative);
		if (!bOk)
		{
			ReportError(FString::Printf(TEXT("%s: the ledger refused the write"), *What));
		}
		return FinishStep(What, bOk);
	}

	void FDnaTestWorld::Wait(double Seconds)
	{
		const FString What = FString::Printf(TEXT("wait %gs"), Seconds);
		Transcript.Add(FString::Printf(TEXT("[%s]"), *What));
		ManualClock->Advance(Seconds);
		FinishStep(What, true);
	}

	bool FDnaTestWorld::Jump(FName Chapter)
	{
		const FString What = FString::Printf(TEXT("jump %s"), *Chapter.ToString());
		Transcript.Add(FString::Printf(TEXT("[%s]"), *What));
		const bool bOk = LedgerObject->JumpToChapter(Chapter);
		if (!bOk)
		{
			ReportError(FString::Printf(TEXT("%s: no such chapter"), *What));
		}
		return FinishStep(What, bOk);
	}

	// ---------------------------------------------------------------- looking

	int32 FDnaTestWorld::CompileExpression(const FString& Expression, bool bAsCondition, FString& OutError)
	{
		const FDnaSourceText Source(TEXT("<expression>"), Expression);
		FDnaDiagnosticSink Diagnostics(TEXT("<expression>"));
		const FDnaExprPtr Expr = ParseDnaExpressionSegment(Source, 0, Source.Len(), Diagnostics);
		if (Expr.IsValid())
		{
			ValidateDnaExpression(ProgramModel, Expr, bAsCondition, Diagnostics);
		}
		if (!Expr.IsValid() || Diagnostics.HasErrors())
		{
			const FDnaDiagnostic* First = Diagnostics.FirstError();
			OutError = First ? FDnaDiagnosticSink::ToWireString(*First) : TEXT("the expression does not parse");
			return INDEX_NONE;
		}
		const int32 Index = CompileDnaExpression(ProgramModel, Expr, *ExpressionPool);
		if (Index == INDEX_NONE)
		{
			OutError = TEXT("the expression could not be lowered");
		}
		return Index;
	}

	bool FDnaTestWorld::Evaluate(const FString& Expression, FDnaValue& OutValue)
	{
		FString Error;
		const int32 Index = CompileExpression(Expression, /*bAsCondition=*/false, Error);
		const FDnaCompiledExpr* Compiled = ExpressionPool->GetExpression(Index);
		if (!Compiled)
		{
			ReportError(FString::Printf(TEXT("`%s`: %s"), *Expression, *Error));
			return false;
		}
		OutValue = LedgerObject->Evaluate(*Compiled);
		return true;
	}

	bool FDnaTestWorld::Expect(const FString& Condition)
	{
		FString Error;
		const int32 Index = CompileExpression(Condition, /*bAsCondition=*/true, Error);
		const FDnaCompiledExpr* Compiled = ExpressionPool->GetExpression(Index);
		if (!Compiled)
		{
			ReportError(FString::Printf(TEXT("expect `%s`: %s"), *Condition, *Error));
			return false;
		}
		if (!LedgerObject->EvaluateCondition(*Compiled))
		{
			const FDnaSourceText Source(TEXT("<expression>"), Condition);
			FDnaDiagnosticSink Scratch;
			const FDnaExprPtr Parsed = ParseDnaExpressionSegment(Source, 0, Source.Len(), Scratch);
			ReportError(FString::Printf(TEXT("expect `%s` is false. %s"), *Condition, Parsed.IsValid() ? *DescribeFactsOf(*Parsed) : TEXT("")));
			return false;
		}
		return true;
	}

	int32 FDnaTestWorld::CompileAst(const FDnaExprPtr& Expr)
	{
		return Expr.IsValid() ? CompileDnaExpression(ProgramModel, Expr, *ExpressionPool) : INDEX_NONE;
	}

	const FDnaCompiledExpr* FDnaTestWorld::GetPooledExpression(int32 Index) const
	{
		return ExpressionPool->GetExpression(Index);
	}

	bool FDnaTestWorld::EvaluateValueFor(const FDnaExprPtr& Expr, const FString& Fact, FDnaValue& OutValue)
	{
		const int32 Index = Expr.IsValid() ? CompileDnaValueFor(ProgramModel, Expr, Fact, *ExpressionPool) : INDEX_NONE;
		const FDnaCompiledExpr* Compiled = ExpressionPool->GetExpression(Index);
		if (!Compiled)
		{
			ReportError(FString::Printf(TEXT("`%s` could not be compiled"), Expr.IsValid() ? *Expr->ToString() : TEXT("<null>")));
			return false;
		}
		OutValue = LedgerObject->Evaluate(*Compiled);
		return true;
	}

	bool FDnaTestWorld::EvaluateAst(const FDnaExprPtr& Expr, FDnaValue& OutValue)
	{
		const FDnaCompiledExpr* Compiled = ExpressionPool->GetExpression(CompileAst(Expr));
		if (!Compiled)
		{
			ReportError(FString::Printf(TEXT("`%s` could not be compiled"), Expr.IsValid() ? *Expr->ToString() : TEXT("<null>")));
			return false;
		}
		OutValue = LedgerObject->Evaluate(*Compiled);
		return true;
	}

	FString FDnaTestWorld::DescribeFactsOf(const FDnaExpr& Expr) const
	{
		TArray<const FDnaExpr*> Names;
		Expr.CollectNames(Names);
		TArray<FString> Parts;
		for (const FDnaExpr* Name : Names)
		{
			if (ProgramModel.FindFact(Name->Name))
			{
				const FString Part = FString::Printf(TEXT("%s = %s"), *Name->Name, *LedgerObject->GetFact(FName(*Name->Name)).ToString());
				Parts.AddUnique(Part);
			}
		}
		TFunction<bool(const FDnaExpr&)> ReadsChapter = [&ReadsChapter](const FDnaExpr& Node)
		{
			return Node.Kind == EDnaExprKind::Chapter
				|| Node.Args.ContainsByPredicate([&ReadsChapter](const FDnaExprPtr& Arg) { return Arg.IsValid() && ReadsChapter(*Arg); });
		};
		if (ReadsChapter(Expr))
		{
			Parts.AddUnique(FString::Printf(TEXT("chapter = %s"), *LedgerObject->GetChapter().ToString()));
		}
		return Parts.Num() > 0 ? TEXT("Now: ") + FString::Join(Parts, TEXT(", ")) : FString();
	}

	bool FDnaTestWorld::TranscriptHas(const FString& Needle) const
	{
		return Transcript.ContainsByPredicate([&Needle](const FString& Entry) { return Entry.Contains(Needle); });
	}

	bool FDnaTestWorld::LastDialogueHas(const FString& Needle) const
	{
		return LastDialogue.ContainsByPredicate([&Needle](const FString& Entry) { return Entry.Contains(Needle); });
	}

	bool FDnaTestWorld::CheckSaveRoundTrip(const FString& Where)
	{
		const TArray<FString> Differences = SaveRoundTripDifferences();
		if (Differences.Num() > 0)
		{
			ReportError(FString::Printf(TEXT("After \"%s\", saving and loading does not give the same ledger:\n    %s"),
				*Where, *FString::Join(Differences, TEXT("\n    "))));
			return false;
		}
		return true;
	}

	TArray<FString> FDnaTestWorld::SaveRoundTripDifferences()
	{
		const FDnaLedgerSnapshot Saved = LedgerObject->MakeSnapshot();

		// Written the strict way: an archive that keeps only SaveGame properties, as many games' save
		// systems are. A field that is not marked SaveGame is lost here and the comparison says so.
		TArray<uint8> Bytes;
		{
			FMemoryWriter Writer(Bytes, /*bIsPersistent=*/true);
			Writer.ArIsSaveGame = true;
			FObjectAndNameAsStringProxyArchive Archive(Writer, /*bInLoadIfFindFails=*/false);
			Archive.ArIsSaveGame = true;
			FDnaLedgerSnapshot Copy = Saved;
			FDnaLedgerSnapshot::StaticStruct()->SerializeItem(Archive, &Copy, nullptr);
		}
		FDnaLedgerSnapshot Loaded;
		{
			FMemoryReader Reader(Bytes, /*bIsPersistent=*/true);
			Reader.ArIsSaveGame = true;
			FObjectAndNameAsStringProxyArchive Archive(Reader, /*bInLoadIfFindFails=*/false);
			Archive.ArIsSaveGame = true;
			FDnaLedgerSnapshot::StaticStruct()->SerializeItem(Archive, &Loaded, nullptr);
		}

		// A fresh ledger that knows the same scripts, as after starting the game and loading the save.
		UDreamNarrativeLedger* Fresh = NewObject<UDreamNarrativeLedger>(GameInstance, NAME_None, RF_Transient);
		// Its timers run on a clock of their own, one that never moves: only what they have left is compared.
		Fresh->SetClock(MakeShared<FDnaManualClock>());
		Fresh->SetTrace(LedgerObject->GetTrace());
		for (UDreamNarrativeScript* Script : Scripts)
		{
			Fresh->RegisterScript(Script);
		}
		Fresh->ApplySnapshot(Loaded);
		Fresh->SetTrace(nullptr);

		return DiffSnapshots(Saved, Fresh->MakeSnapshot());
	}
}
