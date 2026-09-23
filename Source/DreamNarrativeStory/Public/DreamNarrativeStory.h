// Copyright DreamMoon. All Rights Reserved.
//
// The Story subsystem: knows every dialogue node in every registered script, picks the entry for a
// character, and hands out runners. Quests are not implemented yet; chapters live on the ledger.

#pragma once

#include "CoreMinimal.h"
#include "DreamNarrativeDialogue.h"
#include "DreamNarrativeDialogueRunner.h"
#include "DreamNarrativeLedger.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "DreamNarrativeStory.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDnaDialogueStartedSignature, UDreamNarrativeDialogueRunner*, Runner);
DECLARE_MULTICAST_DELEGATE_OneParam(FDnaTalkRequestedSignature, FName /*Character*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FDnaStartRequestedSignature, FName /*Node*/);

class UDreamNarrativeRecorder;

UCLASS()
class DREAMNARRATIVESTORY_API UDreamNarrativeStory : public UGameInstanceSubsystem, public IDnaQuestStateProvider
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** For tools and tests that have no game instance: wires the subsystem to a ledger by hand. */
	void InitializeStandalone(UDreamNarrativeLedger* InLedger);

	/** Registers a script with the ledger and indexes its dialogue. */
	UFUNCTION(BlueprintCallable, Category = "Narrative|Story")
	void RegisterScript(UDreamNarrativeScript* Script);

	/** Loads and registers every script asset under the configured asset directory. */
	UFUNCTION(BlueprintCallable, Category = "Narrative|Story")
	int32 LoadAllScripts();

	/** Starts a specific node. Null when it does not exist. */
	UFUNCTION(BlueprintCallable, Category = "Narrative|Story")
	UDreamNarrativeDialogueRunner* StartDialogue(FName Node);

	/**
	 * Picks the entry for a character: among its `for` nodes whose condition holds, the highest
	 * priority wins, ties going to the earlier declaration. Null when nothing applies.
	 */
	UFUNCTION(BlueprintCallable, Category = "Narrative|Story")
	UDreamNarrativeDialogueRunner* TalkTo(FName Character);

	/** The entry TalkTo would pick, without starting it. */
	UFUNCTION(BlueprintPure, Category = "Narrative|Story")
	FName SelectEntry(FName Character) const;

	UFUNCTION(BlueprintPure, Category = "Narrative|Story")
	UDreamNarrativeDialogueRunner* GetActiveRunner() const { return ActiveRunner; }

	UFUNCTION(BlueprintPure, Category = "Narrative|Story")
	UDreamNarrativeLedger* GetLedger() const { return Ledger; }

	UFUNCTION(BlueprintPure, Category = "Narrative|Story")
	bool HasDialogue(FName Node) const { return NodeIndex.Contains(Node); }

	void GetDialogueNames(TArray<FName>& Out) const { NodeIndex.GetKeys(Out); }
	const FDnaDialogueNodeData* FindNode(FName Node, const UDnaDialogueSection** OutSection = nullptr) const;

	UPROPERTY(BlueprintAssignable, Category = "Narrative|Story")
	FDnaDialogueStartedSignature OnDialogueStarted;

	/** TalkTo was called; the entry is picked right after, so anything it draws comes after this. */
	FDnaTalkRequestedSignature OnTalkRequested;

	/** StartDialogue was called for a node by name (TalkTo does not fire this). */
	FDnaStartRequestedSignature OnStartRequested;

	// ---------------------------------------------------------------- recording

	/** Starts writing down what happens to the story (`dn.record`), from the ledger as it stands. A recording already running ends unsaved. */
	UDreamNarrativeRecorder* StartRecording(const FString& Name);

	/** Stops recording and hands back the test block it wrote; empty when nothing was being recorded. */
	FString StopRecording();

	UDreamNarrativeRecorder* GetRecorder() const { return Recorder; }

	// IDnaQuestStateProvider -- quests are not implemented yet, so every query is false.
	virtual bool GetQuestState(FName Quest, EDnaQuestStateQuery Query) const override { return false; }

private:
	/** Starts a node: what StartDialogue and TalkTo share once they know which node. */
	UDreamNarrativeDialogueRunner* Begin(FName Node);

	UFUNCTION()
	void HandleRunnerEnded(UDreamNarrativeDialogueRunner* Runner);

	struct FNodeRef
	{
		TWeakObjectPtr<const UDnaDialogueSection> Section;
		int32 NodeIndex = INDEX_NONE;
	};

	UPROPERTY()
	TObjectPtr<UDreamNarrativeLedger> Ledger;

	UPROPERTY()
	TArray<TObjectPtr<UDreamNarrativeScript>> Scripts;

	UPROPERTY()
	TObjectPtr<UDreamNarrativeDialogueRunner> ActiveRunner;

	UPROPERTY()
	TObjectPtr<UDreamNarrativeRecorder> Recorder;

	TMap<FName, FNodeRef> NodeIndex;
	/** Entry nodes per character, in declaration order. */
	TMap<FName, TArray<FName>> EntriesByCharacter;
};
