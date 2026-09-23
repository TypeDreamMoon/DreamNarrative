// Copyright DreamMoon. All Rights Reserved.
//
// The smoke-layer fixtures: the real engine objects the logic layer stands in for.
//
//   FDnaStandaloneGame   a game instance made the way a standalone game makes one -- a world context,
//                        a world, and every game-instance subsystem initialised by the engine in
//                        dependency order -- inside the running process, torn down again at the end.
//   FDnaGameInProcess    the game instance a running -game process already has, found through the
//                        engine's world contexts.
//   FDnaNarrativeLog     collects what the narrative logs while it lives; the `dn.` commands answer
//                        through the log.
//   FDnaScratchAssets    compiles sources into real packages under a unique /Game path that exist
//                        only in memory, announces them to the asset registry and points the plugin's
//                        asset directory there, so code that finds scripts the way a game does finds
//                        them. Everything is withdrawn when it goes.

#pragma once

#include "CoreMinimal.h"
#include "Misc/OutputDevice.h"
#include "UObject/GCObject.h"

class FAutomationTestBase;
class UDreamNarrativeLedger;
class UDreamNarrativeScript;
class UDreamNarrativeStory;
class UGameInstance;
class UWorld;

namespace UE::DreamNarrative::Tests
{
	/** What both kinds of game give a smoke test: a world with a game instance, and a way to type into its console. */
	class FDnaGameBase
	{
	public:
		virtual ~FDnaGameBase() = default;

		virtual UWorld* GetWorld() const = 0;
		UGameInstance* GetGameInstance() const;
		UDreamNarrativeLedger* GetLedger() const;
		UDreamNarrativeStory* GetStory() const;

		/** Runs a console command against this game's world, as typing it into the game's console would. */
		bool Exec(const FString& Command) const;
	};

	class FDnaStandaloneGame : public FDnaGameBase, public FGCObject
	{
	public:
		FDnaStandaloneGame();
		virtual ~FDnaStandaloneGame() override;

		virtual UWorld* GetWorld() const override { return World; }

		/** Ticks the world Frames times, Seconds of game time each. */
		void Tick(float Seconds, int32 Frames = 1);

		virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
		virtual FString GetReferencerName() const override { return TEXT("UE::DreamNarrative::Tests::FDnaStandaloneGame"); }

	private:
		TObjectPtr<UGameInstance> GameInstance;
		TObjectPtr<UWorld> World;
		/** Tick() moves the engine's frame counter; the destructor puts it back. */
		uint64 FrameCounterBefore = 0;
	};

	class FDnaGameInProcess : public FDnaGameBase
	{
	public:
		/** The first world of type Game with a game instance; null when the process runs no game. */
		virtual UWorld* GetWorld() const override;
	};

	class FDnaNarrativeLog : public FOutputDevice
	{
	public:
		FDnaNarrativeLog();
		virtual ~FDnaNarrativeLog() override;

		/** The lines logged since the last call, oldest first. */
		TArray<FString> Take();

		virtual void Serialize(const TCHAR* Message, ELogVerbosity::Type Verbosity, const FName& Category) override;
		virtual bool CanBeUsedOnAnyThread() const override { return true; }

	private:
		FCriticalSection Lock;
		TArray<FString> Lines;
	};

	class FDnaScratchAssets : public FGCObject
	{
	public:
		/** Compiles the files, given relative to the plugin, as one program. Failure fails the test. */
		FDnaScratchAssets(FAutomationTestBase& Test, const TArray<FString>& PluginRelativePaths);
		virtual ~FDnaScratchAssets() override;

		bool IsValid() const { return bValid; }
		const FString& GetDirectory() const { return Directory; }

		virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
		virtual FString GetReferencerName() const override { return TEXT("UE::DreamNarrative::Tests::FDnaScratchAssets"); }

	private:
		FString Directory;
		FString PreviousDirectory;
		TArray<TObjectPtr<UDreamNarrativeScript>> Scripts;
		bool bValid = false;
	};
}
