// Copyright DreamMoon. All Rights Reserved.
//
// The fact ledger: the one place the plugin keeps state. Dialogue reads and writes it, quests watch
// it, chapters gate on it, the game feeds it through `extern` facts. It lives on the game instance so
// it survives level travel.
//
// Writes happen in batches. A dialogue option that sets three facts commits once, and observers
// (quests, chapter rules) re-evaluate once, after the batch -- never in the middle of it. A write
// outside any batch is a batch of one.

#pragma once

#include "CoreMinimal.h"
#include "DreamNarrativeClock.h"
#include "DreamNarrativeEvent.h"
#include "DreamNarrativeExpression.h"
#include "DreamNarrativeScript.h"
#include "DreamNarrativeStageHost.h"
#include "DreamNarrativeTrace.h"
#include "DreamNarrativeTypes.h"
#include "Math/RandomStream.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "DreamNarrativeLedger.generated.h"

// Every property of the snapshot is marked SaveGame, down to the values, so the snapshot survives an
// archive that only writes SaveGame properties (ArIsSaveGame) as well as one that writes them all.

USTRUCT(BlueprintType)
struct DREAMNARRATIVECORE_API FDnaFactSnapshotEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, SaveGame, Category = "Narrative")
	FName Fact;

	UPROPERTY(BlueprintReadWrite, SaveGame, Category = "Narrative")
	FDnaValue Value;
};

/**
 * The story as it stood when it first entered a chapter: what selecting the chapter goes back to.
 * Progress facts are left out (selecting keeps them as they are), and so are session and profile facts.
 */
USTRUCT(BlueprintType)
struct DREAMNARRATIVECORE_API FDnaChapterCheckpoint
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, SaveGame, Category = "Narrative")
	FName Chapter;

	UPROPERTY(BlueprintReadWrite, SaveGame, Category = "Narrative")
	TArray<FDnaFactSnapshotEntry> Facts;

	UPROPERTY(BlueprintReadWrite, SaveGame, Category = "Narrative")
	TMap<FName, int32> Visited;

	UPROPERTY(BlueprintReadWrite, SaveGame, Category = "Narrative")
	TArray<FName> ChosenOnce;

	UPROPERTY(BlueprintReadWrite, SaveGame, Category = "Narrative")
	TMap<FName, float> Timers;
};

/**
 * Everything of the save slot the ledger needs to come back exactly as it was. Session facts are
 * left out, and so are profile facts: they are in the profile snapshot.
 */
USTRUCT(BlueprintType)
struct DREAMNARRATIVECORE_API FDnaLedgerSnapshot
{
	GENERATED_BODY()

	/** 1: no checkpoints. 2: checkpoints. An older one loads as it was written. */
	UPROPERTY(BlueprintReadWrite, SaveGame, Category = "Narrative")
	int32 SchemaVersion = 2;

	UPROPERTY(BlueprintReadWrite, SaveGame, Category = "Narrative")
	TArray<FDnaFactSnapshotEntry> Facts;

	UPROPERTY(BlueprintReadWrite, SaveGame, Category = "Narrative")
	FName Chapter;

	UPROPERTY(BlueprintReadWrite, SaveGame, Category = "Narrative")
	TMap<FName, int32> Visited;

	UPROPERTY(BlueprintReadWrite, SaveGame, Category = "Narrative")
	TArray<FName> ChosenOnce;

	/** Timers still running, with the narrative seconds each had left. */
	UPROPERTY(BlueprintReadWrite, SaveGame, Category = "Narrative")
	TMap<FName, float> Timers;

	/** One per chapter the story has entered, in the order it entered them. */
	UPROPERTY(BlueprintReadWrite, SaveGame, Category = "Narrative")
	TArray<FDnaChapterCheckpoint> Checkpoints;
};

/** The profile facts: what outlives a save slot and a playthrough (endings seen, the route). Where it is kept is the game's. */
USTRUCT(BlueprintType)
struct DREAMNARRATIVECORE_API FDnaProfileSnapshot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, SaveGame, Category = "Narrative")
	int32 SchemaVersion = 1;

	UPROPERTY(BlueprintReadWrite, SaveGame, Category = "Narrative")
	TArray<FDnaFactSnapshotEntry> Facts;
};

/**
 * Decides what `random` and `chance` draw. The ledger draws from its own seeded stream until a tool
 * puts a source in charge: a test forcing the next outcomes, an explorer trying each of them.
 */
class DREAMNARRATIVECORE_API IDnaRandomSource
{
public:
	virtual ~IDnaRandomSource() = default;

	/** The next draw, uniform in [0, 1). Stream is the ledger's own, for a source that decides only some draws. */
	virtual float NextUnit(FRandomStream& Stream) = 0;
};

/** Story registers one of these so expressions can ask about quests without Core knowing quests. */
class DREAMNARRATIVECORE_API IDnaQuestStateProvider
{
public:
	virtual ~IDnaQuestStateProvider() = default;
	virtual bool GetQuestState(FName Quest, EDnaQuestStateQuery Query) const = 0;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FDnaFactChangedSignature, FName, Fact, FDnaValue, OldValue, FDnaValue, NewValue);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDnaChapterChangedSignature, FName, OldChapter, FName, NewChapter);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDnaEventSignature, FName, Event);
DECLARE_MULTICAST_DELEGATE_OneParam(FDnaBatchCommittedSignature, const TArray<FName>& /*ChangedFacts*/);
DECLARE_MULTICAST_DELEGATE_ThreeParams(FDnaFactWrittenSignature, FName /*Fact*/, const FDnaValue& /*Value*/, EDnaWriteSource /*Source*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FDnaJumpRequestedSignature, FName /*Chapter*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FDnaTimerFiredSignature, FName /*Timer*/);
DECLARE_MULTICAST_DELEGATE(FDnaSnapshotAppliedSignature);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDnaEventPayloadSignature, const FDnaEventPayload&, Event);
DECLARE_MULTICAST_DELEGATE_TwoParams(FDnaEventRaisedSignature, const FDnaEventPayload& /*Event*/, EDnaWriteSource /*Source*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FDnaScriptRegisteredSignature, UDreamNarrativeScript* /*Script*/);
DECLARE_MULTICAST_DELEGATE(FDnaNewPlaythroughSignature);
DECLARE_MULTICAST_DELEGATE_OneParam(FDnaChapterSelectSignature, FName /*Chapter*/);

UCLASS()
class DREAMNARRATIVECORE_API UDreamNarrativeLedger : public UGameInstanceSubsystem, public IDnaEvalContext
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ---------------------------------------------------------------- declarations

	/** Declares the script's facts, chapters and characters. Idempotent; a re-registration updates. */
	UFUNCTION(BlueprintCallable, Category = "Narrative|Ledger")
	void RegisterScript(UDreamNarrativeScript* Script);

	UFUNCTION(BlueprintPure, Category = "Narrative|Ledger")
	bool HasFact(FName Fact) const { return FactDecls.Contains(Resolve(Fact)); }

	const FDnaFactDecl* FindFactDecl(FName Fact) const;
	const FDnaChapterDecl* FindChapterDecl(FName Chapter) const;
	const FDnaCharacterDecl* FindCharacterDecl(FName Character) const;
	const FDnaEventDecl* FindEventDecl(FName Event) const { return EventDecls.Find(Event); }
	void GetEventNames(TArray<FName>& Out) const { EventDecls.GetKeys(Out); }
	void GetFactNames(TArray<FName>& Out) const { FactDecls.GetKeys(Out); }

	/** The scripts registered so far. */
	const TArray<TObjectPtr<UDreamNarrativeScript>>& GetScripts() const { return Scripts; }

	// ---------------------------------------------------------------- facts

	UFUNCTION(BlueprintPure, Category = "Narrative|Ledger")
	FDnaValue GetFact(FName Fact) const;

	UFUNCTION(BlueprintPure, Category = "Narrative|Ledger")
	bool GetFactAsBool(FName Fact) const { return GetFact(Fact).AsBool(); }

	UFUNCTION(BlueprintPure, Category = "Narrative|Ledger")
	int32 GetFactAsInt(FName Fact) const { return static_cast<int32>(GetFact(Fact).AsNumber()); }

	/**
	 * Writes a fact. Returns false when the write was refused: an undeclared fact, a value of the
	 * wrong type or outside the declared set, or game code writing a narrative-owned fact while the
	 * ownership rule is enforced.
	 */
	bool SetFact(FName Fact, const FDnaValue& Value, EDnaWriteSource Source = EDnaWriteSource::Game);

	UFUNCTION(BlueprintCallable, Category = "Narrative|Ledger", meta = (DisplayName = "Set Fact"))
	bool SetFactFromGame(FName Fact, FDnaValue Value) { return SetFact(Fact, Value, EDnaWriteSource::Game); }

	UFUNCTION(BlueprintCallable, Category = "Narrative|Ledger")
	bool SetFactBool(FName Fact, bool bValue) { return SetFact(Fact, FDnaValue::FromBool(bValue), EDnaWriteSource::Game); }

	UFUNCTION(BlueprintCallable, Category = "Narrative|Ledger")
	bool SetFactInt(FName Fact, int32 Value) { return SetFact(Fact, FDnaValue::FromInt(Value), EDnaWriteSource::Game); }

	UFUNCTION(BlueprintCallable, Category = "Narrative|Ledger")
	bool SetFactFloat(FName Fact, float Value) { return SetFact(Fact, FDnaValue::FromFloat(Value), EDnaWriteSource::Game); }

	UFUNCTION(BlueprintCallable, Category = "Narrative|Ledger")
	bool SetFactTag(FName Fact, FName Member) { return SetFact(Fact, FDnaValue::FromTag(Member), EDnaWriteSource::Game); }

	/** `+=` for numeric facts. */
	UFUNCTION(BlueprintCallable, Category = "Narrative|Ledger")
	bool AddToFact(FName Fact, float Delta);

	// ---------------------------------------------------------------- batches

	/** Opens a batch; observers are told once, at the matching EndBatch. Nests. */
	UFUNCTION(BlueprintCallable, Category = "Narrative|Ledger")
	void BeginBatch();

	UFUNCTION(BlueprintCallable, Category = "Narrative|Ledger")
	void EndBatch();

	struct FBatchScope
	{
		explicit FBatchScope(UDreamNarrativeLedger& InLedger) : Ledger(InLedger) { Ledger.BeginBatch(); }
		~FBatchScope() { Ledger.EndBatch(); }
	private:
		UDreamNarrativeLedger& Ledger;
	};

	// ---------------------------------------------------------------- chapter

	UFUNCTION(BlueprintPure, Category = "Narrative|Chapter")
	FName GetChapter() const { return CurrentChapter; }

	/**
	 * Sets the chapter and broadcasts. Does not touch facts: `advance` is a state change, not a jump.
	 * The story moving on (bLeaveCheckpoint) leaves a checkpoint the first time it enters a chapter.
	 */
	UFUNCTION(BlueprintCallable, Category = "Narrative|Chapter")
	bool SetChapter(FName Chapter, bool bLeaveCheckpoint = true);

	/**
	 * The debug teleport: applies the implied facts of every chapter on the `after` chain, first to
	 * last, then sets the chapter. Returns false for an unknown chapter.
	 */
	UFUNCTION(BlueprintCallable, Category = "Narrative|Chapter")
	bool JumpToChapter(FName Chapter);

	/** Chapters in declaration order. */
	void GetChapterNames(TArray<FName>& Out) const { Out = ChapterOrder; }

	// ---------------------------------------------------------------- playthroughs and chapter select

	/**
	 * Starts the story over: the save slot's layer back to its defaults (checkpoints too), the profile
	 * kept as it is; then the first chapter is entered, leaving its checkpoint.
	 */
	UFUNCTION(BlueprintCallable, Category = "Narrative|Chapter")
	void StartNewPlaythrough();

	/**
	 * Goes back to the checkpoint the story left when it first entered Chapter, keeping progress as it
	 * is now: progress facts (side quests' among them) and their timers. False when there is none.
	 */
	UFUNCTION(BlueprintCallable, Category = "Narrative|Chapter")
	bool SelectChapter(FName Chapter);

	/** The chapters that can be selected, in the order the story first entered them. */
	UFUNCTION(BlueprintPure, Category = "Narrative|Chapter")
	TArray<FName> GetChapterCheckpoints() const;

	// ---------------------------------------------------------------- dialogue bookkeeping

	UFUNCTION(BlueprintPure, Category = "Narrative|Dialogue")
	int32 GetVisited(FName Dialogue) const;

	UFUNCTION(BlueprintCallable, Category = "Narrative|Dialogue")
	void MarkVisited(FName Dialogue);

	/** Sets a visit count outright, for tools that put the story in a given state. */
	UFUNCTION(BlueprintCallable, Category = "Narrative|Dialogue")
	void SetVisited(FName Dialogue, int32 Count);

	UFUNCTION(BlueprintPure, Category = "Narrative|Dialogue")
	bool WasChosenOnce(FName LineId) const { return ChosenOnce.Contains(LineId); }

	UFUNCTION(BlueprintCallable, Category = "Narrative|Dialogue")
	void MarkChosenOnce(FName LineId) { ChosenOnce.Add(LineId); }

	// ---------------------------------------------------------------- events

	/** Raises an event that carries nothing, as the game. */
	UFUNCTION(BlueprintCallable, Category = "Narrative|Events")
	void EmitEvent(FName Event);

	/**
	 * Raises an event, as the game: dialogues that open `on` it start, and the game hears it back.
	 * A declared event gets the defaults of the parameters it leaves out; a value that does not fit
	 * its parameter is dropped with a warning, so the default stands in.
	 */
	UFUNCTION(BlueprintCallable, Category = "Narrative|Events")
	void RaiseEvent(const FDnaEventPayload& Event) { RaiseEventAs(Event, EDnaWriteSource::Game); }

	/** The same, saying who raised it: the story's own `emit` raises as the narrative. */
	void RaiseEventAs(const FDnaEventPayload& Event, EDnaWriteSource Source);

	// ---------------------------------------------------------------- snapshot

	UFUNCTION(BlueprintPure, Category = "Narrative|Save")
	FDnaLedgerSnapshot MakeSnapshot() const;

	/** Restores a snapshot, resolving renamed facts through the redirects the scripts declared. */
	UFUNCTION(BlueprintCallable, Category = "Narrative|Save")
	void ApplySnapshot(const FDnaLedgerSnapshot& Snapshot);

	/** The save slot's layer back to defaults: every fact but the profile's, chapter to none, bookkeeping and checkpoints cleared, timers stopped. */
	UFUNCTION(BlueprintCallable, Category = "Narrative|Save")
	void ResetToDefaults();

	/** The profile facts, for the game to keep apart from any save slot. */
	UFUNCTION(BlueprintPure, Category = "Narrative|Save")
	FDnaProfileSnapshot MakeProfileSnapshot() const;

	/** Puts the profile facts back (those it does not carry to their defaults); the save slot's layer is left alone. */
	UFUNCTION(BlueprintCallable, Category = "Narrative|Save")
	void ApplyProfileSnapshot(const FDnaProfileSnapshot& Snapshot);

	// ---------------------------------------------------------------- timers

	/**
	 * Starts (or restarts) a named timer on the ledger's clock: a quest's timeout, say. When it runs out,
	 * OnTimerFired says so. Timers are part of the snapshot: a save keeps what each had left.
	 */
	void StartTimer(FName Timer, double Seconds);

	void CancelTimer(FName Timer);

	bool IsTimerRunning(FName Timer) const { return Timers.Contains(Timer); }

	/** Narrative seconds the timer has left; negative when it is not running. */
	double GetTimerRemaining(FName Timer) const;

	// ---------------------------------------------------------------- quests (provided by Story)

	void SetQuestStateProvider(IDnaQuestStateProvider* Provider) { QuestStateProvider = Provider; }

	// ---------------------------------------------------------------- stages (played by Stage)

	/** Who plays the stages the story asks for; null for nobody (the story then only tells the game). Not owned. */
	void SetStageHost(IDnaStageHost* Host) { StageHost = Host; }
	IDnaStageHost* GetStageHost() const { return StageHost; }

	/** The host's stages starting and ending, for what holds still while one plays. The host broadcasts it. */
	FDnaStageActivitySignature OnStageActivity;

	// ---------------------------------------------------------------- time and chance

	/**
	 * The clock every narrative wait runs on. Until one is installed it is the game's clock (world
	 * time through the game instance's timers, standing still while the game is paused).
	 */
	IDnaClock& GetClock();

	/** Replaces the clock; tests and tools install a manual one. Null goes back to the game's clock. */
	void SetClock(TSharedPtr<IDnaClock> InClock) { Clock = MoveTemp(InClock); }

	/** Restarts `random` and `chance` from a seed, so a run can be repeated. */
	UFUNCTION(BlueprintCallable, Category = "Narrative|Ledger")
	void SetRandomSeed(int32 Seed) { Random.Initialize(Seed); }

	/** Puts a source in charge of `random` and `chance`; null goes back to the seeded stream. Not owned. */
	void SetRandomSource(IDnaRandomSource* InSource) { RandomSource = InSource; }
	IDnaRandomSource* GetRandomSource() const { return RandomSource; }

	/** Where the ledger, its expressions and everything running on it report what they do; null for nobody. Not owned. */
	void SetTrace(IDnaTrace* InTrace) { Trace = InTrace; }

	// ---------------------------------------------------------------- IDnaEvalContext

	virtual FDnaValue GetFactValue(FName Fact) const override { return GetFact(Fact); }
	virtual FName GetCurrentChapter() const override { return CurrentChapter; }
	virtual bool GetQuestState(FName Quest, EDnaQuestStateQuery Query) const override;
	virtual int32 GetVisitCount(FName Dialogue) const override { return GetVisited(Dialogue); }
	virtual float NextRandomUnit() override { return RandomSource ? RandomSource->NextUnit(Random) : Random.GetFraction(); }
	virtual IDnaTrace* GetTrace() const override { return Trace; }

	bool EvaluateCondition(const FDnaCompiledExpr& Expr) { return DreamNarrative::EvaluateCondition(Expr, *this); }
	FDnaValue Evaluate(const FDnaCompiledExpr& Expr) { return DreamNarrative::EvaluateExpr(Expr, *this); }

	// ---------------------------------------------------------------- delegates

	UPROPERTY(BlueprintAssignable, Category = "Narrative|Ledger")
	FDnaFactChangedSignature OnFactChanged;

	UPROPERTY(BlueprintAssignable, Category = "Narrative|Chapter")
	FDnaChapterChangedSignature OnChapterChanged;

	/** Every event raised, by name (the game's events and the story's `emit` alike). */
	UPROPERTY(BlueprintAssignable, Category = "Narrative|Events")
	FDnaEventSignature OnEvent;

	/** Every event raised, with what it carries. */
	UPROPERTY(BlueprintAssignable, Category = "Narrative|Events")
	FDnaEventPayloadSignature OnEventPayload;

	/** Every event raised, with what it carries and who raised it: what the story listens to. */
	FDnaEventRaisedSignature OnEventRaised;

	/** Fires after a batch commits with the facts that changed in it. Observers subscribe here. */
	FDnaBatchCommittedSignature OnBatchCommitted;

	/** A script's declarations are in: what reads them (the GAS bridge) looks again. */
	FDnaScriptRegisteredSignature OnScriptRegistered;

	/** Fires for every write that changes a fact, saying who wrote it: what a recorder keeps of the game's writes. */
	FDnaFactWrittenSignature OnFactWritten;

	/** Fires when a debug teleport starts, before it writes the implied facts. */
	FDnaJumpRequestedSignature OnJumpRequested;

	/** A new playthrough: fires once the save slot's layer is back to defaults, before the first chapter is entered. */
	FDnaNewPlaythroughSignature OnNewPlaythrough;

	/** A chapter is selected: fires before the story goes back to its checkpoint. */
	FDnaChapterSelectSignature OnChapterSelected;

	/** Fires when a timer runs out; it is no longer running by then. */
	FDnaTimerFiredSignature OnTimerFired;

	/** Fires as a snapshot starts going in, before anything of the story it replaces is touched. */
	FDnaSnapshotAppliedSignature OnSnapshotApplying;

	/** Fires once a snapshot is in place: what observers kept about the ledger before it is stale now. */
	FDnaSnapshotAppliedSignature OnSnapshotApplied;

	/** True while a snapshot goes in: the batches it commits restore a story, nothing happens in it. */
	bool IsApplyingSnapshot() const { return bApplyingSnapshot; }

private:
	/** Follows `was` redirects to the current name. */
	FName Resolve(FName Fact) const;
	/** Left out of a checkpoint and kept by a selected chapter. */
	bool IsProgress(FName Fact) const;
	void LeaveCheckpoint(FName Chapter);
	void ApplyImplied(const FDnaChapterDecl& Chapter);
	void CheckAdvanceRules();
	void Commit();

	UPROPERTY()
	TArray<TObjectPtr<UDreamNarrativeScript>> Scripts;

	TMap<FName, FDnaFactDecl> FactDecls;
	TMap<FName, FDnaValue> Values;
	TMap<FName, FDnaChapterDecl> ChapterDecls;
	TArray<FName> ChapterOrder;
	TMap<FName, FDnaCharacterDecl> CharacterDecls;
	TMap<FName, FDnaEventDecl> EventDecls;
	/** old name -> new name, from `was`. */
	TMap<FName, FName> Redirects;
	/** Chapter rules need the expression pool of the script that declared them. */
	TMap<FName, TWeakObjectPtr<UDreamNarrativeScript>> ChapterScripts;

	FName CurrentChapter;
	TMap<FName, int32> VisitCounts;
	TSet<FName> ChosenOnce;
	TArray<FDnaChapterCheckpoint> Checkpoints;

	int32 BatchDepth = 0;
	TArray<FName> DirtyFacts;
	bool bCommitting = false;
	bool bApplyingSnapshot = false;

	IDnaQuestStateProvider* QuestStateProvider = nullptr;
	IDnaStageHost* StageHost = nullptr;
	FRandomStream Random;
	IDnaRandomSource* RandomSource = nullptr;
	TSharedPtr<IDnaClock> Clock;
	TMap<FName, FDnaClockHandle> Timers;
	IDnaTrace* Trace = nullptr;
};
