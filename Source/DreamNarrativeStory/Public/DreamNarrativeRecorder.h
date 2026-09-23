// Copyright DreamMoon. All Rights Reserved.
//
// Writes down what happens to the story while it runs -- who was talked to, what was shown, what
// was chosen, what the game wrote, every draw -- as a `.dna` test block that plays it all back in
// the logic layer. The ledger as it stood when recording began becomes the block's `given`; what
// changed by the end becomes its closing `expect`s. `dn.record start <name>` / `dn.record stop`.
//
// It watches through the story's and the ledger's own notifications and puts itself in front of
// the ledger's draws, passing each one on unchanged: the game plays exactly as it would unrecorded.

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
	/** One thing the player or the game did, written down when the next begins (its draws come with it). */
	struct FStep
	{
		FString Header;
		/** Lines under the header, each with its depth below it. */
		TArray<TPair<int32, FString>> Children;
		TArray<float> Draws;
	};

	void HandleTalk(FName Character);
	void HandleStart(FName Node);
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

	/** Writes the open step down and opens Step; an empty header opens none. */
	void NextStep(FStep&& Step);
	void NextStep(const FString& Header);
	void Flush();
	void Detach();
	void ReleaseRunner();

	FString Name;

	UPROPERTY()
	TObjectPtr<UDreamNarrativeStory> Story;

	UPROPERTY()
	TObjectPtr<UDreamNarrativeLedger> Ledger;

	/** The dialogue being written down. */
	UPROPERTY()
	TObjectPtr<UDreamNarrativeDialogueRunner> Runner;

	/** The ledger as it stood when recording began. */
	FDnaLedgerSnapshot StartSnapshot;
	TMap<FName, FDnaValue> StartSession;

	TArray<FString> Body;
	TOptional<FStep> Open;
	/** Game writes made while a dialogue ran: written down after it. */
	TArray<FStep> WrittenDuringDialogue;

	IDnaRandomSource* PassOn = nullptr;
	FDelegateHandle TalkHandle;
	FDelegateHandle StartHandle;
	FDelegateHandle WriteHandle;
	FDelegateHandle JumpHandle;
	FDelegateHandle QuestHandle;
};
