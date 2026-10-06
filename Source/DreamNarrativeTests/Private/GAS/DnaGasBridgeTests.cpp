// Copyright DreamMoon. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystemComponent.h"
#include "DreamNarrativeDialogueRunner.h"
#include "DreamNarrativeGASBridge.h"
#include "DreamNarrativeLedger.h"
#include "DreamNarrativeStory.h"
#include "Fixtures/DnaTestWorld.h"
#include "GameplayTagsManager.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
	/** A tag the test host registers (Tools/TestHost/Template/Config/Tags); invalid, and the test fails, anywhere else. */
	FGameplayTag HostTag(FAutomationTestBase& Test, const TCHAR* Name)
	{
		const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(Name, /*ErrorIfNotFound=*/false);
		if (!Tag.IsValid())
		{
			Test.AddError(FString::Printf(TEXT("The gameplay tag %s is not registered; run this test in the test host"), Name));
		}
		return Tag;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaGasBridgeTest, "DreamNarrative.GAS.Bridge", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDnaGasBridgeTest::RunTest(const FString& Parameters)
{
	using namespace UE::DreamNarrative::Tests;

	FDnaTestWorld World(*this);
	const bool bCompiled = World.CompileText(TEXT(
		"character 波德\n"
		"channel 侦察\n"
		"event 发现敌人 gameplay DreamNarrativeTest.Enemy.Spotted\n"
		"  类型: tag in (Small, Large) from target tags DreamNarrativeTest.Enemy.Size\n"
		"  数量: int range 0..9 = 1 from magnitude\n"
		"  着火: bool from instigator tags DreamNarrativeTest.State.Burning\n"
		"event 幽灵 gameplay DreamNarrativeTest.Missing\n"
		"fact 潜行中: bool extern gameplay DreamNarrativeTest.State.Stealth\n"
		"fact 层数: int extern gameplay DreamNarrativeTest.State.Stacks\n"
		"dialogue 报告 on 发现敌人 channel 侦察\n"
		"  波德: {发现敌人.类型} x{发现敌人.数量}\n"));
	if (!bCompiled)
	{
		return false;
	}
	const FGameplayTag TAG_Spotted = HostTag(*this, TEXT("DreamNarrativeTest.Enemy.Spotted"));
	const FGameplayTag TAG_SpottedAmbush = HostTag(*this, TEXT("DreamNarrativeTest.Enemy.Spotted.Ambush"));
	const FGameplayTag TAG_SizeLarge = HostTag(*this, TEXT("DreamNarrativeTest.Enemy.Size.Large"));
	const FGameplayTag TAG_Burning = HostTag(*this, TEXT("DreamNarrativeTest.State.Burning"));
	const FGameplayTag TAG_Stealth = HostTag(*this, TEXT("DreamNarrativeTest.State.Stealth"));
	const FGameplayTag TAG_Stacks = HostTag(*this, TEXT("DreamNarrativeTest.State.Stacks"));
	const FGameplayTag TAG_Defeated = HostTag(*this, TEXT("DreamNarrativeTest.Boss.Defeated"));
	if (HasAnyErrors())
	{
		return false;
	}
	UDreamNarrativeLedger& Ledger = World.Ledger();
	TStrongObjectPtr<UDreamNarrativeGASBridge> Bridge(NewObject<UDreamNarrativeGASBridge>(World.GetGameInstance()));
	TStrongObjectPtr<UAbilitySystemComponent> System(NewObject<UAbilitySystemComponent>(GetTransientPackage()));
	Bridge->InitializeStandalone(&Ledger);
	TArray<FString> Heard;
	const FDelegateHandle Handle = Ledger.OnEventRaised.AddLambda([&Heard](const FDnaEventPayload& Event, EDnaWriteSource Source)
	{
		Heard.Add(Event.ToString() + (Source == EDnaWriteSource::Game ? TEXT("") : TEXT(" (not as the game)")));
	});

	// -- tags feed facts, from what the system already has
	AddExpectedMessagePlain(TEXT("DreamNarrativeTest.Missing"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	System->AddLooseGameplayTag(TAG_Stealth);
	Bridge->WatchAbilitySystem(System.Get(), /*bMirrorTags=*/true);
	TestTrue(TEXT("a fact starts from the tag the system has"), Ledger.GetFactAsBool(TEXT("潜行中")));
	System->AddLooseGameplayTag(TAG_Stacks, 3);
	TestEqual(TEXT("an int fact is the tag's count"), Ledger.GetFactAsInt(TEXT("层数")), 3);
	System->RemoveLooseGameplayTag(TAG_Stealth);
	TestFalse(TEXT("a bool fact goes with the tag"), Ledger.GetFactAsBool(TEXT("潜行中")));

	// -- gameplay events raise the events mapped to them, the parameters taken from the payload
	FGameplayEventData Data;
	Data.EventMagnitude = 2.f;
	Data.TargetTags.AddTag(TAG_SizeLarge);
	Data.InstigatorTags.AddTag(TAG_Burning);
	System->HandleGameplayEvent(TAG_Spotted, &Data);
	const UDreamNarrativeDialogueRunner* Report = World.Story().GetRunner(TEXT("侦察"));
	TestTrue(TEXT("the event opened its dialogue, which reads the parameters"), Report && Report->GetCurrentLine().Text.ToString() == TEXT("Large x2"));
	const FGameplayEventData Empty;
	System->HandleGameplayEvent(TAG_SpottedAmbush, &Empty);
	System->HandleGameplayEvent(TAG_Defeated, &Data);
	TestTrue(TEXT("the game can route one itself"), Bridge->RaiseFromGameplayEvent(TAG_Spotted, Data));
	TestFalse(TEXT("but not one no script maps"), Bridge->RaiseFromGameplayEvent(TAG_Defeated, Data));
	TestEqual(TEXT("what was raised"), FString::Join(Heard, TEXT("\n")), FString(TEXT(
		"发现敌人(类型 = Large, 数量 = 2, 着火 = true)\n"
		"发现敌人(类型 = Small, 数量 = 0, 着火 = false)\n"
		"发现敌人(类型 = Large, 数量 = 2, 着火 = true)")));

	// -- once unwatched, nothing more comes through
	Bridge->UnwatchAbilitySystem(System.Get());
	Heard.Reset();
	System->AddLooseGameplayTag(TAG_Stealth);
	System->HandleGameplayEvent(TAG_Spotted, &Data);
	TestFalse(TEXT("an unwatched system's tags feed nothing"), Ledger.GetFactAsBool(TEXT("潜行中")));
	TestEqual(TEXT("and its events raise nothing"), Heard.Num(), 0);

	Ledger.OnEventRaised.Remove(Handle);
	Bridge->Deinitialize();
	return !HasAnyErrors();
}

#endif
