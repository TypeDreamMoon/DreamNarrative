// Copyright DreamMoon. All Rights Reserved.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "AssetRegistry/AssetRegistryModule.h"
#include "DreamNarrativeScript.h"
#include "DreamNarrativeSettings.h"
#include "DreamNarrativeStageData.h"
#include "Fixtures/DnaTestWorld.h"
#include "Generation/DnaGenerator.h"
#include "LevelSequence.h"
#include "Misc/Guid.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"

using namespace UE::DreamNarrative::Tests;
using namespace UE::DreamNarrative::Lang;

namespace
{
	const FDnaStageData* FindCompiledStage(const UDreamNarrativeScript& Script, FName Name)
	{
		const UDnaStageSection* Section = Script.FindSection<UDnaStageSection>();
		return Section ? Section->Stages.FindByPredicate([Name](const FDnaStageData& Stage) { return Stage.Name == Name; }) : nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaRegressionStageAssetReferences, "DreamNarrative.Regression.Persistence.StageAssetReferences", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDnaRegressionStageAssetReferences::RunTest(const FString& Parameters)
{
	// A level sequence the asset registry knows, in a root of its own.
	const FString Root = TEXT("/Temp/DnaAssetRefs_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	UPackage* Package = CreatePackage(*(Root / TEXT("LS_Reference")));
	ULevelSequence* Sequence = NewObject<ULevelSequence>(Package, TEXT("LS_Reference"), RF_Public | RF_Standalone);
	Sequence->Initialize();
	FAssetRegistryModule::AssetCreated(Sequence);
	UDreamNarrativeSettings* Settings = GetMutableDefault<UDreamNarrativeSettings>();
	const TArray<FString> Roots = Settings->StageAssetRoots;
	Settings->StageAssetRoots = { Root };
	ON_SCOPE_EXIT
	{
		Settings->StageAssetRoots = Roots;
		FAssetRegistryModule::AssetDeleted(Sequence);
		Sequence->ClearFlags(RF_Standalone);
	};

	const FDnaTestSource Source = { TEXT("AssetRefs.dna"), TEXT(
		"stage ByName\n  duration 1.0s\n  sequence\n    0.0s     play LS_Reference\n"
		"stage Missing\n  duration 1.0s\n  sequence\n    0.0s     play LS_Nowhere\n") };
	FDnaGenerateOptions Options;
	Options.bSave = false;
	Options.bTransient = true;
	Options.AssetDirectory = TEXT("/Game/DnaRegression_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	FDnaProgram Program;
	TArray<UDreamNarrativeScript*> Scripts;
	if (!CompileDnaSources(*this, { Source }, Options, Program, Scripts) || !TestEqual(TEXT("one script"), Scripts.Num(), 1)) { return false; }

	const FString Expected = Sequence->GetPathName();
	const FDnaStageData* ByName = FindCompiledStage(*Scripts[0], TEXT("ByName"));
	if (TestNotNull(TEXT("stage by name"), ByName) && TestEqual(TEXT("one entry"), ByName->Entries.Num(), 1))
	{
		TestEqual(TEXT("a bare name is written down as its path"), ByName->Entries[0].Asset, Expected);
	}
	TestTrue(TEXT("the script refers to what its stages play"), Scripts[0]->AssetReferences.Contains(FSoftObjectPath(Expected)));
	const FDnaStageData* Missing = FindCompiledStage(*Scripts[0], TEXT("Missing"));
	if (TestNotNull(TEXT("stage with a missing asset"), Missing) && TestEqual(TEXT("one entry"), Missing->Entries.Num(), 1))
	{
		TestEqual(TEXT("a name nothing answers to stays for the stage to look for"), Missing->Entries[0].Asset, FString(TEXT("LS_Nowhere")));
	}
	TestEqual(TEXT("only found assets are referred to"), Scripts[0]->AssetReferences.Num(), 1);

	// A real asset reports what a cook would miss.
	Options.bTransient = false;
	FDnaGenerateResult Result;
	FDnaDiagnosticSink Diagnostics;
	TestTrue(TEXT("generates"), GenerateDnaProgram(Program, Options, Result, Diagnostics));
	TestTrue(TEXT("a missing asset is reported"), Diagnostics.GetDiagnostics().ContainsByPredicate([](const FDnaDiagnostic& Each)
	{
		return Each.Code == TEXT("DNS6001") && Each.Severity == EDnaSeverity::Warning;
	}));
	return !HasAnyErrors();
}
#endif
