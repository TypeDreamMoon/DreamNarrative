// Copyright DreamMoon. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DreamNarrativeLedger.h"
#include "DreamNarrativeStory.h"
#include "Fixtures/DnaTestWorld.h"

/**
 * The dialogue slice, end to end: the Ring sample is parsed, validated and generated in memory,
 * registered with a fresh ledger, and its dialogue is walked with scripted choices. Facts must move
 * the way the source says, and the entry picked for a character must follow the ledger. Every step
 * also saves and reloads the ledger (FDnaTestWorld).
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaRingSampleTest, "DreamNarrative.Dialogue.RingSample", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDnaRingSampleTest::RunTest(const FString& Parameters)
{
	using namespace UE::DreamNarrative::Tests;

	FDnaTestWorld World(*this);
	if (!World.CompilePluginFiles({ TEXT("Tests/Corpus/Validate/Samples/ring.dna") }))
	{
		return false;
	}
	UDreamNarrativeLedger& Ledger = World.Ledger();
	UDreamNarrativeStory& Story = World.Story();

	// -- declarations landed
	TestTrue(TEXT("fact declared"), Ledger.HasFact(TEXT("换水次数")));
	TestTrue(TEXT("grouped fact declared"), Ledger.HasFact(TEXT("墨梅册.第一页")));
	TestEqual(TEXT("tag default"), Ledger.GetFact(TEXT("梅蕾状态")).Tag, FName(TEXT("含苞")));
	TestTrue(TEXT("dialogue indexed"), Story.HasDialogue(TEXT("凛_日常")));

	// -- entry selection follows the ledger: no water yet, so the first-water entry wins
	TestEqual(TEXT("first entry"), Story.SelectEntry(TEXT("凛")), FName(TEXT("凛_第一次换水")));

	// -- the first-water dialogue: facts move, the chapter advances
	World.Talk(TEXT("凛"));
	TestTrue(TEXT("first line shown"), World.LastDialogueHas(TEXT("凛: 水该换了。")));
	TestTrue(TEXT("player line shown"), World.LastDialogueHas(TEXT("玩家: 我来。")));
	TestTrue(TEXT("stage requested"), World.LastDialogueHas(TEXT("[play 换水]")));
	World.Expect(TEXT("换水次数 == 1 and 梅蕾状态 == 初绽"));
	World.Expect(TEXT("chapter == 第一章"));
	World.Expect(TEXT("visited(凛_第一次换水) == 1"));

	// -- 周期 is extern and starts at 0, which makes `周期 % 3 == 0` true and picks the every-third-cycle
	// entry; at 1 the everyday entry applies
	TestEqual(TEXT("every third cycle at 0"), Story.SelectEntry(TEXT("凛")), FName(TEXT("凛_换水")));
	World.SetExtern(TEXT("周期"), FDnaValue::FromInt(1));
	TestEqual(TEXT("daily entry"), Story.SelectEntry(TEXT("凛")), FName(TEXT("凛_日常")));

	// -- the everyday dialogue: the first-visit line, the once-choice, and the fact it sets
	World.Talk(TEXT("凛"), { TEXT("你在等什么?") });
	TestTrue(TEXT("first-visit branch"), World.LastDialogueHas(TEXT("凛: 你回来了。")));
	TestTrue(TEXT("once choice taken"), World.LastDialogueHas(TEXT("[choose 你在等什么?]")));
	TestTrue(TEXT("choice body ran"), World.LastDialogueHas(TEXT("凛: ……")));
	World.Expect(TEXT("凛提到过等待"));
	TestFalse(TEXT("late-night line hidden at cycle 1"), World.LastDialogueHas(TEXT("早点睡")));

	// -- the second visit: the else branch, and the once option is gone
	World.Talk(TEXT("凛"), { TEXT("没事。") });
	TestTrue(TEXT("else branch"), World.LastDialogueHas(TEXT("凛: 嗯。")));
	TestFalse(TEXT("once option hidden"), World.LastDialogueHas(TEXT("  - 你在等什么?")));

	// -- a snapshot round trip keeps facts, chapter and bookkeeping
	const FDnaLedgerSnapshot Snapshot = Ledger.MakeSnapshot();
	Ledger.ResetToDefaults();
	TestEqual(TEXT("reset water"), Ledger.GetFactAsInt(TEXT("换水次数")), 0);
	Ledger.ApplySnapshot(Snapshot);
	World.Expect(TEXT("换水次数 == 1 and chapter == 第一章"));
	TestTrue(TEXT("restored once flag"), Ledger.WasChosenOnce(TEXT("凛_日常.c1")));

	// -- the debug teleport applies implied facts along the chain
	Ledger.ResetToDefaults();
	World.Jump(TEXT("终局_花开"));
	World.Expect(TEXT("换水次数 == 1"));
	World.Expect(TEXT("梅蕾状态 == 盛开 and 墨梅册.第三页"));

	// -- ownership: game code may not write a narrative fact (the refusal logs an error by design)
	AddExpectedError(TEXT("is owned by the narrative"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("ownership enforced"), Ledger.SetFact(TEXT("换水次数"), FDnaValue::FromInt(9), EDnaWriteSource::Game));
	TestTrue(TEXT("extern writable"), Ledger.SetFact(TEXT("周期"), FDnaValue::FromInt(9), EDnaWriteSource::Game));

	return !HasAnyErrors();
}

#endif
