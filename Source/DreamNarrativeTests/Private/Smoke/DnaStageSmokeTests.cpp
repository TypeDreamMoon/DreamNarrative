// Copyright DreamMoon. All Rights Reserved.
//
// Stages in a real world: a player controller with its pawn, camera manager and HUD, a sun and a sky,
// an actor to look at. The director makes the world look like the stage's pose frame by frame, and
// however the stage ends -- run out, stopped at any moment, replaced, the player dead, the level
// gone -- the world is read back and must be what it was before the stage began.

#include "Misc/AutomationTest.h"
#include "Smoke/DnaTestStageAbility.h"

FName UDnaTestStageAbility::StageToPlay;
TWeakObjectPtr<AActor> UDnaTestStageAbility::StageTarget;
EDnaStageEnd UDnaTestStageAbility::LastEnd = EDnaStageEnd::Finished;
int32 UDnaTestStageAbility::Ends = 0;

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystemComponent.h"
#include "DreamNarrativeGASBridge.h"
#include "DreamNarrativeLedger.h"
#include "GameplayTagContainer.h"

#include "Camera/CameraActor.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "DreamNarrativeDialogueRunner.h"
#include "DreamNarrativeStageDirector.h"
#include "DreamNarrativeStagePose.h"
#include "DreamNarrativeStageSubsystem.h"
#include "DreamNarrativeStory.h"
#include "Engine/DirectionalLight.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Fixtures/DnaGameFixture.h"
#include "GameFramework/DefaultPawn.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "LevelSequence.h"
#include "LevelSequenceActor.h"
#include "UObject/Package.h"

namespace UE::DreamNarrative::Tests::StageSmoke
{
	const TCHAR* StageSource = TEXT("Tests/Smoke/Stage.dna");
	constexpr float Frame = 1.f / 30.f;

	/** A player in the game's world, and what a stage can reach of the world around it. */
	struct FRig
	{
		APlayerController* Controller = nullptr;
		APawn* Pawn = nullptr;
		AActor* Boss = nullptr;
		ULightComponentBase* Sun = nullptr;
		ULightComponentBase* Sky = nullptr;
		ULevelSequence* Sequence = nullptr;
	};

	FRig MakeRig(UWorld& World)
	{
		FRig Rig;
		Rig.Controller = World.SpawnActor<APlayerController>();
		// The player on this machine: what a stage shows is shown to a local player only.
		Rig.Controller->Player = NewObject<ULocalPlayer>(GEngine, NAME_None, RF_Transient);
		// A game mode would give it one; without it a controller goes when its pawn dies.
		Rig.Controller->SetPlayerState(World.SpawnActor<APlayerState>());
		Rig.Pawn = World.SpawnActor<ADefaultPawn>(FVector(0.0, 0.0, 100.0), FRotator::ZeroRotator);
		Rig.Pawn->Tags.Add(TEXT("主角"));
		Rig.Controller->Possess(Rig.Pawn);
		Rig.Controller->SpawnDefaultHUD();
		Rig.Boss = World.SpawnActor<AStaticMeshActor>(FVector(600.0, 0.0, 100.0), FRotator::ZeroRotator);
		Rig.Boss->Tags.Add(TEXT("巨型机械"));
		Rig.Sun = World.SpawnActor<ADirectionalLight>()->GetLightComponent();
		Rig.Sky = World.SpawnActor<ASkyLight>()->GetLightComponent();
		World.GetWorldSettings()->NotifyBeginPlay();

		// The level sequence the stage plays, made in memory where its path points.
		UPackage* Package = CreatePackage(TEXT("/Temp/DreamNarrativeStageSmoke/LS_Cutscene"));
		Rig.Sequence = FindObject<ULevelSequence>(Package, TEXT("LS_Cutscene"));
		if (!Rig.Sequence)
		{
			Rig.Sequence = NewObject<ULevelSequence>(Package, TEXT("LS_Cutscene"), RF_Public | RF_Standalone | RF_Transient);
			Rig.Sequence->Initialize();
		}
		return Rig;
	}

	void DropRig(FRig& Rig)
	{
		if (Rig.Sequence)
		{
			Rig.Sequence->ClearFlags(RF_Standalone);
			Rig.Sequence = nullptr;
		}
	}

	template <typename T>
	int32 Count(UWorld& World)
	{
		int32 Num = 0;
		for (TActorIterator<T> It(&World); It; ++It) { if (IsValid(*It)) { ++Num; } }
		return Num;
	}

	template <typename T>
	int32 CountComponents(const AActor* Actor)
	{
		if (!Actor) { return 0; }
		TInlineComponentArray<T*> Components(Actor);
		return Components.Num();
	}

	/** Everything a stage may change, read off the world. */
	struct FWorldState
	{
		FString ViewTarget;
		float Dilation = 1.f;
		bool bHudShown = true;
		bool bMoveIgnored = false;
		bool bLookIgnored = false;
		float Sun = 0.f;
		float Sky = 0.f;
		int32 Masks = 0;
		int32 KeyLights = 0;
		int32 Cameras = 0;
		int32 Sequences = 0;

		FString Describe() const
		{
			return FString::Printf(TEXT("view %s, time %.3f, hud %d, move %d look %d, sun %.3f sky %.3f, masks %d, key lights %d, cameras %d, sequences %d"),
				*ViewTarget, Dilation, bHudShown, bMoveIgnored, bLookIgnored, Sun, Sky, Masks, KeyLights, Cameras, Sequences);
		}
	};

	FWorldState Read(UWorld& World, const FRig& Rig)
	{
		FWorldState State;
		if (IsValid(Rig.Controller))
		{
			State.ViewTarget = GetNameSafe(Rig.Controller->GetViewTarget());
			State.bHudShown = Rig.Controller->GetHUD() && Rig.Controller->GetHUD()->bShowHUD;
			State.bMoveIgnored = Rig.Controller->IsMoveInputIgnored();
			State.bLookIgnored = Rig.Controller->IsLookInputIgnored();
			State.Masks = CountComponents<UPostProcessComponent>(Rig.Controller);
		}
		State.Dilation = World.GetWorldSettings()->TimeDilation;
		State.Sun = Rig.Sun ? Rig.Sun->Intensity : 0.f;
		State.Sky = Rig.Sky ? Rig.Sky->Intensity : 0.f;
		State.KeyLights = CountComponents<USpotLightComponent>(IsValid(Rig.Pawn) ? Rig.Pawn : nullptr);
		State.Cameras = Count<ACameraActor>(World);
		State.Sequences = Count<ALevelSequenceActor>(World);
		return State;
	}

	bool ViewIsStage(const FRig& Rig)
	{
		const APlayerCameraManager* Manager = Rig.Controller ? Rig.Controller->PlayerCameraManager.Get() : nullptr;
		const AActor* Pending = Manager ? Manager->PendingViewTarget.Target.Get() : nullptr;
		return Cast<ACameraActor>(Rig.Controller->GetViewTarget()) || Cast<ACameraActor>(Pending);
	}

	/** One game with the stage source loaded and a player rigged in its world. */
	struct FStageGame
	{
		FAutomationTestBase& Test;
		FDnaScratchAssets Assets;
		TUniquePtr<FDnaStandaloneGame> Game;
		FRig Rig;
		UDreamNarrativeStageSubsystem* Stages = nullptr;

		explicit FStageGame(FAutomationTestBase& InTest)
			: Test(InTest), Assets(InTest, { StageSource })
		{
			if (!Assets.IsValid())
			{
				return;
			}
			Game = MakeUnique<FDnaStandaloneGame>();
			Rig = MakeRig(*Game->GetWorld());
			Game->GetStory()->LoadAllScripts();
			Stages = Game->GetGameInstance()->GetSubsystem<UDreamNarrativeStageSubsystem>();
		}

		~FStageGame()
		{
			if (::IsValid(Rig.Controller))
			{
				Rig.Controller->Player = nullptr;
			}
			Game.Reset();
			DropRig(Rig);
		}

		bool IsValid() const { return Game.IsValid() && Stages && Rig.Controller && Rig.Pawn; }
		UWorld& World() const { return *Game->GetWorld(); }
		const FDnaStageData* Stage(FName Name) const { return Stages ? Stages->FindStage(Name) : nullptr; }

		bool Same(const TCHAR* What, const FWorldState& Before)
		{
			const FWorldState After = Read(World(), Rig);
			return Test.TestEqual(What, After.Describe(), Before.Describe());
		}
	};

	/** Advances a director to Seconds into the stage it plays now, a frame at a time; stops early when that stage ends. */
	void AdvanceTo(UDreamNarrativeStageDirector& Director, float Seconds)
	{
		const uint32 Play = Director.GetPlayNumber();
		while (Play != 0 && Director.GetPlayNumber() == Play && Director.GetSeconds() + Frame * 0.5f < Seconds)
		{
			Director.Advance(FMath::Min(Frame, Seconds - Director.GetSeconds()));
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaStageSmokeRunTest, "DreamNarrative.Smoke.InProcess.StagePutsTheWorldBack", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDnaStageSmokeRunTest::RunTest(const FString& Parameters)
{
	using namespace UE::DreamNarrative::Tests::StageSmoke;

	FStageGame Session(*this);
	const FDnaStageData* Cutscene = Session.Stage(TEXT("过场"));
	if (!TestTrue(TEXT("the game is rigged"), Session.IsValid()) || !TestNotNull(TEXT("the stage is loaded"), Cutscene))
	{
		return false;
	}
	UDreamNarrativeStageDirector* Director = UDreamNarrativeStageDirector::FindOrAddDirector(Session.Rig.Controller);
	Director->bManualAdvance = true;
	EDnaStageEnd Ended = EDnaStageEnd::Watchdog;
	int32 Ends = 0;
	Director->OnPlayEnded.AddLambda([&Ended, &Ends](uint32, EDnaStageEnd Reason) { Ended = Reason; ++Ends; });

	const FWorldState Before = Read(Session.World(), Session.Rig);
	TestTrue(TEXT("plays"), Director->Play(*Cutscene) != 0);

	// Every frame the world is what the pose of that moment says: scrubbing to a time and playing to it agree.
	bool bPoseMatches = true;
	bool bSawLocked = false, bSawHud = false, bSawDim = false, bSawMask = false, bSawKey = false, bSawSequence = false, bSawStop = false, bSawCamera = false;
	while (Director->IsPlaying())
	{
		const float Unit = FMath::Min(Director->GetSeconds(), Director->GetEndSeconds());
		bPoseMatches &= Director->GetPose().NearlyEquals(DreamNarrative::SampleDnaStage(*Cutscene, Unit));
		const FWorldState Now = Read(Session.World(), Session.Rig);
		if (Unit > 0.4f && Unit < 0.6f)
		{
			bSawLocked |= Now.bMoveIgnored && Now.bLookIgnored;
			bSawHud |= !Now.bHudShown;
			bSawDim |= Now.Sun < Before.Sun && Now.Sky < Before.Sky;
			bSawKey |= Now.KeyLights == 1;
			bSawSequence |= Now.Sequences == 1;
			bSawCamera |= ViewIsStage(Session.Rig);
		}
		if (Unit > 0.95f && Unit < 1.05f)
		{
			bSawMask |= Now.Masks == 1;
		}
		if (Unit > 1.05f && Unit < 1.15f)
		{
			bSawStop |= FMath::IsNearlyEqual(Now.Dilation, 0.2f * Before.Dilation, 1e-3f);
		}
		Director->Advance(Frame);
	}
	TestTrue(TEXT("each frame matches the stage sampled at its time"), bPoseMatches);
	TestTrue(TEXT("the stage took the camera"), bSawCamera);
	TestTrue(TEXT("the player could not move or look"), bSawLocked);
	TestTrue(TEXT("the HUD went"), bSawHud);
	TestTrue(TEXT("the sun and sky went down"), bSawDim);
	TestTrue(TEXT("a key light on the player"), bSawKey);
	TestTrue(TEXT("the level sequence played"), bSawSequence);
	TestTrue(TEXT("a mask over the cut"), bSawMask);
	TestTrue(TEXT("the hit stopped the world"), bSawStop);
	TestEqual(TEXT("it ran to its end"), Ended, EDnaStageEnd::Finished);
	TestEqual(TEXT("once"), Ends, 1);
	TestTrue(TEXT("it took 3.3 s, its blend back included"), FMath::IsNearlyEqual(Director->GetSeconds(), 3.3f, Frame));
	Session.Same(TEXT("and the world is as it was"), Before);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaStageSmokeCutShortTest, "DreamNarrative.Smoke.InProcess.StageCutShortPutsTheWorldBack", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDnaStageSmokeCutShortTest::RunTest(const FString& Parameters)
{
	using namespace UE::DreamNarrative::Tests::StageSmoke;

	FStageGame Session(*this);
	const FDnaStageData* Cutscene = Session.Stage(TEXT("过场"));
	if (!TestTrue(TEXT("the game is rigged"), Session.IsValid()) || !TestNotNull(TEXT("the stage is loaded"), Cutscene))
	{
		return false;
	}
	UDreamNarrativeStageDirector* Director = UDreamNarrativeStageDirector::FindOrAddDirector(Session.Rig.Controller);
	Director->bManualAdvance = true;
	EDnaStageEnd Ended = EDnaStageEnd::Finished;
	Director->OnPlayEnded.AddLambda([&Ended](uint32, EDnaStageEnd Reason) { Ended = Reason; });
	const FWorldState Before = Read(Session.World(), Session.Rig);

	// Stopped at any moment: the start, under a blend, under the mask, in the hitstop, with the lights coming back, in the blend back.
	for (const float At : { 0.f, 0.1f, 0.5f, 1.f, 1.1f, 2.f, 2.7f, 3.1f })
	{
		Director->Play(*Cutscene);
		AdvanceTo(*Director, At);
		Director->Stop();
		TestEqual(*FString::Printf(TEXT("stopped at %.1f s"), At), Ended, EDnaStageEnd::Stopped);
		Session.Same(*FString::Printf(TEXT("the world after a stop at %.1f s"), At), Before);
	}

	// Replaced: a new stage over one still playing takes over cleanly, and the world comes back once.
	Director->Play(*Cutscene);
	AdvanceTo(*Director, 1.f);
	Director->Play(*Cutscene);
	TestEqual(TEXT("the first was replaced"), Ended, EDnaStageEnd::Replaced);
	TestEqual(TEXT("one stage camera, not two"), Read(Session.World(), Session.Rig).Cameras, 1);
	AdvanceTo(*Director, 0.5f);
	Director->Stop();
	Session.Same(TEXT("the world after a replaced stage"), Before);

	// The game's own interrupt for the whole world.
	Director->Play(*Cutscene);
	AdvanceTo(*Director, 0.5f);
	UDreamNarrativeStageDirector::InterruptAll(&Session.World(), EDnaStageEnd::Death);
	TestEqual(TEXT("interrupted for the world"), Ended, EDnaStageEnd::Death);
	Session.Same(TEXT("the world after an interrupt"), Before);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaStageSmokeDeathTest, "DreamNarrative.Smoke.InProcess.StageEndsWhenThePlayerDies", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDnaStageSmokeDeathTest::RunTest(const FString& Parameters)
{
	using namespace UE::DreamNarrative::Tests::StageSmoke;

	FStageGame Session(*this);
	const FDnaStageData* Cutscene = Session.Stage(TEXT("过场"));
	if (!TestTrue(TEXT("the game is rigged"), Session.IsValid()) || !TestNotNull(TEXT("the stage is loaded"), Cutscene))
	{
		return false;
	}
	FRig& Rig = Session.Rig;

	// -- on the controller: the pawn it played for is let go
	UDreamNarrativeStageDirector* Director = UDreamNarrativeStageDirector::FindOrAddDirector(Rig.Controller);
	Director->bManualAdvance = true;
	EDnaStageEnd Ended = EDnaStageEnd::Finished;
	Director->OnPlayEnded.AddLambda([&Ended](uint32, EDnaStageEnd Reason) { Ended = Reason; });
	const FWorldState Before = Read(Session.World(), Rig);
	Director->Play(*Cutscene);
	AdvanceTo(*Director, 1.f);
	Rig.Controller->UnPossess();
	Director->Advance(Frame);
	TestEqual(TEXT("the player let go: a death"), Ended, EDnaStageEnd::Death);
	FWorldState Expected = Before;
	Expected.ViewTarget = GetNameSafe(Rig.Controller->GetViewTarget());
	TestFalse(TEXT("the view is not left on the stage camera"), ViewIsStage(Rig));
	Session.Same(TEXT("the world after the death"), Expected);

	// -- on the pawn: the pawn destroyed
	Rig.Controller->Possess(Rig.Pawn);
	const FWorldState Possessed = Read(Session.World(), Rig);
	UDreamNarrativeStageDirector* OnPawn = UDreamNarrativeStageDirector::FindOrAddDirector(Rig.Pawn);
	OnPawn->bManualAdvance = true;
	EDnaStageEnd PawnEnded = EDnaStageEnd::Finished;
	OnPawn->OnPlayEnded.AddLambda([&PawnEnded](uint32, EDnaStageEnd Reason) { PawnEnded = Reason; });
	OnPawn->Play(*Cutscene);
	AdvanceTo(*OnPawn, 1.1f);
	TestTrue(TEXT("the pawn's director holds the controller's input"), Rig.Controller->IsMoveInputIgnored());
	Rig.Pawn->Destroy();
	TestEqual(TEXT("its pawn destroyed: a death"), PawnEnded, EDnaStageEnd::Death);
	Rig.Pawn = nullptr;
	Expected = Possessed;
	Expected.ViewTarget = GetNameSafe(Rig.Controller->GetViewTarget());
	Expected.KeyLights = 0;
	TestFalse(TEXT("the view is not left on the stage camera"), ViewIsStage(Rig));
	Session.Same(TEXT("the world after the pawn went"), Expected);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaStageSmokeStoryTest, "DreamNarrative.Smoke.InProcess.StoryStagesInTheGame", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDnaStageSmokeStoryTest::RunTest(const FString& Parameters)
{
	using namespace UE::DreamNarrative::Tests::StageSmoke;

	FStageGame Session(*this);
	if (!TestTrue(TEXT("the game is rigged"), Session.IsValid()))
	{
		return false;
	}
	UDreamNarrativeStory* Story = Session.Game->GetStory();
	const FWorldState Before = Read(Session.World(), Session.Rig);

	// The chatter talks; then 甲's dialogue plays the cutscene on the player's controller.
	FDnaEventPayload Idle;
	Idle.Event = TEXT("闲着");
	Story->GetLedger()->RaiseEvent(Idle);
	UDreamNarrativeDialogueRunner* Chatter = Story->GetRunner(TEXT("闲话"));
	UDreamNarrativeDialogueRunner* Runner = Story->TalkTo(TEXT("甲"));
	if (!TestNotNull(TEXT("the chatter talks"), Chatter) || !TestNotNull(TEXT("甲 talks"), Runner))
	{
		return false;
	}
	Runner->Advance();
	TestEqual(TEXT("the cutscene holds the dialogue"), Runner->GetState(), EDnaRunnerState::PlayingStage);
	UDreamNarrativeStageDirector* Director = Session.Rig.Controller->FindComponentByClass<UDreamNarrativeStageDirector>();
	if (!TestNotNull(TEXT("a director on the player's controller"), Director))
	{
		return false;
	}
	Director->bManualAdvance = true;
	TestEqual(TEXT("playing the cutscene"), Director->GetStageName(), FName(TEXT("过场")));
	TestTrue(TEXT("the chatter holds still"), Chatter->IsPaused());

	AdvanceTo(*Director, 3.4f);
	TestFalse(TEXT("the cutscene is over"), Director->GetStageName() == FName(TEXT("过场")));
	TestFalse(TEXT("the chatter goes on"), Chatter->IsPaused());
	TestEqual(TEXT("the dialogue goes on"), Runner->GetCurrentLine().Text.ToString(), FString(TEXT("就这样。")));

	// That line carries a stage of its own: it plays while the line is up, and goes with it.
	TestEqual(TEXT("the line's stage plays"), Director->GetStageName(), FName(TEXT("推近")));
	AdvanceTo(*Director, 0.5f);
	TestTrue(TEXT("pushing in"), Director->GetPose().Camera.Push > 0.f);
	Runner->Advance();
	TestFalse(TEXT("the line gone, its stage stopped"), Director->IsPlaying());
	Session.Same(TEXT("the world after the story's stages"), Before);

	// The level goes while the cutscene plays: the stage ends, and the story is told so.
	Runner->Advance();
	Runner = Story->TalkTo(TEXT("甲"));
	Runner->Advance();
	EDnaStageEnd Ended = EDnaStageEnd::Finished;
	Director->OnPlayEnded.AddLambda([&Ended](uint32, EDnaStageEnd Reason) { Ended = Reason; });
	AdvanceTo(*Director, 1.f);
	Session.World().BeginTearingDown();
	TestEqual(TEXT("the level going ends the stage"), Ended, EDnaStageEnd::LevelChange);
	TestEqual(TEXT("and the dialogue goes on"), Runner->GetCurrentLine().Text.ToString(), FString(TEXT("就这样。")));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaStageSmokeAbilityTest, "DreamNarrative.Smoke.InProcess.AbilitiesAndStages", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDnaStageSmokeAbilityTest::RunTest(const FString& Parameters)
{
	using namespace UE::DreamNarrative::Tests::StageSmoke;

	FStageGame Session(*this);
	if (!TestTrue(TEXT("the game is rigged"), Session.IsValid()))
	{
		return false;
	}
	// The tags are ones the test host registers (Tools/TestHost/Template/Config/Tags).
	const FGameplayTag Finisher = FGameplayTag::RequestGameplayTag(TEXT("DreamNarrativeTest.Ability.Finisher"), false);
	const FGameplayTag Done = FGameplayTag::RequestGameplayTag(TEXT("DreamNarrativeTest.Quest.Done"), false);
	if (!TestTrue(TEXT("the host registers the tags"), Finisher.IsValid() && Done.IsValid()))
	{
		return false;
	}
	FRig& Rig = Session.Rig;
	UDreamNarrativeGASBridge* Bridge = Session.Game->GetGameInstance()->GetSubsystem<UDreamNarrativeGASBridge>();
	UAbilitySystemComponent* System = NewObject<UAbilitySystemComponent>(Rig.Pawn);
	System->RegisterComponent();
	System->InitAbilityActorInfo(Rig.Pawn, Rig.Pawn);
	Bridge->WatchAbilitySystem(System, /*bMirrorTags=*/false);
	const FWorldState Before = Read(Session.World(), Rig);

	// -- a gameplay event a stage maps plays it on the system's avatar, the event's target bound
	FGameplayEventData Payload;
	Payload.Target = Rig.Boss;
	System->HandleGameplayEvent(Finisher, &Payload);
	UDreamNarrativeStageDirector* Director = Rig.Pawn->FindComponentByClass<UDreamNarrativeStageDirector>();
	if (!TestNotNull(TEXT("a director on the avatar"), Director))
	{
		return false;
	}
	Director->bManualAdvance = true;
	TestEqual(TEXT("playing the finisher"), Director->GetStageName(), FName(TEXT("处决")));
	TestTrue(TEXT("its target bound"), Director->ResolveActor(TEXT("target")) == Rig.Boss);
	TestTrue(TEXT("the player held"), Rig.Controller->IsMoveInputIgnored());
	AdvanceTo(*Director, 1.3f);
	TestFalse(TEXT("over"), Director->IsPlaying());
	Session.Same(TEXT("the world after the event's stage"), Before);

	// -- a completed quest sends its gameplay event to the watched systems
	int32 Heard = 0;
	System->GenericGameplayEventCallbacks.FindOrAdd(Done).AddLambda([&Heard](const FGameplayEventData*) { ++Heard; });
	UDreamNarrativeStory* Story = Session.Game->GetStory();
	TestTrue(TEXT("take the quest on"), Story->AcceptQuest(TEXT("猎狼")));
	TestEqual(TEXT("nothing sent yet"), Heard, 0);
	Story->GetLedger()->SetFact(TEXT("狼"), FDnaValue::FromInt(1), EDnaWriteSource::Game);
	TestEqual(TEXT("completed"), Story->GetQuestStateOf(TEXT("猎狼")), EDnaQuestState::Completed);
	TestEqual(TEXT("and the event sent once"), Heard, 1);

	// -- an ability plays the stage and ends with it
	UDnaTestStageAbility::StageToPlay = TEXT("处决");
	UDnaTestStageAbility::StageTarget = Rig.Boss;
	UDnaTestStageAbility::Ends = 0;
	const FGameplayAbilitySpecHandle Ability = System->GiveAbility(FGameplayAbilitySpec(UDnaTestStageAbility::StaticClass()));
	TestTrue(TEXT("the ability activates"), System->TryActivateAbility(Ability));
	TestEqual(TEXT("and plays the stage"), Director->GetStageName(), FName(TEXT("处决")));
	AdvanceTo(*Director, 1.3f);
	const FGameplayAbilitySpec* Spec = System->FindAbilitySpecFromHandle(Ability);
	TestEqual(TEXT("the task heard the stage end"), UDnaTestStageAbility::Ends, 1);
	TestEqual(TEXT("at its end"), UDnaTestStageAbility::LastEnd, EDnaStageEnd::Finished);
	TestTrue(TEXT("and the ability ended"), Spec && !Spec->IsActive());
	Session.Same(TEXT("the world after the ability's stage"), Before);

	// -- the ability cancelled: the stage goes with it
	TestTrue(TEXT("again"), System->TryActivateAbility(Ability));
	AdvanceTo(*Director, 0.4f);
	TestTrue(TEXT("playing"), Director->IsPlaying());
	System->CancelAbilityHandle(Ability);
	TestFalse(TEXT("cancelling the ability stops its stage"), Director->IsPlaying());
	Session.Same(TEXT("the world after the cancelled ability"), Before);

	Bridge->UnwatchAbilitySystem(System);
	UDnaTestStageAbility::StageTarget.Reset();
	return !HasAnyErrors();
}

#endif
