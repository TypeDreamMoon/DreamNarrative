// Copyright DreamMoon. All Rights Reserved.
#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilitySystemComponent.h"
#include "DreamNarrativeGASBridge.h"
#include "DreamNarrativeLedger.h"
#include "DreamNarrativeStory.h"
#include "Fixtures/DnaTestWorld.h"
#include "UObject/StrongObjectPtr.h"
using namespace UE::DreamNarrative::Tests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaRegressionStateNestedOnce, "DreamNarrative.Regression.State.NestedEventOnce", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDnaRegressionStateNestedOnce::RunTest(const FString& Parameters)
{
	FDnaTestWorld World(*this);
	if (!World.CompileText(TEXT(
		"character Speaker\nfact Applied: int\nevent Pulse\nchannel Radio\n"
		"dialogue Relay on Pulse\n  emit Pulse\n  Speaker: Holding.\n"
		"dialogue OnceOnly on Pulse channel Radio once\n  set Applied += 1\n"))) { return false; }
	World.Ledger().EmitEvent(TEXT("Pulse"));
	TestNotNull(TEXT("relay remains active"), World.Story().GetRunner(NAME_None));
	TestEqual(TEXT("once branch executes once despite nested event"), World.Ledger().GetFactAsInt(TEXT("Applied")), 1);
	TestEqual(TEXT("once dialogue records one visit"), World.Ledger().GetVisited(TEXT("OnceOnly")), 1);
	World.Story().StopAllDialogue();
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaRegressionStateJumpCheckpoint, "DreamNarrative.Regression.State.JumpCreatesCheckpoint", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDnaRegressionStateJumpCheckpoint::RunTest(const FString& Parameters)
{
	FDnaTestWorld World(*this);
	if (!World.CompileText(TEXT(
		"fact Gate: bool\nchapter Origin\n  advance -> Skipped if Gate\n"
		"chapter Skipped after Origin\nchapter Target\n  implied\n    Gate = true\n"))) { return false; }
	TestTrue(TEXT("origin entered"), World.Ledger().SetChapter(TEXT("Origin")));
	TestEqual(TEXT("only origin checkpoint initially"), World.Ledger().GetChapterCheckpoints().Num(), 1);
	TestTrue(TEXT("jump succeeds"), World.Ledger().JumpToChapter(TEXT("Target")));
	TestEqual(TEXT("jump reaches its target"), World.Ledger().GetCurrentChapter(), FName(TEXT("Target")));
	TestTrue(TEXT("target implied fact applied"), World.Ledger().GetFactAsBool(TEXT("Gate")));
	TestFalse(TEXT("teleport must not checkpoint a skipped chapter"), World.Ledger().GetChapterCheckpoints().Contains(TEXT("Skipped")));
	TestEqual(TEXT("teleport leaves checkpoint set unchanged"), World.Ledger().GetChapterCheckpoints().Num(), 1);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaRegressionStateGasLoad, "DreamNarrative.Regression.State.GasMirrorAfterLoad", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDnaRegressionStateGasLoad::RunTest(const FString& Parameters)
{
	FDnaTestWorld World(*this);
	if (!World.CompileText(TEXT("fact Stacks: int extern session gameplay DreamNarrativeTest.State.Stacks\n"))) { return false; }
	const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("DreamNarrativeTest.State.Stacks"), false);
	if (!TestTrue(TEXT("host tag exists"), Tag.IsValid())) { return false; }
	TStrongObjectPtr<UDreamNarrativeGASBridge> Bridge(NewObject<UDreamNarrativeGASBridge>(World.GetGameInstance()));
	TStrongObjectPtr<UAbilitySystemComponent> System(NewObject<UAbilitySystemComponent>(GetTransientPackage()));
	Bridge->InitializeStandalone(&World.Ledger(), &World.Story());
	System->AddLooseGameplayTag(Tag, 3);
	Bridge->WatchAbilitySystem(System.Get(), true);
	TestEqual(TEXT("initial tag count is mirrored"), World.Ledger().GetFactAsInt(TEXT("Stacks")), 3);
	const FDnaLedgerSnapshot Saved = World.Ledger().MakeSnapshot();
	TestFalse(TEXT("session fact excluded from save"), Saved.Facts.ContainsByPredicate([](const FDnaFactSnapshotEntry& Entry) { return Entry.Fact == TEXT("Stacks"); }));
	World.Ledger().ApplySnapshot(Saved);
	TestEqual(TEXT("ability system still owns three stacks"), System->GetTagCount(Tag), 3);
	TestEqual(TEXT("watched session fact must mirror live ASC after load"), World.Ledger().GetFactAsInt(TEXT("Stacks")), 3);
	System->AddLooseGameplayTag(Tag, 1);
	TestEqual(TEXT("subsequent tag event still reaches bridge"), World.Ledger().GetFactAsInt(TEXT("Stacks")), 4);
	Bridge->Deinitialize();
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaRegressionStateControls, "DreamNarrative.Regression.State.ControlPaths", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDnaRegressionStateControls::RunTest(const FString& Parameters)
{
	{
		FDnaTestWorld World(*this);
		if (!World.CompileText(TEXT("fact Applied: int\nevent Pulse\nchannel Radio\ndialogue OnceOnly on Pulse channel Radio once\n  set Applied += 1\n"))) { return false; }
		World.Ledger().EmitEvent(TEXT("Pulse"));
		World.Ledger().EmitEvent(TEXT("Pulse"));
		TestEqual(TEXT("non-nested events obey once"), World.Ledger().GetFactAsInt(TEXT("Applied")), 1);
		TestEqual(TEXT("non-nested once records one visit"), World.Ledger().GetVisited(TEXT("OnceOnly")), 1);
	}
	{
		FDnaTestWorld World(*this);
		if (!World.CompileText(TEXT("fact Gate: bool\nchapter Origin\n  advance -> Skipped if Gate\nchapter Skipped after Origin\nchapter Target\n  implied\n    Gate = true\n"))) { return false; }
		TestTrue(TEXT("fresh jump succeeds"), World.Ledger().JumpToChapter(TEXT("Target")));
		TestEqual(TEXT("fresh jump creates no checkpoint"), World.Ledger().GetChapterCheckpoints().Num(), 0);
		const FDnaLedgerSnapshot Saved = World.Ledger().MakeSnapshot();
		World.Ledger().ApplySnapshot(Saved);
		TestEqual(TEXT("regular load restores chapter"), World.Ledger().GetCurrentChapter(), FName(TEXT("Target")));
		TestTrue(TEXT("regular load restores fact"), World.Ledger().GetFactAsBool(TEXT("Gate")));
		TestEqual(TEXT("regular load creates no checkpoint"), World.Ledger().GetChapterCheckpoints().Num(), 0);
	}
	return !HasAnyErrors();
}
#endif
