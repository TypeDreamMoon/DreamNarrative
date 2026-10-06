// Copyright DreamMoon. All Rights Reserved.
//
// What a stage looks like at a moment: a pure function of the stage and the time. The director
// applies the pose of the current time every frame, and a timeline scrubbing to a time applies the
// same pose, which is what keeps a preview and the game showing the same frame. Only the things that
// happen once (a montage, an effect, a level sequence starting) are not in the pose; they are cues,
// fired as the time passes them.

#pragma once

#include "CoreMinimal.h"
#include "DreamNarrativeStageData.h"
#include "DreamNarrativeStagePose.generated.h"

USTRUCT(BlueprintType)
struct DREAMNARRATIVESTAGE_API FDnaStageCameraPose
{
	GENERATED_BODY()

	/** The stage camera is the view, or the view is blending to it or back from it. */
	UPROPERTY(BlueprintReadOnly, Category = "Narrative")
	bool bActive = false;

	/** The blend (in seconds) the view took to the stage camera. */
	UPROPERTY(BlueprintReadOnly, Category = "Narrative")
	float BlendIn = 0.f;

	/** `blend back` has started: the view is going back to the game, over BlendOut seconds. */
	UPROPERTY(BlueprintReadOnly, Category = "Narrative")
	bool bReturning = false;

	UPROPERTY(BlueprintReadOnly, Category = "Narrative")
	float BlendOut = 0.f;

	/** The shots the camera moves between; None is where the game's view was when the stage took over. */
	UPROPERTY(BlueprintReadOnly, Category = "Narrative")
	FName From;

	UPROPERTY(BlueprintReadOnly, Category = "Narrative")
	FName To;

	/** Eased progress from From to To; 1 when the camera sits at To. */
	UPROPERTY(BlueprintReadOnly, Category = "Narrative")
	float Alpha = 1.f;

	/** Centimetres pushed in along the view, at From and at To. */
	UPROPERTY(BlueprintReadOnly, Category = "Narrative")
	float FromPush = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Narrative")
	float Push = 0.f;

	/** How many cuts have happened: the view jumps whenever it changes. */
	UPROPERTY(BlueprintReadOnly, Category = "Narrative")
	int32 Cuts = 0;

	bool operator==(const FDnaStageCameraPose& Other) const;
};

USTRUCT(BlueprintType)
struct DREAMNARRATIVESTAGE_API FDnaStagePose
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Narrative")
	FDnaStageCameraPose Camera;

	/** The share of their intensity the world's sun and sky lights keep; 1 untouched. */
	UPROPERTY(BlueprintReadOnly, Category = "Narrative")
	float LightShare = 1.f;

	/** The actors that have a key light on. */
	UPROPERTY(BlueprintReadOnly, Category = "Narrative")
	TArray<FName> KeyLights;

	// mask: each 0 when off
	UPROPERTY(BlueprintReadOnly, Category = "Narrative")
	float Distort = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Narrative")
	float ExposureEv = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Narrative")
	float Streaks = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Narrative")
	float Comic = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Narrative")
	float Black = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Narrative")
	float White = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Narrative")
	bool bHudHidden = false;

	/** The text on screen; empty for none. */
	UPROPERTY(BlueprintReadOnly, Category = "Narrative")
	FText Text;

	UPROPERTY(BlueprintReadOnly, Category = "Narrative")
	FName TextStyle;

	UPROPERTY(BlueprintReadOnly, Category = "Narrative")
	bool bInputLocked = false;

	/** What the stage multiplies the world's time by; 1 untouched. */
	UPROPERTY(BlueprintReadOnly, Category = "Narrative")
	float TimeDilation = 1.f;

	UPROPERTY(BlueprintReadOnly, Category = "Narrative")
	bool bDamageWindow = false;

	bool HasMask() const { return Distort > 0.f || ExposureEv != 0.f || Streaks > 0.f || Comic > 0.f || Black > 0.f || White > 0.f; }

	/** Equal to a tolerance on every float: what "the same frame" means. */
	bool NearlyEquals(const FDnaStagePose& Other, float Tolerance = 1e-4f) const;

	/** One line per field that is not at rest, for logs and test failures. */
	FString Describe() const;
};

namespace DreamNarrative
{
	/**
	 * The pose of Stage at Time, both in the stage's unit (seconds, or a fraction of the line).
	 * UnitSeconds is how many seconds one unit is: 1 for an absolute stage, the line's length for a
	 * line-relative one (it turns the few fixed-length moves, a whip, into the stage's unit).
	 */
	DREAMNARRATIVESTAGE_API FDnaStagePose SampleDnaStage(const FDnaStageData& Stage, float Time, float UnitSeconds = 1.f);

	/**
	 * How strongly a mask entry is on at Time: 0 off, 1 at full. A range rises over its fade, holds and
	 * falls; a point rises over its fade and holds to the end of the stage. The validator asks the same
	 * function whether a cut falls at a mask's peak.
	 */
	DREAMNARRATIVESTAGE_API float GetDnaMaskEnvelope(const FDnaStageEntry& Entry, float Time);

	/** True for the entries that happen once rather than holding a state: montage, spawn, stop, play. */
	DREAMNARRATIVESTAGE_API bool IsDnaStageCue(const FDnaStageEntry& Entry);

	/** Seconds a whip takes. */
	constexpr float DnaWhipSeconds = 0.12f;
}
