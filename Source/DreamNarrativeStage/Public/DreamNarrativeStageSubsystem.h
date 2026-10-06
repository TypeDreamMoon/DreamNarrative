// Copyright DreamMoon. All Rights Reserved.
//
// The stages of every registered script, and the host the story plays them through. A stage plays on
// a director: the one that began playing last among those that take the story's stages, or else one
// put on the first local player controller. In a game with nobody to show it to (no player
// controller) a stage still takes its time, on the ledger's clock, so the story waits as it would.

#pragma once

#include "CoreMinimal.h"
#include "DreamNarrativeClock.h"
#include "DreamNarrativeStageData.h"
#include "DreamNarrativeStageHost.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "DreamNarrativeStageSubsystem.generated.h"

class UDreamNarrativeLedger;
class UDreamNarrativeScript;
class UDreamNarrativeStageDirector;

UCLASS()
class DREAMNARRATIVESTAGE_API UDreamNarrativeStageSubsystem : public UGameInstanceSubsystem, public IDnaStageHost
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** For tools and tests that have no game instance: wires the subsystem to a ledger by hand. */
	void InitializeStandalone(UDreamNarrativeLedger* InLedger);

	/** A stage by name (or by the name it had, `was`); null when no registered script declares it. */
	const FDnaStageData* FindStage(FName Stage) const;

	void GetStageNames(TArray<FName>& Out) const { StageIndex.GetKeys(Out); }

	/** The director the story's stages play on now; null when there is none and no player controller to put one on. */
	UDreamNarrativeStageDirector* GetStoryDirector() const;

	/** Directors that take the story's stages announce themselves as they begin play. */
	void RegisterDirector(UDreamNarrativeStageDirector* Director);
	void UnregisterDirector(UDreamNarrativeStageDirector* Director);

	/** Plays a stage on the story's director, as the game. False when there is no such stage. */
	UFUNCTION(BlueprintCallable, Category = "Narrative|Stage")
	bool PlayStageByName(FName Stage);

	/** Ends every stage the subsystem started. */
	UFUNCTION(BlueprintCallable, Category = "Narrative|Stage")
	void StopAllStages(EDnaStageEnd Reason = EDnaStageEnd::Stopped);

	/** Stages playing now, the oldest first. */
	int32 NumPlaying() const { return Plays.Num(); }

	// IDnaStageHost
	virtual bool HasStage(FName Stage) const override { return FindStage(Stage) != nullptr; }
	virtual uint64 PlayStage(const FDnaStageRequest& Request, FDnaStageFinishedDelegate OnFinished) override;
	virtual void StopStage(uint64 Ticket) override;
	virtual bool IsStagePlaying(uint64 Ticket) const override { return Plays.Contains(Ticket); }

private:
	struct FPlay
	{
		FName Stage;
		bool bOnLine = false;
		TWeakObjectPtr<UDreamNarrativeStageDirector> Director;
		uint32 PlayNumber = 0;
		/** For a stage with nowhere to show: when it is over, on the ledger's clock. */
		FDnaClockHandle Timer;
		FDnaStageFinishedDelegate OnFinished;
	};

	void Index(UDreamNarrativeScript* Script);
	void HandleDirectorEnded(uint32 PlayNumber, EDnaStageEnd Reason, TWeakObjectPtr<UDreamNarrativeStageDirector> Director);
	void Finish(uint64 Ticket, EDnaStageEnd Reason);

	struct FStageRef
	{
		TWeakObjectPtr<const UDnaStageSection> Section;
		int32 StageIndex = INDEX_NONE;
	};

	UPROPERTY()
	TObjectPtr<UDreamNarrativeLedger> Ledger;

	TMap<FName, FStageRef> StageIndex;
	TMap<FName, FName> Redirects;
	TArray<TWeakObjectPtr<UDreamNarrativeStageDirector>> Directors;
	TMap<uint64, FPlay> Plays;
	TMap<TWeakObjectPtr<UDreamNarrativeStageDirector>, FDelegateHandle> DirectorHandles;
	uint64 NextTicket = 1;
	FDelegateHandle ScriptHandle;
	FDelegateHandle ApplyingHandle;
};
