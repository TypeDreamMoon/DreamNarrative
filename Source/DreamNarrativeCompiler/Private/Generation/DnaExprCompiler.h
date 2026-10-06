// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamNarrativeExpression.h"
#include "Lang/DnaExpr.h"
#include "Lang/DnaModel.h"

class UDreamNarrativeScript;

namespace UE::DreamNarrative::Lang
{
	/**
	 * Lowers expression ASTs into a script's expression pool. Name resolution mirrors the validator:
	 * a quest state, a fact, or -- in a tag or chapter context -- a bare member.
	 */
	class FDnaExprCompiler
	{
	public:
		FDnaExprCompiler(const FDnaProgram& InProgram, UDreamNarrativeScript& InScript);

		/** Appends the expression to the pool and returns its index; INDEX_NONE for a null expression. */
		int32 Compile(const FDnaExprPtr& Expr);

		/** Same, with the fact the value is being assigned to, so a bare member resolves. */
		int32 CompileValueFor(const FDnaExprPtr& Expr, const FDnaFactInfo* TargetFact);

		/** Folds a constant expression; a bare member resolves against TargetFact. False when not constant. */
		bool Constant(const FDnaExprPtr& Expr, const FDnaFactInfo* TargetFact, FDnaValue& OutValue) const;

		/** How many `and`-joined terms a condition has: the default entry priority. */
		static int32 CountConjuncts(const FDnaExprPtr& Expr);

	private:
		int32 Lower(const FDnaExpr& Expr, FDnaCompiledExpr& Out, const FDnaFactInfo* TagContext, bool bChapterContext) const;
		const FDnaFactInfo* TagFactOf(const FDnaExpr& Expr) const;
		/** The parameter `事件.参数` names, or null. */
		const FDnaFactInfo* EventParamOf(const FDnaExpr& Expr) const;

		const FDnaProgram& Program;
		UDreamNarrativeScript& Script;
	};
}
