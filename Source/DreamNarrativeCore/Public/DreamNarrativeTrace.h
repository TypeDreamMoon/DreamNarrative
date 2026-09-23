// Copyright DreamMoon. All Rights Reserved.
//
// What the narrative runtime does, told to whoever listens: the spec suite's coverage matrix today,
// the debugger's write history and single-stepping later. A ledger carries at most one trace; with
// none installed every trace point costs one pointer test.
//
// Expressions and fact writes are reported with their data. Everything else is a named event such as
// `dialogue.op.Line` or `chapter.jump`, so the modules above Core (Story's instructions, Stage's
// tracks) report through this interface without Core knowing their types.

#pragma once

#include "CoreMinimal.h"
#include "DreamNarrativeTypes.h"

struct FDnaExprNode;

/** How a request to write a fact ended. */
enum class EDnaFactWriteResult : uint8
{
	Written,
	/** The fact already held the value; nobody was told. */
	Unchanged,
	/** A number outside the declared range, stored clamped to it. */
	Clamped,
	RefusedUndeclared,
	/** Game code writing a narrative-owned fact while ownership is enforced. */
	RefusedOwnership,
	/** A tag written to a numeric fact. */
	RefusedType,
	/** A tag that is not one of the fact's members. */
	RefusedDomain,
};

DREAMNARRATIVECORE_API const TCHAR* LexToString(EDnaFactWriteResult Result);

class DREAMNARRATIVECORE_API IDnaTrace
{
public:
	virtual ~IDnaTrace() = default;

	/**
	 * An expression node produced Result. For a unary or binary node Left (and Right) are its operand
	 * values; Right is null when the node has one operand or `and` / `or` decided on the left alone.
	 * Both are null for any other node.
	 */
	virtual void OnExpression(const FDnaExprNode& Node, const FDnaValue* Left, const FDnaValue* Right, const FDnaValue& Result) {}

	/** A write to Fact was asked for and ended as Result. Type and Owner are the declaration's (Bool, Narrative for an undeclared fact). */
	virtual void OnFactWrite(FName Fact, EDnaFactType Type, EDnaFactOwner Owner, EDnaWriteSource Source, EDnaFactWriteResult Result) {}

	/** Anything else, by a dotted name. */
	virtual void OnEvent(const TCHAR* Event) {}
};
