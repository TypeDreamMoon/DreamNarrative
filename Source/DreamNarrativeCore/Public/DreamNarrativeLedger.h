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
#include "DreamNarrativeExpression.h"
#include "DreamNarrativeScript.h"
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

/** Everything the ledger needs to come back exactly as it was. Session facts are left out. */
USTRUCT(BlueprintType)
struct DREAMNARRATIVECORE_API FDnaLedgerSnapshot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, SaveGame, Category = "Narrative")
	int32 SchemaVersion = 1;

	UPROPERTY(BlueprintReadWrite, SaveGame, Category = "Narrative")
	TArray<FDnaFactSnapshotEntry> Facts;

	UPROPERTY(BlueprintReadWrite, SaveGame, Category = "Narrative")
	FName Chapter;

	UPROPERTY(BlueprintReadWrite, SaveGame, Category = "Narrative")
	TMap<FName, int32> Visited;

	UPROPERTY(BlueprintReadWrite, SaveGame, Category = "Narrative")
	TArray<FName> ChosenOnce;
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

	/** Sets the chapter and broadcasts. Does not touch facts: `advance` is a state change, not a jump. */
	UFUNCTION(BlueprintCallable, Category = "Narrative|Chapter")
	bool SetChapter(FName Chapter);

	/**
	 * The debug teleport: applies the implied facts of every chapter on the `after` chain, first to
	 * last, then sets the chapter. Returns false for an unknown chapter.
	 */
	UFUNCTION(BlueprintCallable, Category = "Narrative|Chapter")
	bool JumpToChapter(FName Chapter);

	/** Chapters in declaration order. */
	void GetChapterNames(TArray<FName>& Out) const { Out = ChapterOrder; }

	// ---------------------------------------------------------------- dialogue bookkeeping

	UFUNCTION(BlueprintPure, Category = "Narrative|Dialogue")
	int32 GetVisited(FName Dialogue) const;

	UFUNCTION(BlueprintCallable, Category = "Narrative|Dialogue")
	void MarkVisited(FName Dialogue);

	UFUNCTION(BlueprintPure, Category = "Narrative|Dialogue")
	bool WasChosenOnce(FName LineId) const { return ChosenOnce.Contains(LineId); }

	UFUNCTION(BlueprintCallable, Category = "Narrative|Dialogue")
	void MarkChosenOnce(FName LineId) { ChosenOnce.Add(LineId); }

	// ---------------------------------------------------------------- events

	/** `emit`: broadcasts a named event to the game. */
	UFUNCTION(BlueprintCallable, Category = "Narrative|Events")
	void EmitEvent(FName Event);

	// ---------------------------------------------------------------- snapshot

	UFUNCTION(BlueprintPure, Category = "Narrative|Save")
	FDnaLedgerSnapshot MakeSnapshot() const;

	/** Restores a snapshot, resolving renamed facts through the redirects the scripts declared. */
	UFUNCTION(BlueprintCallable, Category = "Narrative|Save")
	void ApplySnapshot(const FDnaLedgerSnapshot& Snapshot);

	/** Every fact back to its default, chapter to none, bookkeeping cleared. */
	UFUNCTION(BlueprintCallable, Category = "Narrative|Save")
	void ResetToDefaults();

	// ---------------------------------------------------------------- quests (provided by Story)

	void SetQuestStateProvider(IDnaQuestStateProvider* Provider) { QuestStateProvider = Provider; }

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

	/** Where the ledger, its expressions and everything running on it report what they do; null for nobody. Not owned. */
	void SetTrace(IDnaTrace* InTrace) { Trace = InTrace; }

	// ---------------------------------------------------------------- IDnaEvalContext

	virtual FDnaValue GetFactValue(FName Fact) const override { return GetFact(Fact); }
	virtual FName GetCurrentChapter() const override { return CurrentChapter; }
	virtual bool GetQuestState(FName Quest, EDnaQuestStateQuery Query) const override;
	virtual int32 GetVisitCount(FName Dialogue) const override { return GetVisited(Dialogue); }
	virtual float NextRandomUnit() override { return Random.GetFraction(); }
	virtual IDnaTrace* GetTrace() const override { return Trace; }

	bool EvaluateCondition(const FDnaCompiledExpr& Expr) { return DreamNarrative::EvaluateCondition(Expr, *this); }
	FDnaValue Evaluate(const FDnaCompiledExpr& Expr) { return DreamNarrative::EvaluateExpr(Expr, *this); }

	// ---------------------------------------------------------------- delegates

	UPROPERTY(BlueprintAssignable, Category = "Narrative|Ledger")
	FDnaFactChangedSignature OnFactChanged;

	UPROPERTY(BlueprintAssignable, Category = "Narrative|Chapter")
	FDnaChapterChangedSignature OnChapterChanged;

	UPROPERTY(BlueprintAssignable, Category = "Narrative|Events")
	FDnaEventSignature OnEvent;

	/** Fires after a batch commits with the facts that changed in it. Observers subscribe here. */
	FDnaBatchCommittedSignature OnBatchCommitted;

private:
	/** Follows `was` redirects to the current name. */
	FName Resolve(FName Fact) const;
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
	/** old name -> new name, from `was`. */
	TMap<FName, FName> Redirects;
	/** Chapter rules need the expression pool of the script that declared them. */
	TMap<FName, TWeakObjectPtr<UDreamNarrativeScript>> ChapterScripts;

	FName CurrentChapter;
	TMap<FName, int32> VisitCounts;
	TSet<FName> ChosenOnce;

	int32 BatchDepth = 0;
	TArray<FName> DirtyFacts;
	bool bCommitting = false;

	IDnaQuestStateProvider* QuestStateProvider = nullptr;
	FRandomStream Random;
	TSharedPtr<IDnaClock> Clock;
	IDnaTrace* Trace = nullptr;
};
