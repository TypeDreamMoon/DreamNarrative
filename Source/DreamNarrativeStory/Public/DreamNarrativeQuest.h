// Copyright DreamMoon. All Rights Reserved.
//
// Compiled quests. A quest holds no progress of its own: progress is the facts its objectives read,
// and its state is a fact too (QuestStateFact). What is compiled here is the rules -- when it may be
// taken, what completes or fails it, what runs on each turn, what it pays out.

#pragma once

#include "CoreMinimal.h"
#include "DreamNarrativeDialogue.h"
#include "DreamNarrativeScript.h"
#include "DreamNarrativeQuest.generated.h"

UENUM(BlueprintType)
enum class EDnaQuestPriority : uint8
{
	Side,
	/** The story's spine: the explorer checks that nothing can lock it out. */
	Main,
	/** Not shown to the player; bookkeeping such as a chain of clues. */
	Hidden,
};

/** A text with its interpolations, as a line carries one. */
USTRUCT()
struct DREAMNARRATIVESTORY_API FDnaQuestText
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Narrative")
	FText Text;

	UPROPERTY(VisibleAnywhere, Category = "Narrative")
	TArray<int32> Args;

	bool IsEmpty() const { return Text.IsEmpty(); }
};

/** `reward 类别 名 xN` / `reward 类别 N`: data handed to the game, never acted on here. */
USTRUCT(BlueprintType)
struct DREAMNARRATIVESTORY_API FDnaReward
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Narrative")
	FName Category;

	/** None for an amount of the category itself (`reward 金币 50`). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Narrative")
	FName Item;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Narrative")
	int32 Amount = 1;
};

/** What an `on accept` / `on complete` / `on fail` / `on timeout` block does. */
USTRUCT()
struct DREAMNARRATIVESTORY_API FDnaQuestHandlerData
{
	GENERATED_BODY()

	/** set, accept, complete, fail, advance, play, emit; run to the end in one go. */
	UPROPERTY(VisibleAnywhere, Category = "Narrative")
	TArray<FDnaInstruction> Code;

	UPROPERTY(VisibleAnywhere, Category = "Narrative")
	TArray<FDnaReward> Rewards;

	bool IsEmpty() const { return Code.Num() == 0 && Rewards.Num() == 0; }
};

USTRUCT()
struct DREAMNARRATIVESTORY_API FDnaObjectiveData
{
	GENERATED_BODY()

	/** Done, and stays done, once this holds while the quest is active. */
	UPROPERTY(VisibleAnywhere, Category = "Narrative")
	int32 Condition = INDEX_NONE;

	/** Not needed for completion; shown and latched all the same. */
	UPROPERTY(VisibleAnywhere, Category = "Narrative")
	bool bOptional = false;

	UPROPERTY(VisibleAnywhere, Category = "Narrative")
	FDnaQuestText Show;

	UPROPERTY(VisibleAnywhere, Category = "Narrative")
	int32 SourceLine = 0;
};

USTRUCT()
struct DREAMNARRATIVESTORY_API FDnaSubquestData
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Narrative")
	FName Quest;

	/** Taken on as soon as the parent completes. */
	UPROPERTY(VisibleAnywhere, Category = "Narrative")
	bool bAuto = false;
};

USTRUCT()
struct DREAMNARRATIVESTORY_API FDnaQuestData
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Narrative")
	FName Name;

	UPROPERTY(VisibleAnywhere, Category = "Narrative")
	FText Title;

	UPROPERTY(VisibleAnywhere, Category = "Narrative")
	EDnaQuestPriority Priority = EDnaQuestPriority::Side;

	/** Shown on the tracking HUD: a flag for the game, nothing here reads it. */
	UPROPERTY(VisibleAnywhere, Category = "Narrative")
	bool bTrack = false;

	/** Narrative seconds from accepting to timing out; 0 for none. */
	UPROPERTY(VisibleAnywhere, Category = "Narrative")
	float Timeout = 0.f;

	/** `gameplay: 标签`: the gameplay event the GAS bridge sends the ability systems it watches when the quest is completed. */
	UPROPERTY(VisibleAnywhere, Category = "Narrative")
	FName GameplayTag;

	/** `available if`: may it be taken on (besides not being active or completed). INDEX_NONE: always. */
	UPROPERTY(VisibleAnywhere, Category = "Narrative")
	int32 Available = INDEX_NONE;

	/** `fail if`: fails it while it is active. */
	UPROPERTY(VisibleAnywhere, Category = "Narrative")
	int32 FailIf = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, Category = "Narrative")
	FDnaQuestText JournalAccepted;

	UPROPERTY(VisibleAnywhere, Category = "Narrative")
	FDnaQuestText JournalGoing;

	UPROPERTY(VisibleAnywhere, Category = "Narrative")
	FDnaQuestText JournalCompleted;

	UPROPERTY(VisibleAnywhere, Category = "Narrative")
	FDnaQuestText JournalFailed;

	UPROPERTY(VisibleAnywhere, Category = "Narrative")
	TArray<FDnaObjectiveData> Objectives;

	UPROPERTY(VisibleAnywhere, Category = "Narrative")
	FDnaQuestHandlerData OnAccept;

	UPROPERTY(VisibleAnywhere, Category = "Narrative")
	FDnaQuestHandlerData OnComplete;

	UPROPERTY(VisibleAnywhere, Category = "Narrative")
	FDnaQuestHandlerData OnFail;

	UPROPERTY(VisibleAnywhere, Category = "Narrative")
	FDnaQuestHandlerData OnTimeout;

	UPROPERTY(VisibleAnywhere, Category = "Narrative")
	TArray<FDnaSubquestData> Subquests;

	UPROPERTY(VisibleAnywhere, Category = "Narrative")
	int32 SourceLine = 0;

	UPROPERTY(VisibleAnywhere, Category = "Narrative")
	FName WasName;
};

/** The quests of one script. */
UCLASS()
class DREAMNARRATIVESTORY_API UDnaQuestSection : public UDreamNarrativeScriptSection
{
	GENERATED_BODY()

public:
	UPROPERTY(VisibleAnywhere, Category = "Narrative")
	TArray<FDnaQuestData> Quests;

	const FDnaQuestData* FindQuest(FName Name) const;
};
