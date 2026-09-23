// Copyright DreamMoon. All Rights Reserved.

#include "Fixtures/DnaGameFixture.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "DreamNarrativeCoreModule.h"
#include "DreamNarrativeLedger.h"
#include "DreamNarrativeScript.h"
#include "DreamNarrativeSettings.h"
#include "DreamNarrativeStory.h"
#include "DreamNarrativeStoryModule.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Fixtures/DnaTestWorld.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"

namespace UE::DreamNarrative::Tests
{
	// ---------------------------------------------------------------- base

	UGameInstance* FDnaGameBase::GetGameInstance() const
	{
		const UWorld* World = GetWorld();
		return World ? World->GetGameInstance() : nullptr;
	}

	UDreamNarrativeLedger* FDnaGameBase::GetLedger() const
	{
		const UGameInstance* Instance = GetGameInstance();
		return Instance ? Instance->GetSubsystem<UDreamNarrativeLedger>() : nullptr;
	}

	UDreamNarrativeStory* FDnaGameBase::GetStory() const
	{
		const UGameInstance* Instance = GetGameInstance();
		return Instance ? Instance->GetSubsystem<UDreamNarrativeStory>() : nullptr;
	}

	bool FDnaGameBase::Exec(const FString& Command) const
	{
		return GEngine && GEngine->Exec(GetWorld(), *Command, *GLog);
	}

	// ---------------------------------------------------------------- standalone

	FDnaStandaloneGame::FDnaStandaloneGame()
	{
		FrameCounterBefore = GFrameCounter;
		const FName PackageName = MakeUniqueObjectName(nullptr, UPackage::StaticClass(), FName(TEXT("/Temp/DreamNarrativeSmokeWorld")));
		UPackage* Package = NewObject<UPackage>(nullptr, PackageName, RF_Transient);
		GameInstance = NewObject<UGameInstance>(GEngine);
		GameInstance->InitializeStandalone(TEXT("DreamNarrativeSmokeWorld"), Package);
		World = GameInstance->GetWorld();
		FURL URL;
		World->InitializeActorsForPlay(URL);
	}

	FDnaStandaloneGame::~FDnaStandaloneGame()
	{
		if (World)
		{
			World->BeginTearingDown();
			if (GameInstance)
			{
				GameInstance->Shutdown();
			}
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}
		// Tick() moved the frame counter so the timer manager would run; give the engine its own back.
		GFrameCounter = FrameCounterBefore;
	}

	void FDnaStandaloneGame::Tick(float Seconds, int32 Frames)
	{
		for (int32 Frame = 0; Frame < Frames; ++Frame)
		{
			// A timer manager runs once per engine frame (FTimerManager::HasBeenTickedThisFrame).
			++GFrameCounter;
			World->Tick(LEVELTICK_All, Seconds);
		}
	}

	void FDnaStandaloneGame::AddReferencedObjects(FReferenceCollector& Collector)
	{
		Collector.AddReferencedObject(GameInstance);
		Collector.AddReferencedObject(World);
	}

	// ---------------------------------------------------------------- in process

	UWorld* FDnaGameInProcess::GetWorld() const
	{
		if (!GEngine)
		{
			return nullptr;
		}
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.WorldType == EWorldType::Game && Context.World() && Context.World()->GetGameInstance())
			{
				return Context.World();
			}
		}
		return nullptr;
	}

	// ---------------------------------------------------------------- log

	FDnaNarrativeLog::FDnaNarrativeLog()
	{
		GLog->AddOutputDevice(this);
	}

	FDnaNarrativeLog::~FDnaNarrativeLog()
	{
		GLog->RemoveOutputDevice(this);
	}

	void FDnaNarrativeLog::Serialize(const TCHAR* Message, ELogVerbosity::Type Verbosity, const FName& Category)
	{
		if (Category == LogDreamNarrativeStory.GetCategoryName() || Category == LogDreamNarrative.GetCategoryName())
		{
			FScopeLock ScopeLock(&Lock);
			Lines.Add(Message);
		}
	}

	TArray<FString> FDnaNarrativeLog::Take()
	{
		// The redirector may still hold lines for devices it serves off the logging thread; deliver
		// them before reading, or a command's answer arrives with the next command.
		GLog->Flush();
		FScopeLock ScopeLock(&Lock);
		return MoveTemp(Lines);
	}

	// ---------------------------------------------------------------- scratch assets

	FDnaScratchAssets::FDnaScratchAssets(FAutomationTestBase& Test, const TArray<FString>& PluginRelativePaths)
	{
		// Under /Game so the asset registry treats the packages like any project content; unique per
		// fixture so two tests never see each other's scripts.
		Directory = FString::Printf(TEXT("/Game/DreamNarrativeScratch/%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));

		TArray<FDnaTestSource> Sources;
		if (!ReadPluginSources(Test, PluginRelativePaths, Sources))
		{
			return;
		}
		Lang::FDnaGenerateOptions Options;
		Options.bSave = false;
		Options.bTransient = false;
		Options.AssetDirectory = Directory;
		Lang::FDnaProgram Program;
		TArray<UDreamNarrativeScript*> Generated;
		if (!CompileDnaSources(Test, Sources, Options, Program, Generated))
		{
			return;
		}
		for (UDreamNarrativeScript* Script : Generated)
		{
			Scripts.Add(Script);
			FAssetRegistryModule::AssetCreated(Script);
		}

		UDreamNarrativeSettings* Settings = GetMutableDefault<UDreamNarrativeSettings>();
		PreviousDirectory = Settings->AssetDirectory;
		Settings->AssetDirectory = Directory;
		bValid = true;
	}

	FDnaScratchAssets::~FDnaScratchAssets()
	{
		if (bValid)
		{
			GetMutableDefault<UDreamNarrativeSettings>()->AssetDirectory = PreviousDirectory;
		}
		for (UDreamNarrativeScript* Script : Scripts)
		{
			if (Script)
			{
				FAssetRegistryModule::AssetDeleted(Script);
				Script->ClearFlags(RF_Public | RF_Standalone);
				Script->MarkAsGarbage();
				if (UPackage* Package = Script->GetPackage())
				{
					Package->MarkAsGarbage();
				}
			}
		}
		Scripts.Reset();
	}

	void FDnaScratchAssets::AddReferencedObjects(FReferenceCollector& Collector)
	{
		Collector.AddReferencedObjects(Scripts);
	}
}
