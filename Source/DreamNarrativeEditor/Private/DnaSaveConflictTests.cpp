// Copyright DreamMoon. All Rights Reserved.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Framework/Application/SlateApplication.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "HAL/FileManager.h"
#include "SDnaStoryPanel.h"
#include "SDnaStageTimeline.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaRegressionStoryConflict, "DreamNarrative.Regression.Persistence.StorySaveExternalChanges", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDnaRegressionStoryConflict::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("Slate available"), FSlateApplication::IsInitialized())) { return false; }
	const FString Path = FPaths::ProjectIntermediateDir() / TEXT("StoryConflict_") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".dna");
	ON_SCOPE_EXIT { IFileManager::Get().Delete(*Path); };
	const FString Source = TEXT("character Speaker\ndialogue Intro for Speaker\n  Speaker: Original line.\n");
	if (!TestTrue(TEXT("write scratch source"), FFileHelper::SaveStringToFile(Source, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))) { return false; }
	TSharedRef<SDnaStoryPanel> Panel = SNew(SDnaStoryPanel).Files({ Path });
	FText Error;
	if (!TestTrue(TEXT("local panel edit"), Panel->EditCondition(TEXT("Intro"), TEXT("true"), Error))) { return false; }
	const FString External = Source.Replace(TEXT("Original line."), TEXT("External writer's line."));
	TestTrue(TEXT("external editor saves a change"), FFileHelper::SaveStringToFile(External, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
	TArray<FString> Errors;
	TestFalse(TEXT("saving a stale session must report conflict"), Panel->SaveAll(Errors));
	FString Actual;
	FFileHelper::LoadFileToString(Actual, *Path);
	TestTrue(TEXT("external writer's content must survive"), Actual.Contains(TEXT("External writer's line.")));
	TestEqual(TEXT("the stale file is reported as conflicted"), Panel->GetConflictedFiles(), TArray<FString>{ Path });
	TestTrue(TEXT("discarding the panel's edits"), Panel->DiscardEdits(Path, Error));
	TestFalse(TEXT("nothing unsaved after discarding"), Panel->HasUnsavedEdits());
	FFileHelper::LoadFileToString(Actual, *Path);
	TestTrue(TEXT("discarding leaves the external change on disk"), Actual.Contains(TEXT("External writer's line.")));
	TestTrue(TEXT("an edit after discarding builds on the external change"), Panel->EditCondition(TEXT("Intro"), TEXT("true"), Error));
	Errors.Reset();
	TestTrue(TEXT("and saves without conflict"), Panel->SaveAll(Errors));
	FFileHelper::LoadFileToString(Actual, *Path);
	TestTrue(TEXT("saved text keeps the external line"), Actual.Contains(TEXT("External writer's line.")));

	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaRegressionTimelineConflict, "DreamNarrative.Regression.Persistence.TimelineSaveExternalChanges", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDnaRegressionTimelineConflict::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("Slate available"), FSlateApplication::IsInitialized())) { return false; }
	const FString Path = FPaths::ProjectIntermediateDir() / TEXT("TimelineConflict_") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".dna");
	ON_SCOPE_EXIT { IFileManager::Get().Delete(*Path); };
	const FString Source = TEXT("stage Clip\n  duration 2.0s\n  ui\n    0.0s..1.0s hide hud\n  restore\n    ui show hud\n");
	if (!TestTrue(TEXT("write scratch source"), FFileHelper::SaveStringToFile(Source, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))) { return false; }
	TSharedRef<SDnaStageTimeline> Panel = SNew(SDnaStageTimeline).Files({ Path });
	FText Error;
	if (!TestTrue(TEXT("select scratch stage"), Panel->SelectStage(TEXT("Clip"))) ||
		!TestTrue(TEXT("local timeline edit"), Panel->MoveEntry(4, 0.2f, 1.2f, Error))) { return false; }
	const FString External = Source + TEXT("// External writer's note.\n");
	TestTrue(TEXT("external editor saves a change"), FFileHelper::SaveStringToFile(External, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
	TestFalse(TEXT("saving a stale session must report conflict"), Panel->SaveAll());
	FString Actual;
	FFileHelper::LoadFileToString(Actual, *Path);
	TestTrue(TEXT("external writer's content must survive"), Actual.Contains(TEXT("External writer's note.")));
	TestEqual(TEXT("the stale file is reported as conflicted"), Panel->GetConflictedFiles(), TArray<FString>{ Path });
	TestTrue(TEXT("overwriting is the user's explicit choice"), Panel->OverwriteDisk(Path, Error));
	TestFalse(TEXT("nothing unsaved after overwriting"), Panel->HasUnsavedEdits());
	FFileHelper::LoadFileToString(Actual, *Path);
	TestFalse(TEXT("overwriting replaces the external change"), Actual.Contains(TEXT("External writer's note.")));
	TestEqual(TEXT("overwriting writes the panel's text"), Actual, Panel->GetText());

	return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaRegressionSaveAllOrNone, "DreamNarrative.Regression.Persistence.SaveAllOrNone", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDnaRegressionSaveAllOrNone::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("Slate available"), FSlateApplication::IsInitialized())) { return false; }
	const FString Stem = FPaths::ProjectIntermediateDir() / TEXT("SaveAllOrNone_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	const FString First = Stem + TEXT("_A.dna");
	const FString Second = Stem + TEXT("_B.dna");
	ON_SCOPE_EXIT { IFileManager::Get().Delete(*First); IFileManager::Get().Delete(*Second); };
	const FString SourceA = TEXT("character Alpha\ndialogue IntroA for Alpha\n  Alpha: Hello.\n");
	const FString SourceB = TEXT("character Beta\ndialogue IntroB for Beta\n  Beta: Hello.\n");
	if (!TestTrue(TEXT("write sources"), FFileHelper::SaveStringToFile(SourceA, *First, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)
		&& FFileHelper::SaveStringToFile(SourceB, *Second, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))) { return false; }
	TSharedRef<SDnaStoryPanel> Panel = SNew(SDnaStoryPanel).Files({ First, Second });
	FText Error;
	if (!TestTrue(TEXT("edit the first"), Panel->EditCondition(TEXT("IntroA"), TEXT("true"), Error))
		|| !TestTrue(TEXT("edit the second"), Panel->EditCondition(TEXT("IntroB"), TEXT("true"), Error))) { return false; }
	TestTrue(TEXT("someone else changes the second"), FFileHelper::SaveStringToFile(SourceB + TEXT("// elsewhere\n"), *Second, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
	TArray<FString> Errors;
	TestFalse(TEXT("the save is refused"), Panel->SaveAll(Errors));
	FString Actual;
	FFileHelper::LoadFileToString(Actual, *First);
	TestEqual(TEXT("nor is the first file written"), Actual, SourceA);
	TestTrue(TEXT("both edits are still unsaved"), Panel->HasUnsavedEdits());
	TArray<FString> Left;
	IFileManager::Get().FindFiles(Left, *(FPaths::GetPath(First) / TEXT("*.tmp")), true, false);
	TestFalse(TEXT("no temporary file is left behind"), Left.ContainsByPredicate([](const FString& Name) { return Name.Contains(TEXT("SaveAllOrNone_")); }));
	return !HasAnyErrors();
}

#endif
