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

	return !HasAnyErrors();
}
#endif
