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
#include "AssetRegistry/AssetRegistryModule.h"
#include "DreamNarrativeStageArbiter.h"
#include "Misc/Guid.h"
#include "Lang/DnaProject.h"
#include "Misc/ScopeExit.h"
#include "Sound/SoundWave.h"
#include "UObject/Package.h"

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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaRegressionStageAudio, "DreamNarrative.Regression.Stage.AudioEvents", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDnaRegressionStageAudio::RunTest(const FString& Parameters)
{
	const FString Root = TEXT("/Temp/DnaStageAudio_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	TArray<UObject*> Made;
	for (const TCHAR* Name : { TEXT("SFX_Hit"), TEXT("SFX_Wind"), TEXT("BGM_A"), TEXT("BGM_B") })
	{
		USoundWave* Wave = NewObject<USoundWave>(CreatePackage(*(Root / Name)), Name, RF_Public | RF_Standalone);
		Wave->Duration = 3.f;
		FAssetRegistryModule::AssetCreated(Wave);
		Made.Add(Wave);
	}
	ON_SCOPE_EXIT { for (UObject* Object : Made) { FAssetRegistryModule::AssetDeleted(Object); Object->ClearFlags(RF_Standalone); } };

	FDnaTestWorld Definitions(*this);
	if (!Definitions.CompileText(FString::Printf(TEXT(
		"stage 打击\n  duration 2.0s\n  audio\n"
		"    0.0s..1.0s   sound %s/SFX_Wind\n"
		"    0.0s         music %s/BGM_A fade 1.0s\n"
		"    0.5s         sound %s/SFX_Hit volume 0.8\n"
		"stage 回忆\n  duration 3.0s\n  audio\n"
		"    0.0s         music %s/BGM_B fade 0.5s\n"), *Root, *Root, *Root, *Root))) { return false; }
	FDnaStandaloneGame Game;
	RegisterDefinitions(Game, Definitions);
	UDreamNarrativeStageDirector* First = MakeDirector(Game);
	UDreamNarrativeStageDirector* Second = MakeDirector(Game);
	UDreamNarrativeStageArbiter* Arbiter = Game.GetWorld()->GetSubsystem<UDreamNarrativeStageArbiter>();
	if (!TestNotNull(TEXT("the world arbitrates"), Arbiter)) { return false; }
	TStrongObjectPtr<UDnaStageRegressionProbe> Probe(NewObject<UDnaStageRegressionProbe>());
	for (UDreamNarrativeStageDirector* Director : { First, Second })
	{
		Director->OnSound.AddDynamic(Probe.Get(), &UDnaStageRegressionProbe::HearSound);
		Director->OnSoundStopped.AddDynamic(Probe.Get(), &UDnaStageRegressionProbe::HearSoundStopped);
	}
	Arbiter->OnMusic.AddDynamic(Probe.Get(), &UDnaStageRegressionProbe::HearMusic);

	TestTrue(TEXT("the first stage plays"), First->PlayStage(TEXT("打击")));
	TestEqual(TEXT("its first moment"), Probe->Take(), FString(TEXT("sound SFX_Wind 1.0 held, music BGM_A 1.0")));
	First->Advance(0.6f);
	TestEqual(TEXT("a sound at its time"), Probe->Take(), FString(TEXT("sound SFX_Hit 0.8")));
	First->Advance(0.5f);
	TestEqual(TEXT("a held sound stops at the end of its range"), Probe->Take(), FString(TEXT("stop")));

	// Music is the world's: the stage that asked last plays; its end goes back to the one beneath, then to the game.
	TestTrue(TEXT("the second stage plays"), Second->PlayStage(TEXT("回忆")));
	TestEqual(TEXT("its music takes over"), Probe->Take(), FString(TEXT("music BGM_B 0.5")));
	First->Stop();
	TestEqual(TEXT("the one beneath ending moves nothing"), Probe->Take(), FString());
	Second->Stop();
	TestEqual(TEXT("the last one ending gives the music back to the game"), Probe->Take(), FString(TEXT("music game 0.5")));

	First->PlayStage(TEXT("打击"));
	Second->PlayStage(TEXT("回忆"));
	Probe->Take();
	Second->Stop();
	TestEqual(TEXT("the one on top ending goes back to the one beneath"), Probe->Take(), FString(TEXT("music BGM_A 0.5")));
	First->Stop();
	TestEqual(TEXT("cut short, a held sound stops too"), Probe->Take(), FString(TEXT("music game 1.0, stop")));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaRegressionStageBlocking, "DreamNarrative.Regression.Stage.Blocking", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDnaRegressionStageBlocking::RunTest(const FString& Parameters)
{
	FDnaTestWorld Definitions(*this);
	if (!Definitions.CompileText(TEXT(
		"stage 走位\n  duration 2.0s\n"
		"  marks\n"
		"    门口     local (500, 0, 0) look 乙\n"
		"  blocking\n"
		"    0.0s..1.0s   move 甲 -> 门口 run\n"
		"    1.0s         face 甲 -> 乙 over 0.5s\n"
		"    0.0s..1.5s   look 乙 at 甲\n"
		"  restore\n"
		"    blocking back\n"
		"stage 留下\n  duration 1.0s\n"
		"  blocking\n"
		"    0.0s..1.0s   move 甲 -> 乙\n"))) { return false; }
	FDnaStandaloneGame Game;
	RegisterDefinitions(Game, Definitions);
	ADnaStageBlockingActor* Walker = Game.GetWorld()->SpawnActor<ADnaStageBlockingActor>(FVector::ZeroVector, FRotator::ZeroRotator);
	ADnaStageBlockingActor* Watcher = Game.GetWorld()->SpawnActor<ADnaStageBlockingActor>(FVector(500.0, 500.0, 0.0), FRotator::ZeroRotator);
	Walker->Tags.Add(TEXT("甲"));
	Watcher->Tags.Add(TEXT("乙"));
	UDreamNarrativeStageDirector* Director = MakeDirector(Game);

	TestTrue(TEXT("the stage plays"), Director->PlayStage(TEXT("走位")));
	TestEqual(TEXT("the walker hears where to"), Walker->Take(), FString(TEXT("move (500,0,0) 1.0 run")));
	TestEqual(TEXT("the watcher looks"), Watcher->Take(), FString(TEXT("look 甲")));
	Director->Advance(0.5f);
	TestEqual(TEXT("halfway there at half time"), Walker->GetActorLocation().X, 250.0, 1.0);
	Director->Advance(0.5f);
	TestEqual(TEXT("at the mark when the range ends"), Walker->GetActorLocation().X, 500.0, 0.1);
	TestEqual(TEXT("facing as the mark says (toward 乙)"), Walker->GetActorRotation().Yaw, 90.0, 0.5);
	TestEqual(TEXT("and hears it arrived"), Walker->Take(), FString(TEXT("arrived")));
	Director->Advance(0.6f);
	TestEqual(TEXT("the look ends with its range"), Watcher->Take(), FString(TEXT("look off")));
	Director->Advance(1.0f);
	TestFalse(TEXT("the stage is over"), Director->IsPlaying());
	TestTrue(TEXT("`blocking back` puts the walker where it stood"), Walker->GetActorLocation().IsNearlyZero(0.1) && FMath::IsNearlyZero(Walker->GetActorRotation().Yaw, 0.1));

	// Cut short with nothing to put back, an actor stays where the stage left it.
	TestTrue(TEXT("the second stage plays"), Director->PlayStage(TEXT("留下")));
	Walker->Take();
	Director->Advance(0.5f);
	Director->Stop();
	TestEqual(TEXT("a move cut short ends where it is"), Walker->Take(), FString(TEXT("arrived")));
	TestTrue(TEXT("and the walker stays partway"), Walker->GetActorLocation().X > 100.0 && Walker->GetActorLocation().X < 400.0);
	TestEqual(TEXT("an actor goal is walked up to on the walker's own level"), Walker->GetActorLocation().Z, 0.0, 0.1);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaRegressionStageBlockingSyntax, "DreamNarrative.Regression.Stage.BlockingSyntax", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDnaRegressionStageBlockingSyntax::RunTest(const FString& Parameters)
{
	using namespace UE::DreamNarrative::Lang;
	auto Codes = [](const FString& Text)
	{
		const FString Path = TEXT("Blocking.dna");
		const TMap<FString, FString> Texts = { { Path, Text } };
		FDnaProgram Program;
		FDnaDiagnosticSink Diagnostics;
		LoadDnaProgram({ Path }, Program, Diagnostics, &Texts);
		TArray<FString> Out;
		for (const FDnaDiagnostic& Each : Diagnostics.GetDiagnostics()) { if (Each.Severity == EDnaSeverity::Error) { Out.Add(Each.Code); } }
		return Out;
	};
	TestEqual(TEXT("blocking, marks and a shake compile"), Codes(TEXT(
		"stage 好\n  duration 2.0s\n  marks\n    桌边  local (0, 100, 0) rot (0, 180, 0)\n"
		"  camera\n    0.5s  shake /Script/Engine.CameraShakeBase scale 1.5\n"
		"  blocking\n    0.0s..1.0s  move 甲 -> 桌边 walk navigate\n    1.0s  face 甲 -> 乙 over 0.3s\n    1.0s..2.0s  look 甲 at 乙\n")).Num(), 0);
	TestEqual(TEXT("a shake is a moment"), Codes(TEXT("stage 坏\n  duration 2.0s\n  camera\n    0.0s..1.0s  shake /Game/S\n")), TArray<FString>{ TEXT("DNS7007") });
	TestEqual(TEXT("`blocking back` belongs under restore"), Codes(TEXT("stage 坏\n  duration 2.0s\n  blocking\n    1.0s  back\n")), TArray<FString>{ TEXT("DNS7006") });
	TestEqual(TEXT("a move names who and where"), Codes(TEXT("stage 坏\n  duration 2.0s\n  blocking\n    0.0s..1.0s  move 甲\n")), TArray<FString>{ TEXT("DNS7006") });
	return !HasAnyErrors();
}

#endif
