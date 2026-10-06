// Copyright DreamMoon. All Rights Reserved.
//
// Writes down what happens to the story while it runs -- who was talked to, what was shown, what
// was chosen, what the game wrote, every draw -- as a `.dna` test block that plays it all back in
// the logic layer. The ledger as it stood when recording began becomes the block's `given`; what
// changed by the end becomes its closing `expect`s. `dn.record start <name>` / `dn.record stop`.
//
// It watches through the story's and the ledger's own notifications and puts itself in front of
// the ledger's draws, passing each one on unchanged: the game plays exactly as it would unrecorded.
//
// A test step says everything it opens before the next step begins, so what a dialogue shows is
// written under the step that opened it -- a dialogue an event opened under the step that raised the
// event, even when it waited its turn -- and a line shown again after a pause is written once. What
// the timing of the game decided (a dialogue cut off by a later step, a draw made while two channels
// talked) can come out differently in the replay.

#pragma once

#include "CoreMinimal.h"
#include "DreamNarrativeDialogueRunner.h"
#include "DreamNarrativeLedger.h"
#include "DreamNarrativeStory.h"
#include "DreamNarrativeRecorder.generated.h"

class UDreamNarrativeStory;

UCLASS()
class DREAMNARRATIVESTORY_API UDreamNarrativeRecorder : public UObject, public IDnaRandomSource
{
	GENERATED_BODY()

public:
	/** Starts writing down what happens to Story, from the ledger as it stands now. */
	void Begin(UDreamNarrativeStory* InStory, const FString& InName);

	/** Stops, and hands back the file: a comment saying where it came from, then the test block. */
	FString End();

	bool IsRecording() const { return Story != nullptr; }
	const FString& GetName() const { return Name; }

	/** Where `dn.record` keeps a recording: <project>/<source directory>/Tests/Recorded/<Name>.dna. */
	static FString GetDefaultPath(const FString& Name);

	// IDnaRandomSource
	virtual float NextUnit(FRandomStream& Stream) override;

	virtual void BeginDestroy() override;

private:
	/** One thing the player or the game did, with what it opened and the draws made meanwhile. */
	struct FStep
	{
		FString Header;
		/** Lines under the header, each with its depth below it: what main showed and what was chosen there. */
		TArray<TPair<int32, FString>> Children;
		/** The same for each other channel, in the order they first spoke. */
		TArray<TPair<FName, TArray<TPair<int32, FString>>>> Channels;
		TArray<float> Draws;
	};

	void HandleTalk(FName Character);
	void HandleStart(FName Node);
	void HandleEvent(const FDnaEventPayload& Event, EDnaWriteSource Source);
	void HandleFactWritten(FName Fact, const FDnaValue& Value, EDnaWriteSource Source);
	void HandleJump(FName Chapter);
	void HandleQuestRequested(EDnaQuestRequest What, FName Quest, EDnaWriteSource Who);

	UFUNCTION()
	void HandleDialogueStarted(UDreamNarrativeDialogueRunner* InRunner);

	UFUNCTION()
	void HandleLine(UDreamNarrativeDialogueRunner* InRunner, const FDnaLineView& Line);

	UFUNCTION()
	void HandleChoice(UDreamNarrativeDialogueRunner* InRunner, const FDnaChoiceView& Choice);

	UFUNCTION()
	void HandleEnded(UDreamNarrativeDialogueRunner* InRunner);

	void AddStep(FStep&& Step);
	void AddStep(const FString& Header);
	/**
	 * The step a draw made now is written under: the earliest one a dialogue still running belongs
	 * to (the replay runs that dialogue there), else the latest; never one before an earlier draw's,
	 * so the replay hands the draws out in the order they were made.
	 */
	int32 DrawStep();
	/** Where the lines Runner shows are written: its step, its channel's block there. Null for a dialogue not written down. */
	TArray<TPair<int32, FString>>* LinesOf(const UDreamNarrativeDialogueRunner* InRunner);
	void Unwatch(UDreamNarrativeDialogueRunner* InRunner);
	void Detach();

	FString Name;

	UPROPERTY()
	TObjectPtr<UDreamNarrativeStory> Story;

	UPROPERTY()
	TObjectPtr<UDreamNarrativeLedger> Ledger;

	/** Every dialogue being written down, and the step it belongs to. */
	UPROPERTY()
	TMap<TObjectPtr<UDreamNarrativeDialogueRunner>, int32> Watched;

	int32 LastDrawStep = INDEX_NONE;
	/** For each event, the step it was last raised in. */
	TMap<FName, int32> EventSteps;

	/** The ledger as it stood when recording began. */
	FDnaLedgerSnapshot StartSnapshot;
	TMap<FName, FDnaValue> StartSession;

	TArray<FStep> Steps;

	IDnaRandomSource* PassOn = nullptr;
	FDelegateHandle TalkHandle;
	FDelegateHandle StartHandle;
	FDelegateHandle WriteHandle;
	FDelegateHandle JumpHandle;
	FDelegateHandle QuestHandle;
	FDelegateHandle EventHandle;
};
