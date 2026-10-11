// Copyright DreamMoon. All Rights Reserved.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "AssetRegistry/AssetRegistryModule.h"
#include "DreamNarrativeEditorModule.h"
#include "DreamNarrativeScript.h"
#include "DreamNarrativeSettings.h"
#include "Misc/Guid.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaCookScriptPackages, "DreamNarrative.Regression.Persistence.CookTakesScripts", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDnaCookScriptPackages::RunTest(const FString& Parameters)
{
	UDreamNarrativeSettings* Settings = GetMutableDefault<UDreamNarrativeSettings>();
	const FString Directory = Settings->AssetDirectory;
	Settings->AssetDirectory = TEXT("/Game/DnaCookProbe_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	UPackage* Package = CreatePackage(*(Settings->AssetDirectory / TEXT("Chapter") / TEXT("Probe")));
	UDreamNarrativeScript* Script = NewObject<UDreamNarrativeScript>(Package, TEXT("Probe"), RF_Public | RF_Standalone);
	FAssetRegistryModule::AssetCreated(Script);
	ON_SCOPE_EXIT
	{
		Settings->AssetDirectory = Directory;
		FAssetRegistryModule::AssetDeleted(Script);
		Script->ClearFlags(RF_Standalone);
	};

	TArray<FName> Packages;
	UE::DreamNarrative::Editor::CollectScriptPackagesToCook(Packages);
	TestTrue(TEXT("a script below the asset directory goes into the cook"), Packages.Contains(Package->GetFName()));
	return !HasAnyErrors();
}
#endif
