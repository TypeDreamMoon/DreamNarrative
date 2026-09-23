// Copyright DreamMoon. All Rights Reserved.
//
// Counts what the runtime does, as coverage cells: named facts about execution such as
// `expr.Add.Int+Float`, `dialogue.op.Choice` or `ledger.write.Clamped`. The spec suite's matrix
// (Tests/Spec/coverage.json) says which cells the suite must reach.

#pragma once

#include "CoreMinimal.h"
#include "DreamNarrativeTrace.h"

namespace UE::DreamNarrative::Tests
{
	class FDnaCoverage : public IDnaTrace
	{
	public:
		virtual void OnExpression(const FDnaExprNode& Node, const FDnaValue* Left, const FDnaValue* Right, const FDnaValue& Result) override;
		virtual void OnFactWrite(FName Fact, EDnaFactType Type, EDnaFactOwner Owner, EDnaWriteSource Source, EDnaFactWriteResult Result) override;
		virtual void OnEvent(const TCHAR* Event) override { Hit(Event); }

		void Hit(const FString& Cell) { ++Hits.FindOrAdd(Cell); }
		const TMap<FString, int32>& GetHits() const { return Hits; }

	private:
		TMap<FString, int32> Hits;
	};
}
