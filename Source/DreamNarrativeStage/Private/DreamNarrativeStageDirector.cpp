// Copyright DreamMoon. All Rights Reserved.

#include "DreamNarrativeStageDirector.h"
#include "DreamNarrativeLedger.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "AssetRegistry/ARFilter.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "DreamNarrativeSettings.h"
#include "DreamNarrativeStageArbiter.h"
#include "DreamNarrativeStageModule.h"
#include "DreamNarrativeStageSubsystem.h"
#include "DreamNarrativeStageWatchdog.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/HUD.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "LevelSequence.h"
#include "LevelSequenceActor.h"
#include "LevelSequencePlayer.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/PackageName.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"

namespace
{
	const TCHAR* SlotName(uint8 Slot)
	{
		static const TCHAR* Names[] = { TEXT("camera"), TEXT("light"), TEXT("key light"), TEXT("mask"), TEXT("vfx"), TEXT("hud"), TEXT("text"),
			TEXT("anim"), TEXT("input"), TEXT("time"), TEXT("damage window"), TEXT("sequence") };
		return Slot < UE_ARRAY_COUNT(Names) ? Names[Slot] : TEXT("?");
	}

	/** Puts the view on Target over Blend seconds. */
	void SetView(APlayerController& Controller, AActor* Target, float Blend)
	{
		Controller.SetViewTargetWithBlend(Target, Blend, VTBlend_Cubic, 0.f, false);
	}
}

UDreamNarrativeStageDirector::UDreamNarrativeStageDirector()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

// ---------------------------------------------------------------- playing

uint32 UDreamNarrativeStageDirector::Play(const FDnaStageData& InStage, float LineSeconds)
{
	const TWeakObjectPtr<UDreamNarrativeLedger> Reporting = NextActivityLedger;
	NextActivityLedger.Reset();
	if (bEnding) { return 0; }
	if (InStage.bLineRelative && LineSeconds <= 0.f)
	{
		UE_LOG(LogDreamNarrativeStage, Warning, TEXT("Stage %s follows a line; it plays only with the line it is attached to"), *InStage.Name.ToString());
		return 0;
	}
	const FDnaStageData RequestedStage = InStage;
	const uint32 Request = ++PlayRequestSerial;
	if (bPlaying)
	{
		End(EDnaStageEnd::Replaced);
	}
	// An end listener may have started a newer request. It owns the director now.
	if (Request != PlayRequestSerial) { return 0; }

	Stage = RequestedStage;
	UnitSeconds = Stage.bLineRelative ? LineSeconds : 1.f;
	Seconds = 0.f;
	NextCue = 0;
	Pose = FDnaStagePose();
	bPlaying = true;
	bEnding = false;
	if (++PlayNumber == 0) { PlayNumber = 1; }
	const uint32 ThisPlay = PlayNumber;

	APawn* Pawn = GetPawn();
	StartPawn = Pawn;
	bHadPawn = Pawn != nullptr;
	if (const UWorld* World = GetWorld())
	{
		if (UDreamNarrativeStageWatchdog* Watchdog = World->GetSubsystem<UDreamNarrativeStageWatchdog>())
		{
			Watchdog->Watch(this);
		}
	}
	UE_LOG(LogDreamNarrativeStage, Verbose, TEXT("Stage %s starts on %s"), *Stage.Name.ToString(), *GetNameSafe(GetOwner()));

	ActivityLedger = Reporting;
	if (!ActivityLedger.IsValid())
	{
		const UWorld* World = GetWorld();
		const UGameInstance* Instance = World ? World->GetGameInstance() : nullptr;
		ActivityLedger = Instance ? Instance->GetSubsystem<UDreamNarrativeLedger>() : nullptr;
	}
	if (UDreamNarrativeLedger* Ledger = ActivityLedger.Get())
	{
		Ledger->OnStageActivity.Broadcast(Stage.Name, true, Stage.bLineRelative);
	}
	if (!bPlaying || PlayNumber != ThisPlay) { return ThisPlay; }
	OnStageStarted.Broadcast(Stage.Name);
	if (!bPlaying || PlayNumber != ThisPlay)
	{
		return ThisPlay;
	}
	// The pose at the start, and what starts at once; ending waits for time to pass.
	FireCuesUpTo(0.f);
	if (bPlaying && PlayNumber == ThisPlay)
	{
		ApplyPose(DreamNarrative::SampleDnaStage(Stage, 0.f, UnitSeconds));
	}
	return ThisPlay;
}

void UDreamNarrativeStageDirector::ReportActivityTo(UDreamNarrativeLedger* Ledger)
{
	NextActivityLedger = Ledger;
}

bool UDreamNarrativeStageDirector::PlayStage(FName StageName)
{
	const UWorld* World = GetWorld();
	const UGameInstance* Instance = World ? World->GetGameInstance() : nullptr;
	const UDreamNarrativeStageSubsystem* Stages = Instance ? Instance->GetSubsystem<UDreamNarrativeStageSubsystem>() : nullptr;
	const FDnaStageData* Data = Stages ? Stages->FindStage(StageName) : nullptr;
	if (!Data)
	{
		UE_LOG(LogDreamNarrativeStage, Warning, TEXT("No registered script declares a stage called %s"), *StageName.ToString());
		return false;
	}
	return Play(*Data) != 0;
}

void UDreamNarrativeStageDirector::Stop(EDnaStageEnd Reason)
{
	End(Reason);
}

float UDreamNarrativeStageDirector::GetEndSeconds() const
{
	return (Stage.Duration + Stage.GetTail()) * UnitSeconds;
}

void UDreamNarrativeStageDirector::Advance(float DeltaSeconds)
{
	if (!bPlaying || bEnding)
	{
		return;
	}
	const uint32 ThisPlay = PlayNumber;
	++AdvanceCount;
	// The pawn the stage started with is gone, or the controller let it go: the player died.
	if (bHadPawn && (!StartPawn.IsValid() || GetPawn() != StartPawn.Get()))
	{
		End(EDnaStageEnd::Death);
		return;
	}

	Seconds += FMath::Max(DeltaSeconds, 0.f);
	const float EndSeconds = GetEndSeconds();
	const float Unit = FMath::Min(Seconds, EndSeconds) / UnitSeconds;
	FireCuesUpTo(Unit);
	if (!bPlaying || PlayNumber != ThisPlay)
	{
		return;
	}
	ApplyPose(DreamNarrative::SampleDnaStage(Stage, Unit, UnitSeconds));
	if (bPlaying && PlayNumber == ThisPlay && Seconds >= EndSeconds && DeltaSeconds > 0.f)
	{
		End(EDnaStageEnd::Finished);
	}
}

void UDreamNarrativeStageDirector::End(EDnaStageEnd Reason)
{
	if (!bPlaying || bEnding)
	{
		return;
	}
	bEnding = true;
	UE_LOG(LogDreamNarrativeStage, Verbose, TEXT("Stage %s ends (%s) after %.2f s"), *Stage.Name.ToString(), *UEnum::GetValueAsString(Reason), Seconds);
	Unwind();
	if (const UWorld* World = GetWorld())
	{
		if (UDreamNarrativeStageWatchdog* Watchdog = World->GetSubsystem<UDreamNarrativeStageWatchdog>())
		{
			Watchdog->Unwatch(this);
		}
	}
	bPlaying = false;
	bEnding = false;
	const FName Name = Stage.Name;
	const uint32 Number = PlayNumber;
	const bool bOnLine = Stage.bLineRelative;
	UDreamNarrativeLedger* Ledger = ActivityLedger.Get();
	ActivityLedger.Reset();
	if (Ledger) { Ledger->OnStageActivity.Broadcast(Name, false, bOnLine); }
	OnStageEnded.Broadcast(Name, Reason);
	OnPlayEnded.Broadcast(Number, Reason);
}

// ---------------------------------------------------------------- the world as the pose says

void UDreamNarrativeStageDirector::Touch(ESlot Slot)
{
	Touched.AddUnique(Slot);
}

TArray<FString> UDreamNarrativeStageDirector::DescribeTouched() const
{
	TArray<FString> Out;
	for (const ESlot Slot : Touched)
	{
		Out.Add(SlotName(static_cast<uint8>(Slot)));
	}
	return Out;
}

void UDreamNarrativeStageDirector::ApplyPose(const FDnaStagePose& NewPose)
{
	if (!bPlaying || bEnding) { return; }
	const uint32 ThisPlay = PlayNumber;
	Pose = NewPose;
	ApplyCamera(NewPose.Camera);
	ApplyLight(NewPose);
	ApplyMask(NewPose);

	if (NewPose.bHudHidden != bHudHidden)
	{
		if (NewPose.bHudHidden)
		{
			Touch(ESlot::Hud);
			const APlayerController* Controller = GetController();
			Hud = Controller ? Controller->GetHUD() : nullptr;
			if (UDreamNarrativeStageArbiter* Arbiter = GetArbiter())
			{
				Arbiter->HideHud(this, Hud.Get());
			}
		}
		else if (UDreamNarrativeStageArbiter* Arbiter = GetArbiter())
		{
			Arbiter->ReleaseHud(this);
		}
		bHudHidden = NewPose.bHudHidden;
		OnHudVisibility.Broadcast(!bHudHidden);
		if (!bPlaying || bEnding || PlayNumber != ThisPlay) { return; }
	}

	if (!NewPose.Text.EqualTo(ShownText) || NewPose.Text.IsEmpty() != ShownText.IsEmpty())
	{
		if (!NewPose.Text.IsEmpty())
		{
			Touch(ESlot::Text);
		}
		ShownText = NewPose.Text;
		OnText.Broadcast(ShownText, NewPose.TextStyle);
		if (!bPlaying || bEnding || PlayNumber != ThisPlay) { return; }
	}

	ApplyGame(NewPose);
}

void UDreamNarrativeStageDirector::ApplyCamera(const FDnaStageCameraPose& Camera)
{
	APlayerController* Controller = bViewTaken ? CameraController.Get() : GetController();
	if (!Camera.bActive)
	{
		if (bViewTaken)
		{
			// The view is back with the game: let the stage camera go.
			UnwindSlot(ESlot::Camera);
		}
		return;
	}
	if (!bViewTaken)
	{
		UWorld* World = GetWorld();
		// What a stage shows is shown on this machine: a controller of a remote player keeps its view.
		// (Its camera manager would also send every change back to itself as a client call.)
		if (!Controller || !Controller->PlayerCameraManager || !Controller->IsLocalController() || !World)
		{
			return;
		}
		Touch(ESlot::Camera);
		CameraController = Controller;
		GameView = Controller->PlayerCameraManager->GetCameraCacheView();
		if (GameView.FOV <= 0.f)
		{
			GameView.FOV = Controller->PlayerCameraManager->GetFOVAngle() > 0.f ? Controller->PlayerCameraManager->GetFOVAngle() : 90.f;
		}
		FActorSpawnParameters Params;
		Params.Owner = GetOwner();
		Params.ObjectFlags |= RF_Transient;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		StageCamera = World->SpawnActor<ACameraActor>(GameView.Location, GameView.Rotation, Params);
		if (ACameraActor* Spawned = StageCamera.Get())
		{
			Spawned->GetCameraComponent()->SetConstraintAspectRatio(false);
			Spawned->GetCameraComponent()->SetFieldOfView(GameView.FOV);
			if (UDreamNarrativeStageArbiter* Arbiter = GetArbiter())
			{
				Arbiter->TakeView(this, Controller, Spawned);
			}
			SetView(*Controller, Spawned, Camera.BlendIn);
		}
		bViewTaken = true;
		bViewReturning = false;
	}

	if (ACameraActor* Spawned = StageCamera.Get())
	{
		const FMinimalViewInfo View = DreamNarrative::EvaluateDnaStageView(Stage, Camera, GameView, [this](FName Role) { return ResolveActor(Role); });
		Spawned->SetActorLocationAndRotation(View.Location, View.Rotation);
		Spawned->GetCameraComponent()->SetFieldOfView(View.FOV);
	}

	// A stage that took the view after this one shows over it: this one leaves the view alone.
	const UDreamNarrativeStageArbiter* Arbiter = GetArbiter();
	if (Controller && (!Arbiter || Arbiter->IsShowingView(this)))
	{
		if (Camera.bReturning && !bViewReturning)
		{
			SetView(*Controller, GetViewBack(*Controller), Camera.BlendOut);
			bViewReturning = true;
		}
		else if (!Camera.bReturning && bViewReturning && StageCamera.IsValid())
		{
			// Taken again before the blend back was over.
			SetView(*Controller, StageCamera.Get(), 0.f);
			bViewReturning = false;
		}
	}
}

void UDreamNarrativeStageDirector::ApplyLight(const FDnaStagePose& NewPose)
{
	if (!FMath::IsNearlyEqual(NewPose.LightShare, AppliedLightShare))
	{
		if (UDreamNarrativeStageArbiter* Arbiter = GetArbiter())
		{
			Touch(ESlot::Light);
			Arbiter->SetLightShare(this, NewPose.LightShare);
		}
		AppliedLightShare = NewPose.LightShare;
	}

	// Key lights: one spot light per actor that has one on, put up and taken down with the pose.
	for (auto It = KeyLights.CreateIterator(); It; ++It)
	{
		if (!NewPose.KeyLights.Contains(It.Key()))
		{
			if (ULightComponent* Light = It.Value().Get())
			{
				Light->DestroyComponent();
			}
			It.RemoveCurrent();
		}
	}
	for (const FName& Name : NewPose.KeyLights)
	{
		if (KeyLights.Contains(Name))
		{
			continue;
		}
		AActor* Actor = ResolveActor(Name);
		USceneComponent* Root = Actor ? Actor->GetRootComponent() : nullptr;
		if (!Root)
		{
			KeyLights.Add(Name, nullptr);
			continue;
		}
		Touch(ESlot::KeyLight);
		USpotLightComponent* Light = NewObject<USpotLightComponent>(Actor, NAME_None, RF_Transient);
		Light->SetupAttachment(Root);
		Light->RegisterComponent();
		const FVector Target = Actor->GetActorLocation();
		const FVector Position = Target + Actor->GetActorForwardVector() * 200.f + FVector::UpVector * 150.f;
		Light->SetWorldLocationAndRotation(Position, (Target - Position).Rotation());
		Light->SetIntensity(8000.f);
		Light->SetAttenuationRadius(1000.f);
		Light->SetOuterConeAngle(30.f);
		Light->SetCastShadows(false);
		KeyLights.Add(Name, Light);
	}
}

void UDreamNarrativeStageDirector::ApplyMask(const FDnaStagePose& NewPose)
{
	const bool bWanted = NewPose.HasMask();
	if (bWanted && !Mask.IsValid())
	{
		AActor* Owner = GetOwner();
		if (!Owner)
		{
			return;
		}
		Touch(ESlot::Mask);
		UPostProcessComponent* Component = NewObject<UPostProcessComponent>(Owner, NAME_None, RF_Transient);
		Component->bUnbound = true;
		Component->Priority = 10000.f;
		Component->RegisterComponent();
		Mask = Component;
	}
	UPostProcessComponent* Component = Mask.Get();
	if (!Component)
	{
		return;
	}

	FPostProcessSettings Settings;
	const UDreamNarrativeSettings* Project = UDreamNarrativeSettings::Get();
	// A verb with a material of the project's own drives its `Strength`; the rest use the engine's settings.
	auto ByMaterial = [&](const TCHAR* Verb, float Strength) -> bool
	{
		const TSoftObjectPtr<UMaterialInterface>* Soft = Project->MaskMaterials.Find(FName(Verb));
		if (!Soft || Soft->IsNull())
		{
			return false;
		}
		TWeakObjectPtr<UMaterialInstanceDynamic>& Instance = MaskMaterials.FindOrAdd(FName(Verb));
		if (!Instance.IsValid())
		{
			if (UMaterialInterface* Material = Soft->LoadSynchronous())
			{
				Instance = UMaterialInstanceDynamic::Create(Material, Component);
			}
		}
		if (UMaterialInstanceDynamic* Dynamic = Instance.Get())
		{
			Dynamic->SetScalarParameterValue(TEXT("Strength"), Strength);
			Settings.AddBlendable(Dynamic, Strength > 0.f ? 1.f : 0.f);
			return true;
		}
		return false;
	};

	if (NewPose.Distort > 0.f && !ByMaterial(TEXT("distort"), NewPose.Distort))
	{
		Settings.bOverride_SceneFringeIntensity = true;
		Settings.SceneFringeIntensity = 5.f * NewPose.Distort;
	}
	if (NewPose.ExposureEv != 0.f && !ByMaterial(TEXT("exposure"), NewPose.ExposureEv))
	{
		Settings.bOverride_AutoExposureBias = true;
		Settings.AutoExposureBias = NewPose.ExposureEv;
	}
	if (NewPose.Streaks > 0.f && !ByMaterial(TEXT("streaks"), NewPose.Streaks))
	{
		Settings.bOverride_BloomIntensity = true;
		Settings.BloomIntensity = FMath::Lerp(0.675f, 8.f, NewPose.Streaks);
	}
	if (NewPose.Comic > 0.f && !ByMaterial(TEXT("comic"), NewPose.Comic))
	{
		Settings.bOverride_ColorSaturation = true;
		Settings.ColorSaturation = FVector4(1.f, 1.f, 1.f, 1.f - 0.8f * NewPose.Comic);
		Settings.bOverride_ColorContrast = true;
		Settings.ColorContrast = FVector4(1.f, 1.f, 1.f, 1.f + 0.6f * NewPose.Comic);
	}
	if (NewPose.Black > 0.f && !ByMaterial(TEXT("black"), NewPose.Black))
	{
		Settings.bOverride_ColorGain = true;
		Settings.ColorGain = FVector4(1.f, 1.f, 1.f, 1.f - NewPose.Black);
	}
	if (NewPose.White > 0.f && !ByMaterial(TEXT("white"), NewPose.White))
	{
		Settings.bOverride_ColorOffset = true;
		Settings.ColorOffset = FVector4(0.f, 0.f, 0.f, NewPose.White);
	}
	Component->Settings = Settings;
	Component->BlendWeight = bWanted ? 1.f : 0.f;
}

void UDreamNarrativeStageDirector::ApplyGame(const FDnaStagePose& NewPose)
{
	if (NewPose.bInputLocked != bInputLocked)
	{
		if (NewPose.bInputLocked)
		{
			if (APlayerController* Controller = GetController())
			{
				Touch(ESlot::Input);
				Controller->SetIgnoreMoveInput(true);
				Controller->SetIgnoreLookInput(true);
				InputController = Controller;
				bInputLocked = true;
			}
		}
		else
		{
			if (APlayerController* Controller = InputController.Get())
			{
				Controller->SetIgnoreMoveInput(false);
				Controller->SetIgnoreLookInput(false);
			}
			bInputLocked = false;
		}
	}

	if (!FMath::IsNearlyEqual(NewPose.TimeDilation, AppliedDilation))
	{
		if (UDreamNarrativeStageArbiter* Arbiter = GetArbiter())
		{
			Touch(ESlot::Time);
			Arbiter->SetTimeDilation(this, NewPose.TimeDilation);
			AppliedDilation = NewPose.TimeDilation;
		}
	}

	if (NewPose.bDamageWindow != bDamageWindow)
	{
		if (NewPose.bDamageWindow)
		{
			Touch(ESlot::Damage);
		}
		bDamageWindow = NewPose.bDamageWindow;
		OnDamageWindow.Broadcast(bDamageWindow);
	}
}

// ---------------------------------------------------------------- cues

void UDreamNarrativeStageDirector::FireCuesUpTo(float Unit)
{
	const uint32 ThisPlay = PlayNumber;
	while (NextCue < Stage.Entries.Num() && Stage.Entries[NextCue].Start <= Unit)
	{
		const int32 Index = NextCue++;
		if (DreamNarrative::IsDnaStageCue(Stage.Entries[Index]))
		{
			FireCue(Index);
			if (!bPlaying || PlayNumber != ThisPlay)
			{
				return;
			}
		}
	}
	// Effects a range spawned go at its end.
	for (auto It = RangeEffects.CreateIterator(); It; ++It)
	{
		if (Stage.Entries.IsValidIndex(It.Key()) && Stage.Entries[It.Key()].End <= Unit)
		{
			if (UNiagaraComponent* Effect = It.Value().Get())
			{
				Effect->DestroyComponent();
			}
			It.RemoveCurrent();
		}
	}
	// The restore block's cues (`vfx stop`) at the end.
	if (Unit >= Stage.Duration && NextCue == Stage.Entries.Num())
	{
		++NextCue;
		for (const FDnaStageEntry& Entry : Stage.Restore)
		{
			if (Entry.Verb == EDnaStageVerb::StopVfx)
			{
				for (const TWeakObjectPtr<UNiagaraComponent>& Effect : Effects)
				{
					if (UNiagaraComponent* Component = Effect.Get()) { Component->DestroyComponent(); }
				}
				Effects.Reset();
				RangeEffects.Reset();
			}
		}
	}
}

void UDreamNarrativeStageDirector::FireCue(int32 EntryIndex)
{
	const FDnaStageEntry& Entry = Stage.Entries[EntryIndex];
	UWorld* World = GetWorld();
	if (!World || World->bIsTearingDown)
	{
		return;
	}
	switch (Entry.Verb)
	{
	case EDnaStageVerb::Montage:
	{
		AActor* Actor = ResolveActor(Entry.Target);
		const USkeletalMeshComponent* Mesh = Actor ? Actor->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
		UAnimInstance* Anim = Mesh ? Mesh->GetAnimInstance() : nullptr;
		UAnimMontage* Montage = ResolveAsset<UAnimMontage>(Entry.Asset);
		if (!WarnIfMissing(Entry, Montage, TEXT("montage")))
		{
			break;
		}
		if (!Anim)
		{
			UE_LOG(LogDreamNarrativeStage, Warning, TEXT("Stage %s, line %d: %s has no animated mesh to play %s on"),
				*Stage.Name.ToString(), Entry.SourceLine, *GetNameSafe(Actor), *Entry.Asset);
			break;
		}
		Touch(ESlot::Anim);
		Anim->Montage_Play(Montage);
		Montages.Emplace(Anim, Montage);
		if (!Entry.WarpTarget.IsNone())
		{
			OnWarpTarget.Broadcast(Actor, Entry.WarpTarget, ResolveActor(Entry.WarpActor));
		}
		break;
	}
	case EDnaStageVerb::Spawn:
	{
		UNiagaraSystem* System = ResolveAsset<UNiagaraSystem>(Entry.Asset);
		AActor* At = ResolveActor(Entry.Target);
		if (!WarnIfMissing(Entry, System, TEXT("effect")) || !At || !At->GetRootComponent())
		{
			break;
		}
		if (Entry.bFlag)
		{
			// `persist`: it outlives the stage, so the stage does not hold it.
			UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, System, At->GetActorLocation(), At->GetActorRotation());
			break;
		}
		if (UNiagaraComponent* Effect = UNiagaraFunctionLibrary::SpawnSystemAttached(System, At->GetRootComponent(), NAME_None,
			FVector::ZeroVector, FRotator::ZeroRotator, EAttachLocation::KeepRelativeOffset, /*bAutoDestroy=*/true))
		{
			Touch(ESlot::Vfx);
			Effects.Add(Effect);
			if (Entry.bRange)
			{
				RangeEffects.Add(EntryIndex, Effect);
			}
		}
		break;
	}
	case EDnaStageVerb::StopVfx:
		for (const TWeakObjectPtr<UNiagaraComponent>& Effect : Effects)
		{
			if (UNiagaraComponent* Component = Effect.Get()) { Component->DestroyComponent(); }
		}
		Effects.Reset();
		RangeEffects.Reset();
		break;
	case EDnaStageVerb::PlaySequence:
	{
		ULevelSequence* Sequence = ResolveAsset<ULevelSequence>(Entry.Asset);
		if (!WarnIfMissing(Entry, Sequence, TEXT("level sequence")))
		{
			break;
		}
		ALevelSequenceActor* Actor = nullptr;
		FMovieSceneSequencePlaybackSettings Settings;
		if (ULevelSequencePlayer* Player = ULevelSequencePlayer::CreateLevelSequencePlayer(World, Sequence, Settings, Actor))
		{
			Touch(ESlot::Sequence);
			Sequences.Add(Actor);
			Player->Play();
		}
		break;
	}
	default:
		break;
	}
}

bool UDreamNarrativeStageDirector::WarnIfMissing(const FDnaStageEntry& Entry, const UObject* Asset, const TCHAR* What) const
{
	if (!Asset)
	{
		UE_LOG(LogDreamNarrativeStage, Warning, TEXT("Stage %s, line %d: no %s called %s"), *Stage.Name.ToString(), Entry.SourceLine, What, *Entry.Asset);
		return false;
	}
	return true;
}

// ---------------------------------------------------------------- putting it back

void UDreamNarrativeStageDirector::Unwind()
{
	for (int32 Index = Touched.Num() - 1; Index >= 0; --Index)
	{
		UnwindSlot(Touched[Index]);
	}
	Touched.Reset();
	Pose = FDnaStagePose();
}

void UDreamNarrativeStageDirector::UnwindSlot(ESlot Slot)
{
	const UWorld* World = GetWorld();
	const bool bTearingDown = !World || World->bIsTearingDown;
	switch (Slot)
	{
	case ESlot::Camera:
		if (bViewTaken)
		{
			APlayerController* Controller = CameraController.Get();
			if (UDreamNarrativeStageArbiter* Arbiter = GetArbiter())
			{
				// The view goes beneath only when this stage was the one on screen.
				Arbiter->ReleaseView(this, IsValid(Controller) && !bTearingDown);
			}
			else if (IsValid(Controller) && !bTearingDown)
			{
				SetView(*Controller, GetViewBack(*Controller), 0.f);
			}
		}
		if (ACameraActor* Spawned = StageCamera.Get())
		{
			if (!bTearingDown) { Spawned->Destroy(); }
		}
		StageCamera.Reset();
		bViewTaken = false;
		bViewReturning = false;
		break;
	case ESlot::Light:
		if (UDreamNarrativeStageArbiter* Arbiter = GetArbiter())
		{
			Arbiter->ReleaseLightShare(this);
		}
		AppliedLightShare = 1.f;
		break;
	case ESlot::KeyLight:
		for (const TPair<FName, TWeakObjectPtr<ULightComponent>>& Light : KeyLights)
		{
			if (ULightComponent* Component = Light.Value.Get()) { Component->DestroyComponent(); }
		}
		KeyLights.Reset();
		break;
	case ESlot::Mask:
		if (UPostProcessComponent* Component = Mask.Get())
		{
			Component->DestroyComponent();
		}
		Mask.Reset();
		MaskMaterials.Reset();
		break;
	case ESlot::Vfx:
		for (const TWeakObjectPtr<UNiagaraComponent>& Effect : Effects)
		{
			if (UNiagaraComponent* Component = Effect.Get()) { Component->DestroyComponent(); }
		}
		Effects.Reset();
		RangeEffects.Reset();
		break;
	case ESlot::Hud:
		if (UDreamNarrativeStageArbiter* Arbiter = GetArbiter())
		{
			Arbiter->ReleaseHud(this);
		}
		if (bHudHidden)
		{
			bHudHidden = false;
			OnHudVisibility.Broadcast(true);
		}
		Hud.Reset();
		break;
	case ESlot::Text:
		if (!ShownText.IsEmpty())
		{
			ShownText = FText::GetEmpty();
			OnText.Broadcast(ShownText, NAME_None);
		}
		break;
	case ESlot::Anim:
		for (const TPair<TWeakObjectPtr<UAnimInstance>, TWeakObjectPtr<UAnimMontage>>& Played : Montages)
		{
			UAnimInstance* Anim = Played.Key.Get();
			const UAnimMontage* Montage = Played.Value.Get();
			if (Anim && Montage && Anim->Montage_IsPlaying(Montage))
			{
				Anim->Montage_Stop(0.f, Montage);
			}
		}
		Montages.Reset();
		break;
	case ESlot::Input:
		if (bInputLocked)
		{
			if (APlayerController* Controller = InputController.Get())
			{
				Controller->SetIgnoreMoveInput(false);
				Controller->SetIgnoreLookInput(false);
			}
			bInputLocked = false;
		}
		InputController.Reset();
		break;
	case ESlot::Time:
		if (UDreamNarrativeStageArbiter* Arbiter = GetArbiter())
		{
			Arbiter->ReleaseTimeDilation(this);
		}
		AppliedDilation = 1.f;
		break;
	case ESlot::Damage:
		if (bDamageWindow)
		{
			bDamageWindow = false;
			OnDamageWindow.Broadcast(false);
		}
		break;
	case ESlot::Sequence:
		for (const TWeakObjectPtr<ALevelSequenceActor>& Weak : Sequences)
		{
			if (ALevelSequenceActor* Actor = Weak.Get())
			{
				if (ULevelSequencePlayer* Player = Actor->GetSequencePlayer())
				{
					Player->Stop();
				}
				if (!bTearingDown) { Actor->Destroy(); }
			}
		}
		Sequences.Reset();
		break;
	}
}

// ---------------------------------------------------------------- actors

APlayerController* UDreamNarrativeStageDirector::GetController() const
{
	AActor* Owner = GetOwner();
	if (APlayerController* Controller = Cast<APlayerController>(Owner))
	{
		return Controller;
	}
	if (const APawn* Pawn = Cast<APawn>(Owner))
	{
		if (APlayerController* Controller = Pawn->GetController<APlayerController>())
		{
			return Controller;
		}
	}
	const UWorld* World = GetWorld();
	return World ? World->GetFirstPlayerController() : nullptr;
}

UDreamNarrativeStageArbiter* UDreamNarrativeStageDirector::GetArbiter() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetSubsystem<UDreamNarrativeStageArbiter>() : nullptr;
}

AActor* UDreamNarrativeStageDirector::GetViewBack(APlayerController& Controller) const
{
	const UDreamNarrativeStageArbiter* Arbiter = GetArbiter();
	if (AActor* Beneath = Arbiter ? Arbiter->GetViewBeneath(this) : nullptr)
	{
		return Beneath;
	}
	return Controller.GetPawn() ? static_cast<AActor*>(Controller.GetPawn()) : &Controller;
}

APawn* UDreamNarrativeStageDirector::GetPawn() const
{
	AActor* Owner = GetOwner();
	if (const APlayerController* Controller = Cast<APlayerController>(Owner))
	{
		return Controller->GetPawn();
	}
	return Cast<APawn>(Owner);
}

void UDreamNarrativeStageDirector::BindActor(FName Role, AActor* Actor)
{
	if (Actor)
	{
		Roles.Add(Role, Actor);
	}
	else
	{
		Roles.Remove(Role);
	}
}

void UDreamNarrativeStageDirector::UnbindActor(FName Role)
{
	Roles.Remove(Role);
}

AActor* UDreamNarrativeStageDirector::ResolveActor(FName Role) const
{
	if (Role.IsNone())
	{
		AActor* Owner = GetOwner();
		if (const APlayerController* Controller = Cast<APlayerController>(Owner))
		{
			return Controller->GetPawn() ? static_cast<AActor*>(Controller->GetPawn()) : Owner;
		}
		return Owner;
	}
	if (const TWeakObjectPtr<AActor>* Bound = Roles.Find(Role))
	{
		if (AActor* Actor = Bound->Get())
		{
			return Actor;
		}
	}
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->ActorHasTag(Role))
			{
				return *It;
			}
		}
	}
	return nullptr;
}

template <typename T>
T* UDreamNarrativeStageDirector::ResolveAsset(const FString& NameOrPath) const
{
	if (NameOrPath.IsEmpty())
	{
		return nullptr;
	}
	if (NameOrPath.StartsWith(TEXT("/")))
	{
		FString Path = NameOrPath;
		if (!Path.Contains(TEXT(".")))
		{
			Path += TEXT(".") + FPackageName::GetShortName(Path);
		}
		return LoadObject<T>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
	}
	const IAssetRegistry* Registry = IAssetRegistry::Get();
	if (!Registry)
	{
		return nullptr;
	}
	const FName Wanted(*NameOrPath);
	for (const FString& Root : UDreamNarrativeSettings::Get()->StageAssetRoots)
	{
		FARFilter Filter;
		Filter.PackagePaths.Add(FName(*Root));
		Filter.bRecursivePaths = true;
		Filter.ClassPaths.Add(T::StaticClass()->GetClassPathName());
		Filter.bRecursiveClasses = true;
		TArray<FAssetData> Assets;
		Registry->GetAssets(Filter, Assets);
		for (const FAssetData& Asset : Assets)
		{
			if (Asset.AssetName == Wanted)
			{
				return Cast<T>(Asset.GetAsset());
			}
		}
	}
	return nullptr;
}

UDreamNarrativeStageDirector* UDreamNarrativeStageDirector::FindOrAddDirector(AActor* Actor)
{
	if (!Actor)
	{
		return nullptr;
	}
	if (UDreamNarrativeStageDirector* Existing = Actor->FindComponentByClass<UDreamNarrativeStageDirector>())
	{
		return Existing;
	}
	UDreamNarrativeStageDirector* Director = NewObject<UDreamNarrativeStageDirector>(Actor, NAME_None, RF_Transient);
	Director->RegisterComponent();
	return Director;
}

void UDreamNarrativeStageDirector::InterruptAll(const UObject* WorldContext, EDnaStageEnd Reason)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (UDreamNarrativeStageWatchdog* Watchdog = World ? World->GetSubsystem<UDreamNarrativeStageWatchdog>() : nullptr)
	{
		for (UDreamNarrativeStageDirector* Director : Watchdog->GetPlaying())
		{
			Director->Stop(Reason);
		}
	}
}

// ---------------------------------------------------------------- lifetime

void UDreamNarrativeStageDirector::BeginPlay()
{
	Super::BeginPlay();
	if (bPlaysStoryStages)
	{
		const UGameInstance* Instance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
		if (UDreamNarrativeStageSubsystem* Stages = Instance ? Instance->GetSubsystem<UDreamNarrativeStageSubsystem>() : nullptr)
		{
			Stages->RegisterDirector(this);
		}
	}
}

void UDreamNarrativeStageDirector::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bPlaying)
	{
		switch (EndPlayReason)
		{
		case EEndPlayReason::Destroyed:
			End(Cast<APawn>(GetOwner()) ? EDnaStageEnd::Death : EDnaStageEnd::OwnerGone);
			break;
		case EEndPlayReason::LevelTransition:
		case EEndPlayReason::RemovedFromWorld:
			End(EDnaStageEnd::LevelChange);
			break;
		default:
			End(EDnaStageEnd::Stopped);
			break;
		}
	}
	const UGameInstance* Instance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	if (UDreamNarrativeStageSubsystem* Stages = Instance ? Instance->GetSubsystem<UDreamNarrativeStageSubsystem>() : nullptr)
	{
		Stages->UnregisterDirector(this);
	}
	Super::EndPlay(EndPlayReason);
}

void UDreamNarrativeStageDirector::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (bPlaying && !bManualAdvance)
	{
		// The world runs at the stage's own hitstop; the stage itself does not.
		Advance(DeltaTime / FMath::Max(AppliedDilation, KINDA_SMALL_NUMBER));
	}
}
