// Copyright DreamMoon. All Rights Reserved.
//
// Ledger paths that .dna source cannot reach -- the compiler refuses the source first -- but game
// code can: writes the ledger must refuse, and saves made under names the story has since changed.

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

#endif
