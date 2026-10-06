// Copyright DreamMoon. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Tests/DnaDgtShapes.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DnaDreamGameplayTaskMigration.h"
#include "Lang/DnaModel.h"
#include "Lang/DnaParser.h"
#include "Lang/DnaValidator.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaDgtMigrationTest, "DreamNarrative.Editor.DgtMigration", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDnaDgtMigrationTest::RunTest(const FString& Parameters)
{
	using namespace UE::DreamNarrative;
	using namespace UE::DreamNarrative::Lang;

	// Two tasks the way DGT holds them: in their classes' defaults.
	UDnaDgtShapeTask* Hunt = GetMutableDefault<UDnaDgtShapeTask>();
	UDnaDgtShapeSubTask* Deliver = GetMutableDefault<UDnaDgtShapeSubTask>();
	Hunt->TaskName = TEXT("猎狼");
	Hunt->TaskDisplayName = FText::FromString(TEXT("猎狼"));
	Hunt->TaskDesc = FText::FromString(TEXT("北边林子里的狼又多了。"));
	Hunt->TaskPriority = EDnaDgtShapePriority::EDTP_High;
	Hunt->bUseMaximumCompletionTime = true;
	Hunt->MaximumCompletionTimeSeconds = 600.f;
	Hunt->SubTasks = { UDnaDgtShapeSubTask::StaticClass() };
	Hunt->bCompletedAutoGiveSubTask = true;
	UDnaDgtShapeCondition* Wolves = NewObject<UDnaDgtShapeCondition>(GetTransientPackage());
	Wolves->ConditionDisplayName = FText::FromString(TEXT("狼"));
	Wolves->CompletedCount = 5;
	UDnaDgtShapeCondition* Pelts = NewObject<UDnaDgtShapeCondition>(GetTransientPackage());
	Pelts->ConditionDisplayName = FText::FromString(TEXT("狼皮"));
	Pelts->CompletedCount = 2;
	Hunt->TaskCompletedCondition.Conditions = { { TEXT("Wolves"), Wolves }, { TEXT("Pelts"), Pelts } };
	Hunt->TaskCompletedCondition.CompletionMode = EDnaDgtShapeCompletion::EDTCCM_Custom;
	Hunt->TaskCompletedCondition.CustomConditionCount = 1;
	Deliver->TaskName = TEXT("送皮");
	Deliver->TaskDesc = FText::FromString(TEXT("把皮送{回}村里。"));
	UDnaDgtShapeCondition* Delivered = NewObject<UDnaDgtShapeCondition>(GetTransientPackage());
	Deliver->TaskCompletedCondition.Conditions = { { TEXT("Done"), Delivered } };

	TArray<FString> Notes;
	const FString Text = Editor::MakeDnaFromDreamGameplayTasks({ UDnaDgtShapeTask::StaticClass(), UDnaDgtShapeSubTask::StaticClass() }, Notes);
	AddInfo(Text);

	// Put the defaults back before anything can fail.
	Hunt->TaskName = TEXT("Task");
	Hunt->TaskDisplayName = FText::GetEmpty();
	Hunt->TaskDesc = FText::GetEmpty();
	Hunt->TaskPriority = EDnaDgtShapePriority::EDTP_Normal;
	Hunt->bUseMaximumCompletionTime = false;
	Hunt->MaximumCompletionTimeSeconds = 0.f;
	Hunt->SubTasks.Reset();
	Hunt->bCompletedAutoGiveSubTask = false;
	Hunt->TaskCompletedCondition = FDnaDgtShapeConditions();
	Deliver->TaskName = TEXT("Task");
	Deliver->TaskDesc = FText::GetEmpty();
	Deliver->TaskCompletedCondition = FDnaDgtShapeConditions();

	TestTrue(TEXT("a counter per condition, written by the game"), Text.Contains(TEXT("fact 猎狼_计数\n  Wolves: int extern\n  Pelts: int extern")));
	TestTrue(TEXT("the title"), Text.Contains(TEXT("  title: 猎狼")));
	TestTrue(TEXT("high priority is a main quest"), Text.Contains(TEXT("  priority: main")));
	TestTrue(TEXT("the time limit"), Text.Contains(TEXT("  timeout: 600s")));
	TestTrue(TEXT("the description as the journal"), Text.Contains(TEXT("    accepted: 北边林子里的狼又多了。")));
	TestTrue(TEXT("an objective per condition, shown with its count"), Text.Contains(TEXT("  objective 猎狼_计数.Wolves >= 5\n    show: 狼 {猎狼_计数.Wolves}/5")));
	TestTrue(TEXT("the sub-task, given on completion"), Text.Contains(TEXT("  subquest 送皮 auto")));
	TestTrue(TEXT("braces in texts are not read as interpolation"), Text.Contains(TEXT("把皮送(回)村里。")));
	TestTrue(TEXT("the one-of-two completion is noted"), Notes.ContainsByPredicate([](const FString& Note) { return Note.Contains(TEXT("猎狼")) && Note.Contains(TEXT("1 of its 2")); }));

	// And it compiles.
	FDnaDiagnosticSink Diagnostics;
	TArray<TSharedPtr<FDnaDocument>> Documents;
	TSharedPtr<FDnaDocument> Document = MakeShared<FDnaDocument>();
	ParseDnaDocument(FDnaSourceText(TEXT("Migrated.dna"), Text), FDnaParseOptions(), *Document, Diagnostics);
	Documents.Add(Document);
	FDnaProgram Program;
	BuildDnaProgram(Documents, Program, Diagnostics);
	ValidateDnaProgram(Program, Diagnostics);
	for (const FDnaDiagnostic& Diagnostic : Diagnostics.GetDiagnostics())
	{
		if (Diagnostic.Severity == EDnaSeverity::Error)
		{
			AddError(FDnaDiagnosticSink::ToLogString(Diagnostic));
		}
	}
	return !HasAnyErrors();
}

#endif
