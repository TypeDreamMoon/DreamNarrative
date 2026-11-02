// Copyright DreamMoon. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "DreamNarrativeDialogue.h"
#include "DreamNarrativeScript.h"
#include "Fixtures/DnaTestWorld.h"
#include "HAL/FileManager.h"
#include "Internationalization/PackageLocalizationManager.h"
#include "Misc/Guid.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Sound/SoundWave.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaLocalizedVoiceTest, "DreamNarrative.Dialogue.LocalizedVoiceLayout", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A voice in another language is the engine's localized asset: the same path under Content/L10N/<culture>/.
 * The engine swaps it in when a game (or a packaged one) loads the line's voice; the editor and PIE play the
 * source language. Here: the voice a line compiles to is a package the engine's localization finds the
 * English one of.
 */
bool FDnaLocalizedVoiceTest::RunTest(const FString& Parameters)
{
	using namespace UE::DreamNarrative::Tests;

	const FString Folder = TEXT("DnaL10NProbe_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	const FString Source = TEXT("/Game/") + Folder + TEXT("/VO/老周_初见_1");
	const FString English = TEXT("/Game/L10N/en/") + Folder + TEXT("/VO/老周_初见_1");
	const FString Content = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir());
	// The L10N folders this test makes, to take away again if nothing else is in them.
	TArray<FString> Parents;
	for (const TCHAR* Each : { TEXT("L10N/en"), TEXT("L10N") })
	{
		if (!IFileManager::Get().DirectoryExists(*(Content / Each))) { Parents.Add(Content / Each); }
	}
	TArray<UObject*> Made;
	ON_SCOPE_EXIT
	{
		for (UObject* Object : Made)
		{
			FAssetRegistryModule::AssetDeleted(Object);
			Object->ClearFlags(RF_Standalone);
		}
		IFileManager::Get().DeleteDirectory(*(Content / Folder), false, true);
		IFileManager::Get().DeleteDirectory(*(Content / TEXT("L10N/en") / Folder), false, true);
		for (const FString& Parent : Parents) { IFileManager::Get().DeleteDirectory(*Parent, /*RequireExists=*/false, /*Tree=*/false); }
		FPackageLocalizationManager::Get().ConditionalUpdateCache();
	};
	// Both on disk, as a project has them: the localization cache looks at packages that exist.
	for (const FString& PackageName : { Source, English })
	{
		UPackage* Package = CreatePackage(*PackageName);
		USoundWave* Wave = NewObject<USoundWave>(Package, *FPackageName::GetShortName(PackageName), RF_Public | RF_Standalone);
		Wave->Duration = 1.f;
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		Args.SaveFlags = SAVE_NoError;
		if (!TestTrue(*FString::Printf(TEXT("%s is saved"), *PackageName),
			UPackage::SavePackage(Package, Wave, *FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension()), Args)))
		{
			return false;
		}
		FAssetRegistryModule::AssetCreated(Wave);
		Made.Add(Wave);
	}
	IAssetRegistry::GetChecked().ScanPathsSynchronous({ TEXT("/Game/") + Folder, TEXT("/Game/L10N/en/") + Folder }, /*bForceRescan=*/true);
	FPackageLocalizationManager::Get().ConditionalUpdateCache();

	FDnaTestWorld World(*this);
	if (!World.CompileText(FString::Printf(TEXT(
		"character 老周\n"
		"dialogue 老周_初见 for 老周\n"
		"  老周: 你就是新来的? voice %s\n"), *Source))) { return false; }
	const UDnaDialogueSection* Section = World.GetScripts().Num() > 0 ? World.GetScripts()[0]->FindSection<UDnaDialogueSection>() : nullptr;
	const FDnaLineData* Line = Section && Section->Nodes.Num() > 0 && Section->Nodes[0].Lines.Num() > 0 ? &Section->Nodes[0].Lines[0] : nullptr;
	if (!TestNotNull(TEXT("the line"), Line)) { return false; }
	const FName VoicePackage = FName(*Line->Voice.ToSoftObjectPath().GetLongPackageName());
	TestEqual(TEXT("the line's voice is the source-language asset"), VoicePackage.ToString(), Source);
	TestEqual(TEXT("in English the engine finds its localized asset"),
		FPackageLocalizationManager::Get().FindLocalizedPackageNameForCulture(VoicePackage, TEXT("en")).ToString(), English);
	TestTrue(TEXT("in a language without one it keeps the source"),
		FPackageLocalizationManager::Get().FindLocalizedPackageNameForCulture(VoicePackage, TEXT("ja")).IsNone());
	return !HasAnyErrors();
}

#endif
