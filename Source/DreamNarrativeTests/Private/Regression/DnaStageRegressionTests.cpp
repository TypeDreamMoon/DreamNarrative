// Copyright DreamMoon. All Rights Reserved.
#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilitySystemComponent.h"
#include "DnaStageRegressionProbe.h"
#include "DreamNarrativeDialogueRunner.h"
#include "DreamNarrativeLedger.h"
#include "DreamNarrativeScript.h"
#include "DreamNarrativeStageSubsystem.h"
#include "DreamNarrativeStageWatchdog.h"
#include "DreamNarrativeStory.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Fixtures/DnaGameFixture.h"
#include "Fixtures/DnaTestWorld.h"
#include "GameFramework/Actor.h"
#include "GameFramework/WorldSettings.h"
#include "Smoke/DnaTestStageAbility.h"
#include "UObject/StrongObjectPtr.h"

using namespace UE::DreamNarrative::Tests;
namespace
{
	void RegisterDefinitions(FDnaStandaloneGame& Game, const FDnaTestWorld& Definitions)
	{
		for (UDreamNarrativeScript* Script : Definitions.GetScripts()) { Game.GetStory()->RegisterScript(Script); }
	}
	UDreamNarrativeStageDirector* MakeDirector(FDnaStandaloneGame& Game)
	{
		UDreamNarrativeStageDirector* Director = UDreamNarrativeStageDirector::FindOrAddDirector(Game.GetWorld()->SpawnActor<AActor>());
		Director->bManualAdvance = true;
		return Director;
	}
	UAbilitySystemComponent* MakeSystem(AActor* Avatar)
	{
		UAbilitySystemComponent* System = NewObject<UAbilitySystemComponent>(Avatar);
		System->RegisterComponent();
		System->InitAbilityActorInfo(Avatar, Avatar);
		return System;
	}
	struct FAbilityDefaults
	{
		FName Stage = UDnaTestStageAbility::StageToPlay;
		TWeakObjectPtr<AActor> Target = UDnaTestStageAbility::StageTarget;
		int32 Ends = UDnaTestStageAbility::Ends;
		EDnaStageEnd LastEnd = UDnaTestStageAbility::LastEnd;
		~FAbilityDefaults()
		{
			UDnaTestStageAbility::StageToPlay = Stage;
			UDnaTestStageAbility::StageTarget = Target;
			UDnaTestStageAbility::Ends = Ends;
			UDnaTestStageAbility::LastEnd = LastEnd;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaRegressionAbilityImmediateStop, "DreamNarrative.Regression.Stage.AbilityImmediateStop", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDnaRegressionAbilityImmediateStop::RunTest(const FString& Parameters)
{
	FDnaTestWorld Definitions(*this);
	if (!Definitions.CompileText(TEXT("stage Simple\n  duration 1.0s\n"))) { return false; }
	FDnaStandaloneGame Game;
	RegisterDefinitions(Game, Definitions);
	UDreamNarrativeStageDirector* Director = MakeDirector(Game);
	UAbilitySystemComponent* System = MakeSystem(Director->GetOwner());
	TStrongObjectPtr<UDnaStageRegressionProbe> Probe(NewObject<UDnaStageRegressionProbe>());
	Probe->Director = Director;
	Director->OnStageStarted.AddDynamic(Probe.Get(), &UDnaStageRegressionProbe::StopAtStart);
	FAbilityDefaults Restore;
	UDnaTestStageAbility::StageToPlay = TEXT("Simple");
	UDnaTestStageAbility::StageTarget.Reset();
	UDnaTestStageAbility::Ends = 0;
	const FGameplayAbilitySpecHandle Ability = System->GiveAbility(FGameplayAbilitySpec(UDnaTestStageAbility::StaticClass()));
	TestTrue(TEXT("ability activated"), System->TryActivateAbility(Ability));
	TestFalse(TEXT("startup listener stopped the director"), Director->IsPlaying());
	TestEqual(TEXT("task must report the synchronous interruption"), UDnaTestStageAbility::Ends, 1);
	const FGameplayAbilitySpec* Spec = System->FindAbilitySpecFromHandle(Ability);
	TestTrue(TEXT("ability must finish after the startup interruption"), Spec && !Spec->IsActive());
	System->CancelAbilityHandle(Ability);
	Director->OnStageStarted.RemoveDynamic(Probe.Get(), &UDnaStageRegressionProbe::StopAtStart);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaRegressionPoseAfterStop, "DreamNarrative.Regression.Stage.PoseAfterStop", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDnaRegressionPoseAfterStop::RunTest(const FString& Parameters)
{
	FDnaTestWorld Definitions(*this);
	if (!Definitions.CompileText(TEXT(
		"stage Slow\n  duration 10.0s\n  ui\n    0.0s..10.0s hide hud\n"
		"  game\n    0.0s..10.0s hitstop 0.5x\n"))) { return false; }
	FDnaStandaloneGame Game;
	RegisterDefinitions(Game, Definitions);
	UDreamNarrativeStageDirector* Director = MakeDirector(Game);
	TStrongObjectPtr<UDnaStageRegressionProbe> Probe(NewObject<UDnaStageRegressionProbe>());
	Probe->Director = Director;
	Director->OnHudVisibility.AddDynamic(Probe.Get(), &UDnaStageRegressionProbe::StopWhenHidden);
	const float Before = Game.GetWorld()->GetWorldSettings()->TimeDilation;
	TestTrue(TEXT("stage was accepted"), Director->PlayStage(TEXT("Slow")));
	TestFalse(TEXT("HUD callback stopped the director"), Director->IsPlaying());
	TestEqual(TEXT("stopped stage must leave world time restored"), Game.GetWorld()->GetWorldSettings()->TimeDilation, Before, 1e-5f);
	TestEqual(TEXT("stopped stage must have no outstanding transaction slots"), Director->DescribeTouched().Num(), 0);
	Director->Stop();
	TestEqual(TEXT("calling stop again must not leave the world slowed"), Game.GetWorld()->GetWorldSettings()->TimeDilation, Before, 1e-5f);
	Director->OnHudVisibility.RemoveDynamic(Probe.Get(), &UDnaStageRegressionProbe::StopWhenHidden);
	Game.GetWorld()->GetWorldSettings()->SetTimeDilation(Before);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaRegressionOverlappingTime, "DreamNarrative.Regression.Stage.OverlappingTime", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDnaRegressionOverlappingTime::RunTest(const FString& Parameters)
{
	FDnaTestWorld Definitions(*this);
	if (!Definitions.CompileText(TEXT(
		"stage SlowA\n  duration 10.0s\n  game\n    0.0s..10.0s hitstop 0.5x\n"
		"stage SlowB\n  duration 10.0s\n  game\n    0.0s..10.0s hitstop 0.2x\n"))) { return false; }
	FDnaStandaloneGame Game;
	RegisterDefinitions(Game, Definitions);
	UDreamNarrativeStageDirector* A = MakeDirector(Game);
	UDreamNarrativeStageDirector* B = MakeDirector(Game);
	AWorldSettings* Settings = Game.GetWorld()->GetWorldSettings();
	const float Before = Settings->TimeDilation;
	TestTrue(TEXT("first actor stage started"), A->PlayStage(TEXT("SlowA")));
	TestTrue(TEXT("second actor stage started"), B->PlayStage(TEXT("SlowB")));
	TestEqual(TEXT("both active stages affect time"), Settings->TimeDilation, Before * 0.1f, 1e-5f);
	A->Stop();
	TestTrue(TEXT("second actor still playing"), B->IsPlaying());
	TestEqual(TEXT("remaining stage retains its own scale"), Settings->TimeDilation, Before * 0.2f, 1e-5f);
	B->Stop();
	TestFalse(TEXT("both stages ended"), A->IsPlaying() || B->IsPlaying());
	TestEqual(TEXT("all stages ended restores original world time"), Settings->TimeDilation, Before, 1e-5f);
	Settings->SetTimeDilation(Before);
	// Both ending orders must restore the same original world scale.
	A->PlayStage(TEXT("SlowA"));
	B->PlayStage(TEXT("SlowB"));
	B->Stop();
	A->Stop();
	TestEqual(TEXT("reverse end order also restores original world time"), Settings->TimeDilation, Before, 1e-5f);
	// Between a stage's changes the world's time is the game's to set as well.
	A->PlayStage(TEXT("SlowA"));
	Settings->SetTimeDilation(0.3f);
	if (UDreamNarrativeStageWatchdog* Watchdog = Game.GetWorld()->GetSubsystem<UDreamNarrativeStageWatchdog>())
	{
		Watchdog->Tick(0.016f);
	}
	TestEqual(TEXT("the watchdog leaves the game's own time change alone"), Settings->TimeDilation, 0.3f, 1e-5f);
	A->Stop();
	Settings->SetTimeDilation(Before);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaRegressionStageTearDown, "DreamNarrative.Regression.Stage.StageTearDown", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDnaRegressionStageTearDown::RunTest(const FString& Parameters)
{
	FDnaTestWorld Definitions(*this);
	if (!Definitions.CompileText(TEXT(
		"character Speaker\ndialogue Scene for Speaker\n  play First\n  play Slow\n  Speaker: Done.\n"
		"stage First\n  duration 10.0s\n"
		"stage Slow\n  duration 10.0s\n  game\n    0.0s..10.0s hitstop 0.5x\n"))) { return false; }
	FDnaStandaloneGame Game;
	RegisterDefinitions(Game, Definitions);
	UDreamNarrativeStageDirector* Director = MakeDirector(Game);
	UDreamNarrativeStageSubsystem* Host = Game.GetGameInstance()->GetSubsystem<UDreamNarrativeStageSubsystem>();
	Host->RegisterDirector(Director);
	UDreamNarrativeDialogueRunner* Runner = Game.GetStory()->TalkTo(TEXT("Speaker"));
	if (!TestNotNull(TEXT("story runner started"), Runner)) { return false; }
	TestEqual(TEXT("first stage active before teardown"), Director->GetStageName(), FName(TEXT("First")));
	const float Before = Game.GetWorld()->GetWorldSettings()->TimeDilation;
	Game.GetWorld()->BeginTearingDown();
	TestTrue(TEXT("world tearing down"), Game.GetWorld()->bIsTearingDown);
	TestFalse(TEXT("world teardown must not start another stage"), Director->IsPlaying());
	TestEqual(TEXT("teardown keeps world time restored"), Game.GetWorld()->GetWorldSettings()->TimeDilation, Before, 1e-5f);
	UDreamNarrativeStageWatchdog* Watchdog = Game.GetWorld()->GetSubsystem<UDreamNarrativeStageWatchdog>();
	AddInfo(FString::Printf(TEXT("after teardown: director=%s, tracked=%d"), *Director->GetStageName().ToString(), Watchdog ? Watchdog->GetPlaying().Num() : -1));
	Game.GetStory()->StopAllDialogue();
	Host->StopAllStages();
	Host->UnregisterDirector(Director);
	return !HasAnyErrors();
}
#endif
