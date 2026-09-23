// Copyright DreamMoon. All Rights Reserved.
//
// The expression AST and its parser. Expressions appear in conditions (`if`), assignments (`set`),
// objective tests, chapter rules and text interpolation. The set is frozen at proposal v0.2 §7:
// logic, comparison, arithmetic with `%`, `visited` / `random` / `chance`, quest state, `chapter`
// and `in (...)` for tag facts. No strings, no ternary.

#pragma once

#include "CoreMinimal.h"
#include "DreamNarrativeTypes.h"
#include "Lang/DnaDiagnostic.h"
#include "Lang/DnaLexer.h"

namespace UE::DreamNarrative::Lang
{
	enum class EDnaExprKind : uint8
	{
		/** A number, `true`, `false`, or a percentage (held as Float, 30% = 0.3). */
		Literal,
		/** A dotted name: a fact, a grouped fact, a quest state (`猎狼.completed`), or -- in the right
		 *  context -- a tag member. Resolution is the validator's job. */
		Name,
		/** The `chapter` keyword: the current chapter as a tag value. */
		Chapter,
		/** `visited(node)`, `random(a, b)`, `chance(30%)`. Name holds the function. */
		Call,
		/** `not x`, `-x`. Args[0] is the operand. */
		Unary,
		/** Args[0] op Args[1]. */
		Binary,
		/** Args[0] in (Args[1..]). */
		In,
	};

	enum class EDnaExprOp : uint8
	{
		None,
		Or, And, Not,
		Eq, Ne, Lt, Le, Gt, Ge,
		Add, Sub, Mul, Div, Mod,
		Neg,
	};

	DREAMNARRATIVECOMPILER_API const TCHAR* LexToString(EDnaExprOp Op);

	struct FDnaExpr;
	using FDnaExprPtr = TSharedPtr<FDnaExpr>;

	struct DREAMNARRATIVECOMPILER_API FDnaExpr
	{
		EDnaExprKind Kind = EDnaExprKind::Literal;
		EDnaExprOp Op = EDnaExprOp::None;
		FDnaSpan Span;
		FDnaValue Literal;
		/** Name: the dotted path as written. Call: the function name. */
		FString Name;
		/** Name: the path split on dots. */
		TArray<FString> Path;
		TArray<FDnaExprPtr> Args;

		/** True when the tree contains a Name, a Chapter or a Call -- something that refers to the
		 *  world rather than being a constant. */
		bool RefersToWorld() const;

		/** Every Name node in the tree, pre-order. */
		void CollectNames(TArray<const FDnaExpr*>& OutNames) const;

		/** Source-like rendering, for diagnostics and debug output. */
		FString ToString() const;
	};

	/**
	 * Parses one expression from Tokens starting at Index, leaving Index on the first token that
	 * does not continue it. Returns null after a DNS2007 when the tokens do not form an expression.
	 */
	DREAMNARRATIVECOMPILER_API FDnaExprPtr ParseDnaExpression(const TArray<FDnaToken>& Tokens, int32& Index, FDnaDiagnosticSink& Diagnostics);

	/** Tokenises and parses a whole segment. Leftover tokens are DNS2008. Null on any error. */
	DREAMNARRATIVECOMPILER_API FDnaExprPtr ParseDnaExpressionSegment(const FDnaSourceText& Source, int32 Offset, int32 Length, FDnaDiagnosticSink& Diagnostics);
}
