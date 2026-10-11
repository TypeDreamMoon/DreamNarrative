// Copyright DreamMoon. All Rights Reserved.
//
// Executes one dialogue node at a time. The runner owns no UI: it raises OnLine / OnChoices / OnEnded
// and waits for the game to call Advance() or Choose(). Everything it decides, it decides by reading
// the ledger; everything it changes, it changes by writing the ledger in a batch.
//
// A runner runs on a channel. On one that moves on by itself it advances its own lines when their
// time is up. Its channel may pause it (another channel is talking) and resume it later, when it
// shows the line it stopped at again. A dialogue an event opened sees the event's parameters.

#pragma once

#include "CoreMinimal.h"
#include "DreamNarrativeClock.h"
#include "DreamNarrativeDialogue.h"
#include "DreamNarrativeEvent.h"
#include "DreamNarrativeDialogueRunner.generated.h"

class IDnaStageHost;
class USoundBase;
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
	/** A blocking `play`; continues when the stage host says the stage is over, or on NotifyStageFinished(). */
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

	/** The line's voice; none when it has none. The game plays it (OnVoice says when). */
	UPROPERTY(BlueprintReadOnly, Category = "Narrative")
	TSoftObjectPtr<USoundBase> Voice;

	/** How long the voice runs, when known; a channel that moves on by itself waits this long. */
	UPROPERTY(BlueprintReadOnly, Category = "Narrative")
	float VoiceSeconds = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Narrative")
	bool bNarration = false;

	/** The channel the dialogue runs on. */
	UPROPERTY(BlueprintReadOnly, Category = "Narrative")
	FName Channel;

	/** Shown again: the dialogue was paused on this line and has just resumed. */
	UPROPERTY(BlueprintReadOnly, Category = "Narrative")
	bool bRepeat = false;
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
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDnaRunnerChoiceMadeSignature, UDreamNarrativeDialogueRunner*, Runner, const FDnaChoiceView&, Choice);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDnaRunnerEndedSignature, UDreamNarrativeDialogueRunner*, Runner);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FDnaRunnerStageSignature, UDreamNarrativeDialogueRunner*, Runner, FName, Stage, bool, bBlocking);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDnaRunnerWaitSignature, UDreamNarrativeDialogueRunner*, Runner, float, Seconds);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDnaRunnerPausedSignature, UDreamNarrativeDialogueRunner*, Runner);

UCLASS(BlueprintType)
class DREAMNARRATIVESTORY_API UDreamNarrativeDialogueRunner : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Begins executing Node of Section. Returns false when the node does not exist. A runner can be
	 * started again once its dialogue ends; starting it while one is still running aborts that one.
	 * The channel and the event scope are whatever was set before (main and none unless set).
	 */
	bool Start(UDreamNarrativeStory* InStory, UDreamNarrativeLedger* InLedger, const UDnaDialogueSection* Section, FName Node);

	/** The channel the next Start runs on, and whether its lines move on by themselves. */
	void SetChannel(FName InChannel, bool bInAutoAdvance);

	/** The event whose parameters the next Start's dialogue reads; ClearEventScope for none. */
	void SetEventScope(const FDnaEventPayload& InEvent);
	void ClearEventScope();

	UFUNCTION(BlueprintPure, Category = "Narrative|Dialogue")
	FName GetChannel() const { return Channel; }

	/** The event that opened the dialogue, or null. */
	const FDnaEventPayload* GetEventScope() const { return bHasEvent ? &Event : nullptr; }

	/**
	 * Holds the dialogue where it is: nothing advances, no wait runs down. Its channel does this
	 * while another channel talks over it.
	 */
	void Pause();

	/** Goes on after Pause: the line (or the choices) on screen is shown again, a wait runs on from where it stopped. */
	void Resume();

	UFUNCTION(BlueprintPure, Category = "Narrative|Dialogue")
	bool IsPaused() const { return bPaused; }

	/** How many times OnLine has fired: a line shown again after a pause counts again. */
	int32 GetLinesShown() const { return LinesShown; }

	/** Seconds a line stays up on a channel that moves on by itself: its `for`, else an estimate from its length. */
	static float LineSeconds(const FDnaLineView& Line);

	/** After a line: continue. */
	UFUNCTION(BlueprintCallable, Category = "Narrative|Dialogue")
	void Advance();

	/** After choices: pick one by the Index the view carried. */
	UFUNCTION(BlueprintCallable, Category = "Narrative|Dialogue")
	bool Choose(int32 Index);

	/** Ends the dialogue now. */
	UFUNCTION(BlueprintCallable, Category = "Narrative|Dialogue")
	void Abort();

	/** For a blocking `play`: the stage is over, continue. A stage the host still plays is stopped. */
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

	/** A choice was taken; fires before the dialogue goes on. The choices it was taken from are still in GetChoices(). */
	UPROPERTY(BlueprintAssignable, Category = "Narrative|Dialogue")
	FDnaRunnerChoiceMadeSignature OnChoiceMade;

	UPROPERTY(BlueprintAssignable, Category = "Narrative|Dialogue")
	FDnaRunnerEndedSignature OnEnded;

	UPROPERTY(BlueprintAssignable, Category = "Narrative|Dialogue")
	FDnaRunnerStageSignature OnPlayStage;

	UPROPERTY(BlueprintAssignable, Category = "Narrative|Dialogue")
	FDnaRunnerWaitSignature OnWait;

	/** The channel held the dialogue still (another channel is talking); take its line off screen. */
	UPROPERTY(BlueprintAssignable, Category = "Narrative|Dialogue")
	FDnaRunnerPausedSignature OnPaused;

	/** The dialogue goes on; OnLine (or OnChoices) follows with what is on screen again. */
	UPROPERTY(BlueprintAssignable, Category = "Narrative|Dialogue")
	FDnaRunnerPausedSignature OnResumed;

	/** Every line and choice shown so far, for tests and the debugger. */
	const TArray<FString>& GetTranscript() const { return Transcript; }

private:
	/** Runs instructions until one yields or the node ends. */
	void Run();
	void Finish();
	/** Takes a choice: its once mark, then on along its branch. */
	void TakeChoice(const FDnaChoiceData& Option);
	/** Commits the ledger batch a run of `set` instructions opened, if one is open. */
	void CloseBatch();
	bool EnterNode(FName Node);
	FText FormatText(const FText& Pattern, const TArray<int32>& Args) const;
	FText DisplayNameOf(FName Character) const;
	const FDnaCompiledExpr* Expr(int32 Index) const;
	bool Condition(int32 Index) const;
	/** An expression's value, with the event that opened the dialogue in scope. */
	FDnaValue Evaluate(const FDnaCompiledExpr& Compiled) const;
	/** Shows CurrentLine (unless paused): the broadcast, and the clock for a channel that moves on by itself. */
	void ShowLine();
	void ScheduleAutoAdvance();
	void CancelAutoAdvance();
	void HandleAutoAdvance();
	/** The stage host, when there is one and it has Stage; null otherwise. */
	IDnaStageHost* HostFor(FName Stage) const;
	/** The host says the blocking stage numbered Serial is over. */
	void HandleStageEnded(uint32 Serial);
	/** A line's `@` stage: plays with the line, stops when the line goes. */
	void PlayLineStage();
	void StopLineStage();

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
	/** Consecutive `set` instructions write in one ledger batch, so observers see all of them or none. */
	bool bBatchOpen = false;
	TArray<FString> Transcript;
	/** Guards against runaway loops in a node that never yields. */
	int32 StepBudget = 0;

	FName Channel;
	bool bAutoAdvance = false;
	bool bHasEvent = false;

	UPROPERTY()
	FDnaEventPayload Event;

	bool bPaused = false;
	bool bChoosing = false;
	/** A choice made while a listener of OnChoiceMade paused the dialogue: taken when it resumes. */
	TOptional<FDnaChoiceData> PendingChoice;
	uint32 ExecutionSerial = 0;
	/** The line or choices on screen have been broadcast (a paused runner holds them back until it resumes). */
	bool bShown = false;
	int32 LinesShown = 0;
	FDnaClockHandle AdvanceHandle;
	/** What a wait had left when the runner paused; negative when none was cut short. */
	double PausedWaitRemaining = -1.0;
	/** The blocking stage the host plays for it (0: none), and a number that tells a late end from the current one. */
	uint64 StageTicket = 0;
	uint32 StageSerial = 0;
	/** The stage attached to the line on screen, while the host plays it. */
	uint64 LineStageTicket = 0;
};
