// Copyright DreamMoon. All Rights Reserved.
//
// Events: what the game (or the story itself) says has just happened -- an enemy spotted, an area
// entered -- with a few named values. A script declares the events it listens for and their
// parameters; a dialogue that opens `on` an event reads them as `事件.参数`. Events are not facts:
// nothing keeps them, a save never sees them, and a parameter means something only while the
// dialogue the event opened runs.

#pragma once

#include "CoreMinimal.h"
#include "DreamNarrativeExpression.h"
#include "DreamNarrativeTypes.h"
#include "DreamNarrativeEvent.generated.h"

/** One value an event carries. */
USTRUCT(BlueprintType)
struct DREAMNARRATIVECORE_API FDnaEventArg
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Narrative")
	FName Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Narrative")
	FDnaValue Value;
};

/** An event as raised: its name and the values it carries. */
USTRUCT(BlueprintType)
struct DREAMNARRATIVECORE_API FDnaEventPayload
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Narrative")
	FName Event;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Narrative")
	TArray<FDnaEventArg> Args;

	const FDnaValue* Find(FName Param) const;

	/** Sets Param, replacing a value it already carries. */
	void Set(FName Param, const FDnaValue& Value);

	/** `发现敌人(类型 = 大型, 数量 = 2)`, or the bare name when it carries nothing. */
	FString ToString() const;
};

/** Where a parameter of an event raised from a gameplay event takes its value (`from ...`). */
UENUM(BlueprintType)
enum class EDnaGameplaySource : uint8
{
	None,
	/** `from magnitude`: the gameplay event's magnitude. */
	Magnitude,
	/** `from instigator tags 标签`: a bool is whether the instigator's tags have the tag; a tag is the member named like the child of the tag they have. */
	InstigatorTags,
	/** `from target tags 标签`: the same, of the target's tags. */
	TargetTags,
};

/** One parameter's `from ...`. */
USTRUCT(BlueprintType)
struct DREAMNARRATIVECORE_API FDnaEventParamSource
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Narrative")
	EDnaGameplaySource Source = EDnaGameplaySource::None;

	/** For the tag sources: the tag looked for, or the parent of the tags looked for. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Narrative")
	FName Tag;
};

/** An event as declared in source: its name and its parameters. */
USTRUCT(BlueprintType)
struct DREAMNARRATIVECORE_API FDnaEventDecl
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Narrative")
	FName Name;

	/** Each parameter in declaration order: its name, type, set, range and default mean what they mean for a fact; nothing else does. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Narrative")
	TArray<FDnaFactDecl> Params;

	/** `gameplay 标签`: the gameplay event (that tag or one under it) that raises this event. None for none. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Narrative")
	FName GameplayTag;

	/** Where each parameter comes from when a gameplay event raises it, in the order of Params. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Narrative")
	TArray<FDnaEventParamSource> ParamSources;

	const FDnaFactDecl* FindParam(FName Param) const;
};

/**
 * Another context with an event's parameters on top: what an expression evaluated inside a dialogue
 * an event opened sees. Everything else comes from the inner context.
 */
class DREAMNARRATIVECORE_API FDnaEventScopeContext : public IDnaEvalContext
{
public:
	FDnaEventScopeContext(IDnaEvalContext& InInner, const FDnaEventPayload* InEvent) : Inner(InInner), Event(InEvent) {}

	virtual FDnaValue GetFactValue(FName Fact) const override { return Inner.GetFactValue(Fact); }
	virtual FName GetCurrentChapter() const override { return Inner.GetCurrentChapter(); }
	virtual bool GetQuestState(FName Quest, EDnaQuestStateQuery Query) const override { return Inner.GetQuestState(Quest, Query); }
	virtual int32 GetVisitCount(FName Dialogue) const override { return Inner.GetVisitCount(Dialogue); }
	virtual float NextRandomUnit() override { return Inner.NextRandomUnit(); }
	virtual IDnaTrace* GetTrace() const override { return Inner.GetTrace(); }
	virtual bool GetEventParam(FName InEvent, FName Param, FDnaValue& OutValue) const override;

private:
	IDnaEvalContext& Inner;
	const FDnaEventPayload* Event;
};
