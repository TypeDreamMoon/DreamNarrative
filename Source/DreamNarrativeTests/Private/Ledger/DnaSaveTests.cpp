// Copyright DreamMoon. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DreamNarrativeLedger.h"
#include "Fixtures/DnaTestWorld.h"

// Every fixture step trusts DiffSnapshots to notice a save that lost something. This proves it does,
// one kind of loss at a time.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaSnapshotDiffTest, "DreamNarrative.Ledger.SnapshotDiff", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDnaSnapshotDiffTest::RunTest(const FString& Parameters)
{
	using namespace UE::DreamNarrative::Tests;

	auto Entry = [](const TCHAR* Fact, const FDnaValue& Value)
	{
		FDnaFactSnapshotEntry Result;
		Result.Fact = Fact;
		Result.Value = Value;
		return Result;
	};

	FDnaLedgerSnapshot Base;
	Base.Facts.Add(Entry(TEXT("甲"), FDnaValue::FromInt(3)));
	Base.Facts.Add(Entry(TEXT("乙"), FDnaValue::FromTag(TEXT("盛开"))));
	Base.Chapter = TEXT("第一章");
	Base.Visited.Add(TEXT("对话"), 2);
	Base.ChosenOnce.Add(TEXT("对话.c1"));

	TestEqual(TEXT("identical snapshots agree"), DiffSnapshots(Base, Base).Num(), 0);

	{
		FDnaLedgerSnapshot Reordered = Base;
		Swap(Reordered.Facts[0], Reordered.Facts[1]);
		TestEqual(TEXT("fact order does not matter"), DiffSnapshots(Base, Reordered).Num(), 0);
	}
	{
		FDnaLedgerSnapshot Changed = Base;
		Changed.Facts[0].Value = FDnaValue::FromInt(4);
		TestEqual(TEXT("a changed value"), DiffSnapshots(Base, Changed).Num(), 1);
	}
	{
		FDnaLedgerSnapshot Retyped = Base;
		Retyped.Facts[0].Value = FDnaValue::FromFloat(3.f);
		TestEqual(TEXT("a value that changed type"), DiffSnapshots(Base, Retyped).Num(), 1);
	}
	{
		FDnaLedgerSnapshot Lost = Base;
		Lost.Facts.RemoveAt(1);
		TestEqual(TEXT("a lost fact"), DiffSnapshots(Base, Lost).Num(), 1);
		TestEqual(TEXT("an extra fact"), DiffSnapshots(Lost, Base).Num(), 1);
	}
	{
		FDnaLedgerSnapshot Moved = Base;
		Moved.Chapter = TEXT("序章");
		TestEqual(TEXT("a different chapter"), DiffSnapshots(Base, Moved).Num(), 1);
	}
	{
		FDnaLedgerSnapshot Recounted = Base;
		Recounted.Visited[TEXT("对话")] = 1;
		Recounted.Visited.Add(TEXT("别的"), 1);
		TestEqual(TEXT("a changed and an extra visit count"), DiffSnapshots(Base, Recounted).Num(), 2);
	}
	{
		FDnaLedgerSnapshot Forgotten = Base;
		Forgotten.ChosenOnce.Reset();
		TestEqual(TEXT("a forgotten once-choice"), DiffSnapshots(Base, Forgotten).Num(), 1);
		TestEqual(TEXT("an invented once-choice"), DiffSnapshots(Forgotten, Base).Num(), 1);
	}
	return true;
}

#endif
