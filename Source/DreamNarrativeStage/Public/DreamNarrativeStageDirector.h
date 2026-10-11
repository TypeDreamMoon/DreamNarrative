// Copyright DreamMoon. All Rights Reserved.
//
// Plays one stage at a time on an actor: usually the player controller, or the pawn an ability plays
// its finisher on. Every frame it samples the stage at the current time and makes the world look like
// that pose -- the camera, the sun, the mask over the screen, the HUD, the input, the world's time --
// and fires the cues the time has passed (montages, effects, level sequences).
//
// Whatever it changes it changes as a transaction: the first time it touches a thing it notes how the
// thing was, and when the stage ends -- run out, stopped, replaced, the pawn dead, the level gone --
// it puts every one of them back, the last touched first. A stage that runs to its end gets its
// authored `restore` block (a blend back, the HUD shown again) on the way, and the transaction then
// takes back whatever is left at once. A stage cut short skips straight to that.

#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraTypes.h"
#include "Components/ActorComponent.h"
#include "DreamNarrativeStageData.h"
#include "DreamNarrativeStageHost.h"
#include "DreamNarrativeStagePose.h"
#include "DreamNarrativeStageDirector.generated.h"

class UDreamNarrativeLedger;
class UDreamNarrativeStageArbiter;
class ACameraActor;
class AHUD;
class ALevelSequenceActor;
class APawn;
class APlayerController;
class AWorldSettings;
class UAnimInstance;
class UAnimMontage;
class ULightComponent;
class ULightComponentBase;
class UMaterialInstanceDynamic;
class UNiagaraComponent;
class UPostProcessComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDnaStageStartedSignature, FName, Stage);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDnaStageEndedSignature, FName, Stage, EDnaStageEnd, Reason);
/** The text a `ui text` entry puts up; empty when it goes. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDnaStageTextSignature, const FText&, Text, FName, Style);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDnaStageSwitchSignature, bool, bOn);
/** A montage's `warp 目标 角色`: what the game hands its motion warping. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FDnaStageWarpSignature, AActor*, Actor, FName, WarpTarget, AActor*, TargetActor);
DECLARE_MULTICAST_DELEGATE_TwoParams(FDnaStagePlayEndedNative, uint32 /*Play*/, EDnaStageEnd /*Reason*/);

UCLASS(ClassGroup = (Narrative), meta = (BlueprintSpawnableComponent))
class DREAMNARRATIVESTAGE_API UDreamNarrativeStageDirector : public UActorComponent
{
	GENERATED_BODY()

public:
	UDreamNarrativeStageDirector();

	/**
	 * Plays a stage from its start. LineSeconds stretches a line-relative stage over a line that long.
	 * A stage already playing here ends first (Replaced). Returns the play's number, which
	 * OnPlayEnded reports; 0 when invalid, unwinding, or superseded by a newer Play in an end callback.
	 */
	uint32 Play(const FDnaStageData& Stage, float LineSeconds = 0.f);

	/**
	 * The ledger the next Play tells about the stage (the channels that pause for stages listen there).
	 * Unsaid, it is the game instance's; a host with a ledger of its own -- a tool, a test -- says so first.
	 */
	void ReportActivityTo(UDreamNarrativeLedger* Ledger);

	/** Plays a stage a registered script declares, by name. */
	UFUNCTION(BlueprintCallable, Category = "Narrative|Stage")
	bool PlayStage(FName Stage);

	/** Ends the stage now, putting back everything it changed at once. */
	UFUNCTION(BlueprintCallable, Category = "Narrative|Stage")
	void Stop(EDnaStageEnd Reason = EDnaStageEnd::Stopped);

	UFUNCTION(BlueprintPure, Category = "Narrative|Stage")
	bool IsPlaying() const { return bPlaying; }

	UFUNCTION(BlueprintPure, Category = "Narrative|Stage")
	FName GetStageName() const { return bPlaying ? Stage.Name : NAME_None; }

	/** The number Play gave the stage playing now; 0 when none plays. */
	uint32 GetPlayNumber() const { return bPlaying ? PlayNumber : 0; }

	/** Seconds since the stage started. */
	UFUNCTION(BlueprintPure, Category = "Narrative|Stage")
	float GetSeconds() const { return Seconds; }

	/** Seconds from start to letting go: the stage's length, its blends past the end included. */
	float GetEndSeconds() const;

	/** The pose the world was last made to look like. */
	const FDnaStagePose& GetPose() const { return Pose; }

	/** Moves the stage on by Seconds of stage time: what each tick does with the frame's time. */
	void Advance(float DeltaSeconds);

	/** The actor a stage's role name stands for. Unbound names are looked for as actor tags in the world. */
	UFUNCTION(BlueprintCallable, Category = "Narrative|Stage")
	void BindActor(FName Role, AActor* Actor);

	UFUNCTION(BlueprintCallable, Category = "Narrative|Stage")
	void UnbindActor(FName Role);

	/** The actor a role names; None is the stage's own actor (the pawn, for a director on a controller). Null when nothing answers. */
	AActor* ResolveActor(FName Role) const;

	/** The director on Actor, or a new one added to it. */
	UFUNCTION(BlueprintCallable, Category = "Narrative|Stage")
	static UDreamNarrativeStageDirector* FindOrAddDirector(AActor* Actor);

	/** Ends every stage playing in the world -- what a game calls when the player dies. */
	UFUNCTION(BlueprintCallable, Category = "Narrative|Stage", meta = (WorldContext = "WorldContext"))
	static void InterruptAll(const UObject* WorldContext, EDnaStageEnd Reason);

	/** The things the stage has touched and will put back, oldest first: `camera`, `light`, ... For tests and the debugger. */
	TArray<FString> DescribeTouched() const;

	/** Tests and tools call Advance themselves; the director then does not tick on its own. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Narrative|Stage")
	bool bManualAdvance = false;

	/** The story's stages play on this director; with several, the one that began playing last. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Narrative|Stage")
	bool bPlaysStoryStages = true;

	UPROPERTY(BlueprintAssignable, Category = "Narrative|Stage")
	FDnaStageStartedSignature OnStageStarted;

	UPROPERTY(BlueprintAssignable, Category = "Narrative|Stage")
	FDnaStageEndedSignature OnStageEnded;

	UPROPERTY(BlueprintAssignable, Category = "Narrative|Stage")
	FDnaStageTextSignature OnText;

	/** The HUD was hidden (false) or shown again (true); for a HUD that is not an AHUD. */
	UPROPERTY(BlueprintAssignable, Category = "Narrative|Stage")
	FDnaStageSwitchSignature OnHudVisibility;

	UPROPERTY(BlueprintAssignable, Category = "Narrative|Stage")
	FDnaStageSwitchSignature OnDamageWindow;

	UPROPERTY(BlueprintAssignable, Category = "Narrative|Stage")
	FDnaStageWarpSignature OnWarpTarget;

	/** Every end, with the play's number: what the stage subsystem waits on. */
	FDnaStagePlayEndedNative OnPlayEnded;

	/** How many times the director has moved a stage on; the watchdog tells a stalled director by it. */
	uint64 GetAdvanceCount() const { return AdvanceCount; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	/** The parts of the world a stage touches, each noted with how it was before. */
	enum class ESlot : uint8 { Camera, Light, KeyLight, Mask, Vfx, Hud, Text, Anim, Input, Time, Damage, Sequence };

	void Touch(ESlot Slot);
	void ApplyPose(const FDnaStagePose& NewPose);
	void ApplyCamera(const FDnaStageCameraPose& Camera);
	void ApplyLight(const FDnaStagePose& NewPose);
	void ApplyMask(const FDnaStagePose& NewPose);
	void ApplyGame(const FDnaStagePose& NewPose);
	void FireCuesUpTo(float Unit);
	void FireCue(int32 EntryIndex);
	/** Puts back everything touched, newest first; what is left of a graceful ending goes at once too. */
	void Unwind();
	void UnwindSlot(ESlot Slot);
	void End(EDnaStageEnd Reason);

	APlayerController* GetController() const;
	APawn* GetPawn() const;
	/** What the world's stages share goes through it; null in a world without one. */
	UDreamNarrativeStageArbiter* GetArbiter() const;
	/** Where the view goes back to: the stage beneath this one, or the game. */
	AActor* GetViewBack(APlayerController& Controller) const;
	template <typename T> T* ResolveAsset(const FString& NameOrPath) const;
	/** Logs a cue whose asset was not found; true when it was. */
	bool WarnIfMissing(const FDnaStageEntry& Entry, const UObject* Asset, const TCHAR* What) const;

	UPROPERTY(Transient)
	FDnaStageData Stage;

	FDnaStagePose Pose;
	bool bPlaying = false;
	bool bEnding = false;
	uint32 PlayNumber = 0;
	uint32 PlayRequestSerial = 0;
	TWeakObjectPtr<UDreamNarrativeLedger> ActivityLedger;
	TWeakObjectPtr<UDreamNarrativeLedger> NextActivityLedger;
	/** Seconds since start, and the seconds one unit of the stage is (the line's length for a line-relative stage). */
	float Seconds = 0.f;
	float UnitSeconds = 1.f;
	int32 NextCue = 0;
	uint64 AdvanceCount = 0;

	TMap<FName, TWeakObjectPtr<AActor>> Roles;
	TArray<ESlot> Touched;

	// camera
	TWeakObjectPtr<APlayerController> CameraController;
	TWeakObjectPtr<ACameraActor> StageCamera;
	FMinimalViewInfo GameView;
	bool bViewTaken = false;
	bool bViewReturning = false;

	// light
	TMap<FName, TWeakObjectPtr<ULightComponent>> KeyLights;
	float AppliedLightShare = 1.f;

	// mask
	TWeakObjectPtr<UPostProcessComponent> Mask;
	TMap<FName, TWeakObjectPtr<UMaterialInstanceDynamic>> MaskMaterials;

	// cues
	TArray<TWeakObjectPtr<UNiagaraComponent>> Effects;
	/** Effects spawned by a range entry, by entry, to stop at its end. */
	TMap<int32, TWeakObjectPtr<UNiagaraComponent>> RangeEffects;
	TArray<TPair<TWeakObjectPtr<UAnimInstance>, TWeakObjectPtr<UAnimMontage>>> Montages;
	TArray<TWeakObjectPtr<ALevelSequenceActor>> Sequences;

	// ui
	TWeakObjectPtr<AHUD> Hud;
	bool bHudHidden = false;
	FText ShownText;

	// game
	TWeakObjectPtr<APlayerController> InputController;
	bool bInputLocked = false;
	float AppliedDilation = 1.f;
	bool bDamageWindow = false;

	/** The pawn the stage started with; when it goes, the player died. */
	TWeakObjectPtr<APawn> StartPawn;
	bool bHadPawn = false;
};
