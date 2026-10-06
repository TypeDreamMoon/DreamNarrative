// Copyright DreamMoon. All Rights Reserved.
//
// The Story subsystem: knows every dialogue node and quest in every registered script, picks the
// entry for a character or an event, runs dialogues on their channels, and runs quests. Chapters
// live on the ledger.
//
// Dialogues run on channels, one at a time each and side by side across channels. A channel that is
// talking holds new arrivals by its busy rule (queue, interrupt, drop), may pause other channels
// while it talks and may stop them when it starts. An event raised on the ledger opens, on each
// channel, the best `on` entry that applies.
//
// A quest watches the ledger: after every committed batch it checks `fail if`, latches objectives
// that hold, and completes when the required ones are all done. A hidden quest starts itself the
// first time it can be taken on. Its state is a ledger fact (QuestStateFact), so a save carries it, a
// load restores it, and chapter rules can wait on it.

#pragma once

#include "CoreMinimal.h"
#include "DreamNarrativeDialogue.h"
#include "DreamNarrativeDialogueRunner.h"
#include "DreamNarrativeLedger.h"
#include "DreamNarrativeQuest.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "DreamNarrativeStory.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDnaDialogueStartedSignature, UDreamNarrativeDialogueRunner*, Runner);
DECLARE_MULTICAST_DELEGATE_OneParam(FDnaTalkRequestedSignature, FName /*Character*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FDnaStartRequestedSignature, FName /*Node*/);

/** What was asked of a quest. */
enum class EDnaQuestRequest : uint8
{
	Accept,
	Complete,
	Fail,
};

/** One objective as the player sees it. */
USTRUCT(BlueprintType)
struct DREAMNARRATIVESTORY_API FDnaObjectiveView
{
	GENERATED_BODY()

	/** The `show:` text with its interpolations applied; empty when the objective has none. */
	UPROPERTY(BlueprintReadOnly, Category = "Narrative")
	FText Text;

	UPROPERTY(BlueprintReadOnly, Category = "Narrative")
	bool bDone = false;

	UPROPERTY(BlueprintReadOnly, Category = "Narrative")
	bool bOptional = false;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FDnaQuestStateChangedSignature, FName, Quest, EDnaQuestState, OldState, EDnaQuestState, NewState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDnaQuestUpdatedSignature, FName, Quest);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDnaQuestRewardsSignature, FName, Quest, const TArray<FDnaReward>&, Rewards);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDnaQuestStageSignature, FName, Quest, FName, Stage);
DECLARE_MULTICAST_DELEGATE_ThreeParams(FDnaQuestRequestedSignature, EDnaQuestRequest /*What*/, FName /*Quest*/, EDnaWriteSource /*Who*/);
DECLARE_MULTICAST_DELEGATE_TwoParams(FDnaEventHeardSignature, const FDnaEventPayload& /*Event*/, EDnaWriteSource /*Source*/);

/** A dialogue waiting its turn on a channel. */
USTRUCT()
struct FDnaChannelQueueItem
{
	GENERATED_BODY()

	UPROPERTY()
	FName Node;

	UPROPERTY()
	FDnaEventPayload Event;

	UPROPERTY()
	bool bHasEvent = false;

	UPROPERTY()
	int32 Priority = 0;

	/** A dialogue cut off on this channel that goes on where it stopped, instead of Node starting. */
	UPROPERTY()
	TObjectPtr<UDreamNarrativeDialogueRunner> Suspended;

	/** For a dialogue another channel stopped: that channel, which has to fall quiet first. */
	UPROPERTY()
	FName WaitsFor;
};

/** Where one channel stands. */
USTRUCT()
struct FDnaChannelState
{
	GENERATED_BODY()

	/** The dialogue running (or paused) on it. */
	UPROPERTY()
	TObjectPtr<UDreamNarrativeDialogueRunner> Runner;

	UPROPERTY()
	TArray<FDnaChannelQueueItem> Queue;

	/** Dialogues another channel stopped that go on once that channel falls quiet. */
	UPROPERTY()
	TArray<FDnaChannelQueueItem> Parked;

	/** The node the running dialogue started at, its priority, and whether it goes on after being cut off. */
	FName RunnerNode;
	int32 RunnerPriority = 0;
	bool bRunnerResumes = false;

	/** How many talking channels hold this one still. */
	int32 PauseCount = 0;

	/** Its pauses on other channels are in force: it is talking. */
	bool bSuppressing = false;
};

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

	/**
	 * Starts a specific node on its channel, ending whatever ran there: the game asked for it. Null
	 * when it does not exist.
	 */
	UFUNCTION(BlueprintCallable, Category = "Narrative|Story")
	UDreamNarrativeDialogueRunner* StartDialogue(FName Node);

	/**
	 * Picks the entry for a character: among its `for` nodes whose condition holds and that are not
	 * spent (`once`) or cooling down (`cooldown`), the highest priority wins, ties going to the earlier
	 * declaration. Null when nothing applies.
	 */
	UFUNCTION(BlueprintCallable, Category = "Narrative|Story")
	UDreamNarrativeDialogueRunner* TalkTo(FName Character);

	/** The entry TalkTo would pick, without starting it. */
	UFUNCTION(BlueprintPure, Category = "Narrative|Story")
	FName SelectEntry(FName Character) const;

	/** The dialogue running on the main channel, or null. */
	UFUNCTION(BlueprintPure, Category = "Narrative|Story")
	UDreamNarrativeDialogueRunner* GetActiveRunner() const { return GetRunner(NAME_None); }

	// ---------------------------------------------------------------- channels and events

	/** The dialogue running (or paused) on a channel; None is main. Null when the channel is quiet. */
	UFUNCTION(BlueprintPure, Category = "Narrative|Story")
	UDreamNarrativeDialogueRunner* GetRunner(FName Channel) const;

	/** True while a dialogue runs, not paused, on a channel that locks input. */
	UFUNCTION(BlueprintPure, Category = "Narrative|Story")
	bool IsInputLocked() const;

	/** Every channel, main first, then in the order the scripts declared them. */
	void GetChannelNames(TArray<FName>& Out) const { Out = ChannelOrder; }
	const FDnaChannelData* FindChannel(FName Channel) const;

	/** How many dialogues wait their turn on a channel. */
	int32 GetQueueLength(FName Channel) const;

	/** Ends every dialogue on every channel, and drops what waited its turn. */
	UFUNCTION(BlueprintCallable, Category = "Narrative|Story")
	void StopAllDialogue();

	/** An event was raised; fires before any dialogue it opens starts. */
	FDnaEventHeardSignature OnEventHeard;

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

	// ---------------------------------------------------------------- quests

	/** Takes a quest on, as the game. False, with a warning, when it is not available right now. */
	UFUNCTION(BlueprintCallable, Category = "Narrative|Quest")
	bool AcceptQuest(FName Quest) { return RequestQuest(EDnaQuestRequest::Accept, Quest, EDnaWriteSource::Game); }

	/** Completes an active quest, as the game. False, with a warning, when it is not active. */
	UFUNCTION(BlueprintCallable, Category = "Narrative|Quest")
	bool CompleteQuest(FName Quest) { return RequestQuest(EDnaQuestRequest::Complete, Quest, EDnaWriteSource::Game); }

	/** Fails an active quest, as the game. False, with a warning, when it is not active. */
	UFUNCTION(BlueprintCallable, Category = "Narrative|Quest")
	bool FailQuest(FName Quest) { return RequestQuest(EDnaQuestRequest::Fail, Quest, EDnaWriteSource::Game); }

	/** Accept, complete or fail, saying who asked: the story's own commands ask as the narrative. */
	bool RequestQuest(EDnaQuestRequest What, FName Quest, EDnaWriteSource Who);

	UFUNCTION(BlueprintPure, Category = "Narrative|Quest")
	EDnaQuestState GetQuestStateOf(FName Quest) const;

	/** Not active and not completed, and its `available if` holds (failed and timed-out quests can be taken again). */
	UFUNCTION(BlueprintPure, Category = "Narrative|Quest")
	bool IsQuestAvailable(FName Quest) const;

	/** The journal line for where the quest stands, interpolated now; empty for an inactive quest. */
	UFUNCTION(BlueprintPure, Category = "Narrative|Quest")
	FText GetQuestJournal(FName Quest) const;

	UFUNCTION(BlueprintPure, Category = "Narrative|Quest")
	TArray<FDnaObjectiveView> GetQuestObjectives(FName Quest) const;

	UFUNCTION(BlueprintPure, Category = "Narrative|Quest")
	bool HasQuest(FName Quest) const { return QuestIndex.Contains(Quest); }

	void GetQuestNames(TArray<FName>& Out) const { QuestIndex.GetKeys(Out); }
	const FDnaQuestData* FindQuest(FName Quest, const UDnaQuestSection** OutSection = nullptr) const;

	UPROPERTY(BlueprintAssignable, Category = "Narrative|Quest")
	FDnaQuestStateChangedSignature OnQuestStateChanged;

	/**
	 * What the player sees of an active quest moved: an objective got done, it went from Accepted to
	 * Going, or a fact its objectives or journal read changed. At most once per quest per batch.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Narrative|Quest")
	FDnaQuestUpdatedSignature OnQuestUpdated;

	/** A quest's `on` block paid out; the rewards are data for the game to act on. */
	UPROPERTY(BlueprintAssignable, Category = "Narrative|Quest")
	FDnaQuestRewardsSignature OnQuestRewards;

	/** A quest's `on` block played a stage; the game plays it, nothing waits for it. */
	UPROPERTY(BlueprintAssignable, Category = "Narrative|Quest")
	FDnaQuestStageSignature OnQuestStage;

	/** Someone asked to accept, complete or fail a quest (before it happens, whether it then can or not). */
	FDnaQuestRequestedSignature OnQuestRequested;

	// IDnaQuestStateProvider
	virtual bool GetQuestState(FName Quest, EDnaQuestStateQuery Query) const override;

private:
	// channels and events (DreamNarrativeChannels.cpp)

	/**
	 * Starts Node on its channel. Requested (the game asked): it starts now, ending what ran there.
	 * Otherwise (an event opened it) the channel's busy rule decides. Null when it did not start now.
	 */
	UDreamNarrativeDialogueRunner* StartOnChannel(FName Node, const FDnaEventPayload* Event, bool bRequested);
	UDreamNarrativeDialogueRunner* StartNow(FName Channel, FName Node, const FDnaDialogueNodeData& Data, const UDnaDialogueSection& Section, const FDnaEventPayload* Event);
	FName ChannelOf(const FDnaDialogueNodeData& Data) const;
	FDnaChannelState& StateOf(FName Channel);
	void Enqueue(FName Channel, FDnaChannelQueueItem&& Item);
	/** The channel's dialogue ended or was taken off it: the next in line goes on, or it falls quiet. */
	void ContinueChannel(FName Channel);
	void BecameBusy(FName Channel);
	void BecameQuiet(FName Channel);
	void PauseChannel(FName Channel);
	void UnpauseChannel(FName Channel);
	void StopChannel(FName Channel, FName By);
	/** Takes the running dialogue off the channel: aborted, or held to go on later when it resumes. */
	void CutOff(FName Channel, bool bHoldToResume, FName ParkFor);
	void HandleEventRaised(const FDnaEventPayload& Event, EDnaWriteSource Source);
	FName SelectEventEntry(FName Event, FName Channel, const FDnaEventPayload& Payload) const;
	/** `once` used up, `cooldown` running, or (for an event) already running or waiting its turn. */
	bool IsEntrySpent(const FDnaDialogueNodeData& Data, bool bForEvent) const;
	bool IsRunningOrQueued(FName Node) const;
	void EnsureMainChannel();

	// quests (DreamNarrativeQuests.cpp)
	void WatchLedger();
	void UnwatchLedger();
	void HandleBatchCommitted(const TArray<FName>& Changed);
	void HandleTimerFired(FName Timer);
	void HandleSnapshotApplied();
	void HandleSnapshotApplying();
	/** Changed: the facts the batch being committed changed, if any. */
	void CheckQuests(TConstArrayView<FName> Changed = {});
	void EvaluateQuest(const FDnaQuestData& Quest, const UDreamNarrativeScript& Script, TConstArrayView<FName> Changed);
	void SetQuestState(FName Quest, EDnaQuestState State);
	bool AcceptNow(FName Quest);
	void Finish(FName Quest, EDnaQuestState State);
	void RunHandler(FName Quest, const FDnaQuestHandlerData& Handler, const UDreamNarrativeScript& Script);
	bool Holds(int32 Expr, const UDreamNarrativeScript& Script) const;
	FText Format(const FDnaQuestText& Text, const UDreamNarrativeScript& Script) const;
	/** The values of the facts a quest's objectives read, to tell Accepted from Going. */
	TArray<FDnaValue> ReadValues(const FDnaQuestData& Quest, const UDreamNarrativeScript& Script) const;

	struct FQuestRef
	{
		TWeakObjectPtr<const UDnaQuestSection> Section;
		int32 QuestIndex = INDEX_NONE;
	};

	TMap<FName, FQuestRef> QuestIndex;
	/** Quests in the order they were declared, which is the order they are checked in. */
	TArray<FName> QuestOrder;
	/** For quests still Accepted: what their objectives read when they were taken on. */
	TMap<FName, TArray<FDnaValue>> Baselines;
	FDelegateHandle CommitHandle;
	FDelegateHandle TimerHandle;
	FDelegateHandle SnapshotHandle;
	FDelegateHandle ApplyingHandle;
	FDelegateHandle EventHandle;

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
	TMap<FName, FDnaChannelState> Channels;

	UPROPERTY()
	TObjectPtr<UDreamNarrativeRecorder> Recorder;

	TMap<FName, FNodeRef> NodeIndex;
	/** Entry nodes per character, in declaration order. */
	TMap<FName, TArray<FName>> EntriesByCharacter;
	/** Entry nodes per event, in declaration order. */
	TMap<FName, TArray<FName>> EntriesByEvent;
	TMap<FName, FDnaChannelData> ChannelDecls;
	/** main first, then in the order the scripts declared them. */
	TArray<FName> ChannelOrder;
	/** When each node last started, on the ledger's clock: what `cooldown` counts from. Not saved. */
	TMap<FName, double> LastStarted;
};
