// Copyright DreamMoon. All Rights Reserved.
//
// Executes one dialogue node at a time. The runner owns no UI: it raises OnLine / OnChoices / OnEnded
// and waits for the game to call Advance() or Choose(). Everything it decides, it decides by reading
// the ledger; everything it changes, it changes by writing the ledger in a batch.

#pragma once

#include "CoreMinimal.h"
#include "DreamNarrativeClock.h"
#include "DreamNarrativeDialogue.h"
#include "DreamNarrativeDialogueRunner.generated.h"

class UDreamNarrativeLedger;
class UDreamNarrativeStory;

UENUM(BlueprintType)
enum class EDnaRunnerState : uint8
{
	Idle,
	/** A line is on screen; call Advance() to continue. */
	ShowingLine,
	/** Choices are on screen; call Choose(). */
	AwaitingChoice,
	/** A `wait` is in progress; continues when the ledger's clock says so, or on NotifyWaitFinished(). */
	Waiting,
	/** A blocking `play`; continues on NotifyStageFinished(). */
	PlayingStage,
	Ended,
};

/** What the game shows for a line. */
USTRUCT(BlueprintType)
struct DREAMNARRATIVESTORY_API FDnaLineView
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Narrative")
	FName Id;

	/** None for narration. */
	UPROPERTY(BlueprintReadOnly, Category = "Narrative")
	FName Speaker;

	UPROPERTY(BlueprintReadOnly, Category = "Narrative")
	FText SpeakerDisplay;

	UPROPERTY(BlueprintReadOnly, Category = "Narrative")
	TArray<FName> Tags;

	/** Interpolations already applied. */
	UPROPERTY(BlueprintReadOnly, Category = "Narrative")
	FText Text;

	UPROPERTY(BlueprintReadOnly, Category = "Narrative")
	float Duration = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Narrative")
	FName Stage;

	UPROPERTY(BlueprintReadOnly, Category = "Narrative")
	bool bNarration = false;
};

USTRUCT(BlueprintType)
struct DREAMNARRATIVESTORY_API FDnaChoiceView
{
	GENERATED_BODY()

	/** What to pass to Choose(). */
	UPROPERTY(BlueprintReadOnly, Category = "Narrative")
	int32 Index = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Narrative")
	FName Id;

	UPROPERTY(BlueprintReadOnly, Category = "Narrative")
	FText Label;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDnaRunnerLineSignature, UDreamNarrativeDialogueRunner*, Runner, const FDnaLineView&, Line);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDnaRunnerChoicesSignature, UDreamNarrativeDialogueRunner*, Runner, const TArray<FDnaChoiceView>&, Choices);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDnaRunnerEndedSignature, UDreamNarrativeDialogueRunner*, Runner);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FDnaRunnerStageSignature, UDreamNarrativeDialogueRunner*, Runner, FName, Stage, bool, bBlocking);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDnaRunnerWaitSignature, UDreamNarrativeDialogueRunner*, Runner, float, Seconds);

UCLASS(BlueprintType)
class DREAMNARRATIVESTORY_API UDreamNarrativeDialogueRunner : public UObject
{
	GENERATED_BODY()

public:
	/** Begins executing Node of Section. Returns false when the node does not exist. */
	bool Start(UDreamNarrativeStory* InStory, UDreamNarrativeLedger* InLedger, const UDnaDialogueSection* Section, FName Node);

	/** After a line: continue. */
	UFUNCTION(BlueprintCallable, Category = "Narrative|Dialogue")
	void Advance();

	/** After choices: pick one by the Index the view carried. */
	UFUNCTION(BlueprintCallable, Category = "Narrative|Dialogue")
	bool Choose(int32 Index);

	/** Ends the dialogue now. */
	UFUNCTION(BlueprintCallable, Category = "Narrative|Dialogue")
	void Abort();

	/** For a blocking `play`: the stage is over, continue. */
	UFUNCTION(BlueprintCallable, Category = "Narrative|Dialogue")
	void NotifyStageFinished();

	/** Ends the current `wait` now, whether the clock or the game was driving it. */
	UFUNCTION(BlueprintCallable, Category = "Narrative|Dialogue")
	void NotifyWaitFinished();

	UFUNCTION(BlueprintPure, Category = "Narrative|Dialogue")
	EDnaRunnerState GetState() const { return State; }

	UFUNCTION(BlueprintPure, Category = "Narrative|Dialogue")
	FName GetNodeName() const { return NodeName; }

	UFUNCTION(BlueprintPure, Category = "Narrative|Dialogue")
	const FDnaLineView& GetCurrentLine() const { return CurrentLine; }

	UFUNCTION(BlueprintPure, Category = "Narrative|Dialogue")
	const TArray<FDnaChoiceView>& GetChoices() const { return CurrentChoices; }

	/** When set, `wait` does not start the clock; the game paces waits itself and calls NotifyWaitFinished(). */
	UPROPERTY(BlueprintReadWrite, Category = "Narrative|Dialogue")
	bool bManualWait = false;

	UPROPERTY(BlueprintAssignable, Category = "Narrative|Dialogue")
	FDnaRunnerLineSignature OnLine;

	UPROPERTY(BlueprintAssignable, Category = "Narrative|Dialogue")
	FDnaRunnerChoicesSignature OnChoices;

	UPROPERTY(BlueprintAssignable, Category = "Narrative|Dialogue")
	FDnaRunnerEndedSignature OnEnded;

	UPROPERTY(BlueprintAssignable, Category = "Narrative|Dialogue")
	FDnaRunnerStageSignature OnPlayStage;

	UPROPERTY(BlueprintAssignable, Category = "Narrative|Dialogue")
	FDnaRunnerWaitSignature OnWait;

	/** Every line and choice shown so far, for tests and the debugger. */
	const TArray<FString>& GetTranscript() const { return Transcript; }

private:
	/** Runs instructions until one yields or the node ends. */
	void Run();
	void Finish();
	bool EnterNode(FName Node);
	FText FormatText(const FText& Pattern, const TArray<int32>& Args) const;
	FText DisplayNameOf(FName Character) const;
	const FDnaCompiledExpr* Expr(int32 Index) const;
	bool Condition(int32 Index) const;

	UPROPERTY()
	TObjectPtr<UDreamNarrativeStory> Story;

	UPROPERTY()
	TObjectPtr<UDreamNarrativeLedger> Ledger;

	UPROPERTY()
	TObjectPtr<UDnaDialogueSection> Section;

	const FDnaDialogueNodeData* Node = nullptr;
	FName NodeName;
	int32 PC = 0;
	EDnaRunnerState State = EDnaRunnerState::Idle;

	UPROPERTY()
	FDnaLineView CurrentLine;

	UPROPERTY()
	TArray<FDnaChoiceView> CurrentChoices;

	/** Group index the current choices belong to, and which option index each view maps to. */
	int32 CurrentGroup = INDEX_NONE;
	TArray<int32> CurrentOptionIndices;

	FDnaClockHandle WaitHandle;
	TArray<FString> Transcript;
	/** Guards against runaway loops in a node that never yields. */
	int32 StepBudget = 0;
};
