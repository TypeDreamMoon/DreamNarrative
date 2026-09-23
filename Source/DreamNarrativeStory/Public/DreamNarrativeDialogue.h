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

/** One speech or narration line. Speaker is None for narration. */
USTRUCT(BlueprintType)
struct DREAMNARRATIVESTORY_API FDnaLineData
{
	GENERATED_BODY()

	/** Stable id: the `#id` the source gave, or `<node>.<ordinal>` until it is stamped. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Narrative")
	FName Id;

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

	UPROPERTY(VisibleAnywhere, Category = "Narrative")
	int32 SourceLine = 0;
};

USTRUCT(BlueprintType)
struct DREAMNARRATIVESTORY_API FDnaChoiceData
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Narrative")
	FName Id;

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

/** The dialogue nodes of one script. */
UCLASS()
class DREAMNARRATIVESTORY_API UDnaDialogueSection : public UDreamNarrativeScriptSection
{
	GENERATED_BODY()

public:
	UPROPERTY(VisibleAnywhere, Category = "Narrative")
	TArray<FDnaDialogueNodeData> Nodes;

	const FDnaDialogueNodeData* FindNode(FName Name) const;
};
