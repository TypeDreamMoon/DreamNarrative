// Copyright DreamMoon. All Rights Reserved.
//
// Compiled expressions. The compiler type-checks and lowers the source expression into this flat
// node table; the runtime evaluates it against the ledger without parsing anything. A table, not a
// tree of objects, so it serialises as plain UPROPERTY data inside the script asset.

#pragma once

#include "CoreMinimal.h"
#include "DreamNarrativeTypes.h"
#include "DreamNarrativeExpression.generated.h"

UENUM()
enum class EDnaExprNodeKind : uint8
{
	/** A constant. */
	Literal,
	/** The current value of a fact. Name is the fact. */
	Fact,
	/** A member of a tag fact's set, as a value. Name is the bare member. */
	Member,
	/** The current chapter, as a tag value. */
	Chapter,
	/** A chapter name used as a value. Name is the chapter. */
	ChapterName,
	/** `quest.available` and friends. Name is the quest. */
	QuestState,
	/** `visited(dialogue)`. Name is the dialogue. */
	Visited,
	/** `random(min, max)`. Children are the bounds. */
	Random,
	/** `chance(p)`. Children[0] is the probability, 0..1. */
	Chance,
	/** Children[0] with Operator (Not, Neg). */
	Unary,
	/** Children[0] Operator Children[1]. */
	Binary,
	/** Children[0] in (Children[1..]). */
	In,
	/** `事件.参数`: a parameter of the event that opened the dialogue. Event is the event, Name the parameter. */
	EventParam,
};

UENUM()
enum class EDnaExprOperator : uint8
{
	None,
	Or, And, Not,
	Eq, Ne, Lt, Le, Gt, Ge,
	Add, Sub, Mul, Div, Mod,
	Neg,
};

UENUM(BlueprintType)
enum class EDnaQuestStateQuery : uint8
{
	Available,
	Active,
	Completed,
	Failed,
};

USTRUCT()
struct DREAMNARRATIVECORE_API FDnaExprNode
{
	GENERATED_BODY()

	UPROPERTY()
	EDnaExprNodeKind Kind = EDnaExprNodeKind::Literal;

	UPROPERTY()
	EDnaExprOperator Operator = EDnaExprOperator::None;

	UPROPERTY()
	FDnaValue Literal;

	UPROPERTY()
	FName Name;

	/** For EventParam: the event whose parameter Name is. */
	UPROPERTY()
	FName Event;

	UPROPERTY()
	EDnaQuestStateQuery QuestQuery = EDnaQuestStateQuery::Completed;

	UPROPERTY()
	TArray<int32> Children;
};

USTRUCT(BlueprintType)
struct DREAMNARRATIVECORE_API FDnaCompiledExpr
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FDnaExprNode> Nodes;

	UPROPERTY()
	int32 Root = INDEX_NONE;

	/** The source spelling, for the debugger and for error messages. */
	UPROPERTY()
	FString Source;

	bool IsValid() const { return Nodes.IsValidIndex(Root); }

	/** True when no node reads the world -- the value is the same every time. */
	bool IsConstant() const;
};

class IDnaTrace;

/** What an expression can ask the world. The ledger implements it; tests can fake it. */
class DREAMNARRATIVECORE_API IDnaEvalContext
{
public:
	virtual ~IDnaEvalContext() = default;
	virtual FDnaValue GetFactValue(FName Fact) const = 0;
	virtual FName GetCurrentChapter() const = 0;
	virtual bool GetQuestState(FName Quest, EDnaQuestStateQuery Query) const = 0;
	virtual int32 GetVisitCount(FName Dialogue) const = 0;
	/** Uniform in [0, 1). */
	virtual float NextRandomUnit() = 0;
	/** Where evaluation reports each node, or null (the default) for nobody. */
	virtual IDnaTrace* GetTrace() const { return nullptr; }
	/** A parameter of the event in scope; false (the default) outside a dialogue an event opened. */
	virtual bool GetEventParam(FName Event, FName Param, FDnaValue& OutValue) const { return false; }
};

namespace DreamNarrative
{
	/** The lowest draw: `random` gives its lower bound, `chance` hits unless it is 0%. */
	inline constexpr float LowestDraw = 0.f;
	/** The highest draw, the largest float below 1: `random` gives its upper bound, `chance` misses unless it is 100%. */
	inline constexpr float HighestDraw = 0.99999994f;

	DREAMNARRATIVECORE_API FDnaValue EvaluateExpr(const FDnaCompiledExpr& Expr, IDnaEvalContext& Context);
	/** Truthiness of the result: false / 0 / none are false. An invalid expression is false. */
	DREAMNARRATIVECORE_API bool EvaluateCondition(const FDnaCompiledExpr& Expr, IDnaEvalContext& Context);
	/** Evaluates a constant expression with no world. Returns false when the expression reads the world. */
	DREAMNARRATIVECORE_API bool EvaluateConstant(const FDnaCompiledExpr& Expr, FDnaValue& OutValue);
}
