// Copyright DreamMoon. All Rights Reserved.
//
// What the stages playing in one world at once share: the world's time, the sun and the sky, a
// player's HUD and a player's view. A director that changes one of them holds it here. The arbiter
// composes what every holder asks for over how the thing was before the first of them, and puts it
// back when the last lets go -- whatever order they end in.

#pragma once

#include "CoreMinimal.h"
#include "DreamNarrativeStageDirector.h"
#include "Subsystems/WorldSubsystem.h"
#include "DreamNarrativeStageArbiter.generated.h"

class AHUD;
class APlayerController;
class ULightComponentBase;

UCLASS()
class DREAMNARRATIVESTAGE_API UDreamNarrativeStageArbiter : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Deinitialize() override;

	/** The world's time: the holders' scales multiply over the scale it had before the first. */
	void SetTimeDilation(UDreamNarrativeStageDirector* Director, float Scale);
	void ReleaseTimeDilation(UDreamNarrativeStageDirector* Director);

	/** The sun and the sky: the holders' shares multiply over the intensities they had before the first. */
	void SetLightShare(UDreamNarrativeStageDirector* Director, float Share);
	void ReleaseLightShare(UDreamNarrativeStageDirector* Director);

	/** A HUD stays hidden while any director hides it, and is shown again (if it was) when the last lets go. */
	void HideHud(UDreamNarrativeStageDirector* Director, AHUD* Hud);
	void ReleaseHud(UDreamNarrativeStageDirector* Director);

	/**
	 * A player's view: the director that took it last shows it. When that one lets go the view goes to
	 * the camera of the director beneath, or back to what the game showed before the first.
	 */
	void TakeView(UDreamNarrativeStageDirector* Director, APlayerController* Controller, AActor* Camera);
	/** Whether Director's camera is the one its player sees. */
	bool IsShowingView(const UDreamNarrativeStageDirector* Director) const;
	/** What the view goes to when Director lets go; null when it holds no view. */
	AActor* GetViewBeneath(const UDreamNarrativeStageDirector* Director) const;
	/** Director lets go of the view; bSetView puts the view beneath on screen if Director was showing. */
	void ReleaseView(UDreamNarrativeStageDirector* Director, bool bSetView);

	/** Drops whatever directors gone without letting go still hold, and puts the world right for it. */
	void PurgeStale();

	/** A HUD went (false) or came back (true), all stages counted: for a HUD that is not an AHUD. */
	UPROPERTY(BlueprintAssignable, Category = "Narrative|Stage")
	FDnaStageSwitchSignature OnHudVisibility;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	using FDirectorRef = TWeakObjectPtr<UDreamNarrativeStageDirector>;

	void ApplyTime();
	void ApplyLights();

	TMap<FDirectorRef, float> TimeScales;
	float OriginalTime = 1.f;
	bool bTimeHeld = false;

	TMap<FDirectorRef, float> LightShares;
	TArray<TPair<TWeakObjectPtr<ULightComponentBase>, float>> LightOriginals;
	bool bLightsHeld = false;

	struct FHudHold
	{
		TWeakObjectPtr<AHUD> Hud;
		bool bWasShown = true;
		TArray<FDirectorRef> Holders;
	};
	TArray<FHudHold> Huds;

	struct FViewHold
	{
		FDirectorRef Director;
		TWeakObjectPtr<AActor> Camera;
	};
	struct FViewStack
	{
		TWeakObjectPtr<APlayerController> Controller;
		/** What the game showed before the first stage took the view. */
		TWeakObjectPtr<AActor> Original;
		/** Oldest first; the last one shows. */
		TArray<FViewHold> Holds;
	};
	TArray<FViewStack> Views;
};
