// Copyright DreamMoon. All Rights Reserved.
//
// Ledger paths that .dna source cannot reach -- the compiler refuses the source first -- but game
// code can: writes the ledger must refuse, saves made under names the story has since changed, and
// a tool deciding what `random` and `chance` draw.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DreamNarrativeLedger.h"
#include "Fixtures/DnaTestWorld.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaLedgerRefusalsTest, "DreamNarrative.Ledger.Refusals", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDnaLedgerRefusalsTest::RunTest(const FString& Parameters)
{
	using namespace UE::DreamNarrative::Tests;

	FDnaTestWorld World(*this);
	if (!World.CompileText(TEXT(
		"fact 剧情: int = 1\n"
		"fact 外部: int extern\n"
		"fact 等级: tag in (甲, 乙)\n")))
	{
		return false;
	}
	UDreamNarrativeLedger& Ledger = World.Ledger();

	// Every refusal logs an error by design.
	AddExpectedMessagePlain(TEXT("no fact called"), ELogVerbosity::Error);
	TestFalse(TEXT("an undeclared fact is refused"), Ledger.SetFact(TEXT("没有这个"), FDnaValue::FromInt(1), EDnaWriteSource::Game));

	AddExpectedMessagePlain(TEXT("is owned by the narrative"), ELogVerbosity::Error);
	TestFalse(TEXT("game code may not write a narrative fact"), Ledger.SetFact(TEXT("剧情"), FDnaValue::FromInt(9), EDnaWriteSource::Game));

	AddExpectedMessagePlain(TEXT("a tag value cannot be written"), ELogVerbosity::Error);
	TestFalse(TEXT("a tag is refused by an int fact"), Ledger.SetFact(TEXT("外部"), FDnaValue::FromTag(TEXT("甲")), EDnaWriteSource::Game));

	AddExpectedMessagePlain(TEXT("is not one of the values"), ELogVerbosity::Error);
	TestFalse(TEXT("a member outside the set is refused"), Ledger.SetFact(TEXT("等级"), FDnaValue::FromTag(TEXT("丙")), EDnaWriteSource::Narrative));

	// Refused writes leave nothing behind.
	World.Expect(TEXT("剧情 == 1 and 外部 == 0 and 等级 == 甲"));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaLedgerSnapshotMigrationTest, "DreamNarrative.Ledger.SnapshotMigration", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDnaLedgerSnapshotMigrationTest::RunTest(const FString& Parameters)
{
	using namespace UE::DreamNarrative::Tests;

	// The story renamed 旧名 to 新名 and deleted 删掉的 since the save was made.
	FDnaTestWorld World(*this);
	if (!World.CompileText(TEXT(
		"fact 新名: int was 旧名\n"
		"fact 留着: int\n")))
	{
		return false;
	}

	FDnaLedgerSnapshot Old;
	auto Add = [&Old](const TCHAR* Fact, int32 Value)
	{
		FDnaFactSnapshotEntry& Entry = Old.Facts.AddDefaulted_GetRef();
		Entry.Fact = Fact;
		Entry.Value = FDnaValue::FromInt(Value);
	};
	Add(TEXT("旧名"), 5);
	Add(TEXT("删掉的"), 3);
	Add(TEXT("留着"), 2);

	AddExpectedMessagePlain(TEXT("is no longer declared"), ELogVerbosity::Warning);
	World.Ledger().ApplySnapshot(Old);

	World.Expect(TEXT("新名 == 5"));
	World.Expect(TEXT("留着 == 2"));
	TestFalse(TEXT("the deleted fact did not come back"), World.Ledger().HasFact(TEXT("删掉的")));
	// Reading under the old name still finds the value: game code written against it keeps working.
	TestEqual(TEXT("the old name resolves"), World.Ledger().GetFactAsInt(TEXT("旧名")), 5);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaLedgerRandomSourceTest, "DreamNarrative.Ledger.RandomSource", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDnaLedgerRandomSourceTest::RunTest(const FString& Parameters)
{
	using namespace UE::DreamNarrative::Tests;

	FDnaTestWorld World(*this);
	if (!World.CompileText(TEXT("fact 骰: int\n")))
	{
		return false;
	}

	struct FListed : IDnaRandomSource
	{
		TArray<float> Units;
		virtual float NextUnit(FRandomStream& Stream) override
		{
			const float Unit = Units.Num() > 0 ? Units[0] : 0.f;
			if (Units.Num() > 0) { Units.RemoveAt(0); }
			return Unit;
		}
	};
	struct FPassThrough : IDnaRandomSource
	{
		virtual float NextUnit(FRandomStream& Stream) override { return Stream.GetFraction(); }
	};

	// The lowest and the highest draw reach both ends of a range and both outcomes of a chance.
	FListed Listed;
	Listed.Units = { ::DreamNarrative::LowestDraw, ::DreamNarrative::HighestDraw, ::DreamNarrative::LowestDraw, ::DreamNarrative::HighestDraw };
	World.Ledger().SetRandomSource(&Listed);
	FDnaValue Value;
	World.Evaluate(TEXT("random(1, 6)"), Value);
	TestEqual(TEXT("a low draw is the bottom of the range"), Value.Int, 1);
	World.Evaluate(TEXT("random(1, 6)"), Value);
	TestEqual(TEXT("a high draw is the top"), Value.Int, 6);
	World.Evaluate(TEXT("chance(30%)"), Value);
	TestTrue(TEXT("a low draw makes the chance"), Value.bBool);
	World.Evaluate(TEXT("chance(30%)"), Value);
	TestFalse(TEXT("a high draw misses it"), Value.bBool);

	// A source is handed the ledger's own seeded stream: passing every draw on changes nothing.
	auto Draws = [&World, &Value]()
	{
		World.Ledger().SetRandomSeed(11);
		TArray<FString> Out;
		for (int32 Index = 0; Index < 5; ++Index)
		{
			World.Evaluate(TEXT("random(1, 100)"), Value);
			Out.Add(Value.ToString());
		}
		return FString::Join(Out, TEXT(" "));
	};
	World.Ledger().SetRandomSource(nullptr);
	const FString Plain = Draws();
	FPassThrough PassThrough;
	World.Ledger().SetRandomSource(&PassThrough);
	const FString Passed = Draws();
	World.Ledger().SetRandomSource(nullptr);
	TestEqual(TEXT("the same draws through a source that passes them on"), Passed, Plain);
	return !HasAnyErrors();
}

#endif
