// Copyright DreamMoon. All Rights Reserved.
//
// Compiled dialogue. A dialogue node is a small program: one instruction per source line, with
// `if`/`else` lowered to conditional jumps and choice bodies laid out after the group they belong
// to. The runner walks it with a program counter and yields whenever the game has to do something
// (show a line, present choices, wait).

#pragma once

#include "CoreMinimal.h"
#include "DreamNarrativeScript.h"
#include "DreamNarrativeDialogue.generated.h"

class USoundBase;

UENUM()
enum class EDnaOpCode : uint8
{
	/** Show Lines[Index]; yield until Advance(). Skipped when its condition is false. */
	Line,
	/** Present ChoiceGroups[Index]; yield until Choose(). */
	Choice,
	/** pc = Index. */
	Jump,
	/** If expression Name-less: pc = Index when Expressions[Expr] is false. */
	JumpIfFalse,
	/** Write fact Name from Expressions[Expr] with Assign. */
	Set,
	Accept,
	Complete,
	Fail,
	Advance,
	/** Play stage Name; blocking unless bAsync. */
	Play,
	Emit,
	/** Pause Seconds. */
	Wait,
	/** Leave this node for dialogue Name. */
	GoTo,
	End,
};

UENUM()
enum class EDnaAssignOp : uint8
{
	Set,
	Add,
	Sub,
};

/** What a channel does with a dialogue that arrives while it is talking. */
UENUM(BlueprintType)
enum class EDnaChannelBusy : uint8
{
	/** It waits its turn (by priority, then by arrival). */
	Queue,
	/** It cuts in when its priority is not lower than the one talking; otherwise it waits its turn. */
	Interrupt,
	/** It is dropped. */
	Drop,
};

/**
 * A channel: one dialogue at a time runs on it, alongside the dialogues of other channels. The main
 * channel is built in; `channel 名` blocks declare the others (and may adjust main's pauses and stops).
 */
USTRUCT(BlueprintType)
struct DREAMNARRATIVESTORY_API FDnaChannelData
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Narrative")
	FName Name;

	/** The player cannot act while a dialogue on it runs. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Narrative")
	bool bLockInput = false;

	/** Lines move on by themselves after their time; otherwise the game calls Advance(). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Narrative")
	bool bAutoAdvance = true;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Narrative")
	EDnaChannelBusy Busy = EDnaChannelBusy::Queue;

	/** Channels that hold still while this one talks, and go on where they stopped after. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Narrative")
	TArray<FName> Pauses;

	/** Channels this one cuts off when it starts talking. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Narrative")
	TArray<FName> Stops;

	/** `stage: pause`: it holds still while a stage plays (not one attached to a line), and goes on after. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Narrative")
	bool bPauseDuringStage = false;

	UPROPERTY(VisibleAnywhere, Category = "Narrative")
	int32 SourceLine = 0;
};

namespace DreamNarrative
{
	/** `main`: the channel a dialogue that names none runs on. */
	DREAMNARRATIVESTORY_API FName MainChannelName();
}

/** One speech or narration line. Speaker is None for narration. */
USTRUCT(BlueprintType)
struct DREAMNARRATIVESTORY_API FDnaLineData
{
	GENERATED_BODY()

	/** Stable id: the `#id` the source gave, or `<node>.<ordinal>` until it is stamped. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Narrative")
	FName Id;

	/** The source gave the id (`#id`); otherwise it is derived from the position and moves with edits. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Narrative")
	bool bExplicitId = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Narrative")
	FName Speaker;

	/** Emotion and action tags from the parenthetical, in order. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Narrative")
	TArray<FName> Tags;

	/** The localisable text. Interpolations are `{0}`, `{1}`... filled from TextArgs at display time. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Narrative")
	FText Text;

	/** Expression indices, one per placeholder. */
	UPROPERTY(VisibleAnywhere, Category = "Narrative")
	TArray<int32> TextArgs;

	UPROPERTY(VisibleAnywhere, Category = "Narrative")
	int32 Condition = INDEX_NONE;

	/** Explicit `for` duration in seconds; 0 when the line has none. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Narrative")
	float Duration = 0.f;

	/** Stage attached with `@`, or None. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Narrative")
	FName Stage;

	/** The line's voice, found when the script was compiled (the speaker's convention, a voice table or the line's own `voice`); none when it has none. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Narrative")
	TSoftObjectPtr<USoundBase> Voice;

	/** How long the voice runs, as the asset registry knew it when compiling; 0 when unknown. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Narrative")
	float VoiceSeconds = 0.f;

	UPROPERTY(VisibleAnywhere, Category = "Narrative")
	int32 SourceLine = 0;
};

USTRUCT(BlueprintType)
struct DREAMNARRATIVESTORY_API FDnaChoiceData
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Narrative")
	FName Id;

	/** The source gave the id (`#id`). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Narrative")
	bool bExplicitId = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Narrative")
	FText Label;

	UPROPERTY(VisibleAnywhere, Category = "Narrative")
	TArray<int32> LabelArgs;

	UPROPERTY(VisibleAnywhere, Category = "Narrative")
	int32 Condition = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Narrative")
	bool bOnce = false;

	/** pc of the option's body. */
	UPROPERTY(VisibleAnywhere, Category = "Narrative")
	int32 Target = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, Category = "Narrative")
	int32 SourceLine = 0;
};

USTRUCT()
struct DREAMNARRATIVESTORY_API FDnaChoiceGroupData
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FDnaChoiceData> Options;

	/** pc after the group, where every body falls back to. */
	UPROPERTY()
	int32 Gather = INDEX_NONE;
};

USTRUCT()
struct DREAMNARRATIVESTORY_API FDnaInstruction
{
	GENERATED_BODY()

	UPROPERTY()
	EDnaOpCode Op = EDnaOpCode::End;

	/** Line index, choice group index, or jump target, by Op. */
	UPROPERTY()
	int32 Index = INDEX_NONE;

	/** Expression index for JumpIfFalse and Set. */
	UPROPERTY()
	int32 Expr = INDEX_NONE;

	/** Fact, quest, chapter, stage, event or dialogue name, by Op. */
	UPROPERTY()
	FName Name;

	UPROPERTY()
	EDnaAssignOp Assign = EDnaAssignOp::Set;

	UPROPERTY()
	float Seconds = 0.f;

	UPROPERTY()
	bool bAsync = false;

	/** For Emit: the parameters given, and the expression each takes its value from. */
	UPROPERTY()
	TArray<FName> ArgNames;

	UPROPERTY()
	TArray<int32> ArgExprs;

	UPROPERTY()
	int32 SourceLine = 0;
};

USTRUCT(BlueprintType)
struct DREAMNARRATIVESTORY_API FDnaDialogueNodeData
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Narrative")
	FName Name;

	/** The character this node is an entry for, or None. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Narrative")
	FName ForCharacter;

	UPROPERTY(VisibleAnywhere, Category = "Narrative")
	int32 Condition = INDEX_NONE;

	/** Explicit priority, or the number of condition terms when none was given. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Narrative")
	int32 Priority = 0;

	/** The source wrote `priority n`. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Narrative")
	bool bExplicitPriority = false;

	/** The event this node is an entry for (`on 事件`), or None. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Narrative")
	FName OnEvent;

	/** The channel it runs on; None is main. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Narrative")
	FName Channel;

	/** `once`: as an entry it opens only while it has never been left. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Narrative")
	bool bOnce = false;

	/** `cooldown`: as an entry it opens only this many narrative seconds after it last started. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Narrative")
	float Cooldown = 0.f;

	/** `resume`: cut off, it waits and goes on from the line it was cut at, instead of ending. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Narrative")
	bool bResume = false;

	UPROPERTY(VisibleAnywhere, Category = "Narrative")
	TArray<FDnaInstruction> Code;

	UPROPERTY(VisibleAnywhere, Category = "Narrative")
	TArray<FDnaLineData> Lines;

	UPROPERTY(VisibleAnywhere, Category = "Narrative")
	TArray<FDnaChoiceGroupData> ChoiceGroups;

	UPROPERTY(VisibleAnywhere, Category = "Narrative")
	int32 SourceLine = 0;

	UPROPERTY(VisibleAnywhere, Category = "Narrative")
	FName WasName;
};

/** The dialogue nodes of one script, and the channels it declares. */
UCLASS()
class DREAMNARRATIVESTORY_API UDnaDialogueSection : public UDreamNarrativeScriptSection
{
	GENERATED_BODY()

public:
	UPROPERTY(VisibleAnywhere, Category = "Narrative")
	TArray<FDnaDialogueNodeData> Nodes;

	UPROPERTY(VisibleAnywhere, Category = "Narrative")
	TArray<FDnaChannelData> Channels;

	const FDnaDialogueNodeData* FindNode(FName Name) const;
};
