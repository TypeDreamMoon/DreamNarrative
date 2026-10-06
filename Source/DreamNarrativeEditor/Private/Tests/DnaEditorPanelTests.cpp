// Copyright DreamMoon. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DreamNarrativeClock.h"
#include "DreamNarrativeEditorSettings.h"
#include "DreamNarrativeLedger.h"
#include "DreamNarrativeStory.h"
#include "Engine/GameInstance.h"
#include "Framework/Application/SlateApplication.h"
#include "Generation/DnaGenerator.h"
#include "Interfaces/IPluginManager.h"
#include "Lang/DnaProject.h"
#include "SDnaDebugPanel.h"
#include "SDnaStageTimeline.h"
#include "SDnaStoryPanel.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaEditorPanelsTest, "DreamNarrative.Editor.Panels", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDnaEditorPanelsTest::RunTest(const FString& Parameters)
{
	using namespace UE::DreamNarrative;
	using namespace UE::DreamNarrative::Lang;
	using namespace UE::DreamNarrative::Graph;

	// The source link fills in the file and the line.
	TestEqual(TEXT("a source link"), Editor::MakeSourceLink(TEXT("vscode://file/{file}:{line}"), TEXT("C:/a/b.dna"), 12), FString(TEXT("vscode://file/C:/a/b.dna:12")));

	if (!FSlateApplication::IsInitialized())
	{
		AddInfo(TEXT("No Slate in this process: the panels are not built"));
		return true;
	}
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("DreamNarrative"));
	if (!TestTrue(TEXT("the plugin is there"), Plugin.IsValid()))
	{
		return false;
	}
	const FString Sample = FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir() / TEXT("Tests/Corpus/Validate/Samples/automata.dna"));

	// -- the story view: the sample's graph, grouped by kind; an edit shows at once and goes with undo
	TSharedRef<SDnaStoryPanel> Story = SNew(SDnaStoryPanel).Files({ Sample });
	TestEqual(TEXT("one group per kind"), Story->GetGroupCount(), 6);
	TestEqual(TEXT("the sample compiles"), Story->GetErrorCount(), 0);
	TestTrue(TEXT("its nodes are there"), Story->GetGraph().Nodes.Num() > 30);
	FText Error;
	TestTrue(TEXT("edit a condition"), Story->EditCondition(TEXT("九S_日常"), TEXT("已会合"), Error));
	const int32 Node = Story->GetGraph().Find(EDnaGraphNodeKind::Dialogue, TEXT("九S_日常"));
	TestTrue(TEXT("the view shows it"), Node != INDEX_NONE && Story->GetGraph().Nodes[Node].Detail.Contains(TEXT("if 已会合")));
	TestTrue(TEXT("not saved yet"), Story->HasUnsavedEdits());
	TestTrue(TEXT("connect"), Story->Connect(TEXT("九S_日常"), TEXT("九S_会合"), Error));
	TestTrue(TEXT("the edge is there"), Story->GetGraph().Edges.ContainsByPredicate([&Story](const FDnaGraphEdge& Edge)
	{
		return Edge.Kind == EDnaGraphEdgeKind::GoTo && Story->GetGraph().Nodes[Edge.From].Name == TEXT("九S_日常") && Story->GetGraph().Nodes[Edge.To].Name == TEXT("九S_会合");
	}));
	TestFalse(TEXT("connecting to nothing is refused"), Story->Connect(TEXT("九S_日常"), TEXT("没有这段"), Error));
	TestTrue(TEXT("undo"), Story->Undo());
	TestTrue(TEXT("undo again"), Story->Undo());
	TestFalse(TEXT("back as it is on disk"), Story->HasUnsavedEdits());

	// -- the stage timeline: a drag rewrites one line, the playhead shows what the game would
	TSharedRef<SDnaStageTimeline> Timeline = SNew(SDnaStageTimeline).Files({ Sample });
	TestTrue(TEXT("the sample's stages"), Timeline->GetStageNames().Contains(TEXT("处决_巨型机械")) && Timeline->GetStageNames().Contains(TEXT("骇入闪回")));
	TestTrue(TEXT("pick the finisher"), Timeline->SelectStage(TEXT("处决_巨型机械")));
	const FDnaStageData* Finisher = Timeline->GetStage();
	if (TestNotNull(TEXT("it compiles"), Finisher))
	{
		TestEqual(TEXT("and checks"), Timeline->GetDiagnostics().Num(), 0);
		const FDnaStageEntry* Spawn = Finisher->Entries.FindByPredicate([](const FDnaStageEntry& Entry) { return Entry.Verb == EDnaStageVerb::Spawn; });
		const FString Before = Timeline->GetText();
		if (TestNotNull(TEXT("the effect"), Spawn))
		{
			const int32 Line = Spawn->SourceLine;
			TestTrue(TEXT("drag it"), Timeline->MoveEntry(Line, 2.5f, 2.5f, Error));
			TArray<FString> Old, New;
			Before.ParseIntoArrayLines(Old, false);
			Timeline->GetText().ParseIntoArrayLines(New, false);
			int32 Different = 0;
			for (int32 Index = 0; Index < FMath::Min(Old.Num(), New.Num()); ++Index)
			{
				Different += Old[Index] != New[Index] ? 1 : 0;
			}
			TestEqual(TEXT("as many lines"), New.Num(), Old.Num());
			TestEqual(TEXT("one of them changed"), Different, 1);
			TestTrue(TEXT("that one"), New.IsValidIndex(Line - 1) && New[Line - 1].Contains(TEXT("2.5s     spawn")));
			TestTrue(TEXT("the timeline sees it"), Timeline->GetStage() && Timeline->GetStage()->Entries.ContainsByPredicate([](const FDnaStageEntry& Entry)
			{
				return Entry.Verb == EDnaStageVerb::Spawn && FMath::IsNearlyEqual(Entry.Start, 2.5f);
			}));
			TestTrue(TEXT("unsaved"), Timeline->HasUnsavedEdits());
			TestTrue(TEXT("undo"), Timeline->Undo());
			TestEqual(TEXT("back as it was"), Timeline->GetText(), Before);
		}
		// Scrubbing to a moment shows the pose the director plays at it.
		for (const float At : { 0.f, 1.05f, 2.45f, 4.2f })
		{
			Timeline->Scrub(At);
			TestTrue(*FString::Printf(TEXT("the pose at %.2f s"), At), Timeline->GetPreviewPose().NearlyEquals(DreamNarrative::SampleDnaStage(*Timeline->GetStage(), At)));
		}
	}

	// -- the debugger: a story compiled here, its facts and the writes made to them
	TStrongObjectPtr<UGameInstance> GameInstance(NewObject<UGameInstance>(GetTransientPackage()));
	UDreamNarrativeLedger* Ledger = NewObject<UDreamNarrativeLedger>(GameInstance.Get());
	UDreamNarrativeStory* StoryObject = NewObject<UDreamNarrativeStory>(GameInstance.Get());
	Ledger->SetClock(MakeShared<FDnaManualClock>());
	StoryObject->InitializeStandalone(Ledger);
	FDnaProgram Program;
	FDnaDiagnosticSink Diagnostics;
	LoadDnaProgram({ Sample }, Program, Diagnostics);
	FDnaGenerateOptions Options;
	Options.bSave = false;
	Options.bTransient = true;
	FDnaGenerateResult Result;
	GenerateDnaProgram(Program, Options, Result, Diagnostics);
	for (UDreamNarrativeScript* Script : Result.Scripts)
	{
		StoryObject->RegisterScript(Script);
	}
	TSharedRef<SDnaDebugPanel> Debug = SNew(SDnaDebugPanel).Story(StoryObject);
	TestTrue(TEXT("every fact has a row"), Debug->NumFactRows() > 10);
	TestTrue(TEXT("with where it is kept"), Debug->GetFactRows().ContainsByPredicate([](const TSharedPtr<FString>& Row) { return Row->StartsWith(TEXT("路线 = A")) && Row->Contains(TEXT("profile")); }));
	Ledger->SetFact(TEXT("巨型机械已击破"), FDnaValue::FromBool(true), EDnaWriteSource::Game);
	Debug->Refresh();
	TestTrue(TEXT("the write is in the log"), Debug->NumWrites() >= 1);
	StoryObject->StopAllDialogue();
	return !HasAnyErrors();
}

#endif
