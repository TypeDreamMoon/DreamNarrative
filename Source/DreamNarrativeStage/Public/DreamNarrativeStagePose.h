// Copyright DreamMoon. All Rights Reserved.
//
// What a stage looks like at a moment: a pure function of the stage and the time. The director
// applies the pose of the current time every frame, and a timeline scrubbing to a time applies the
// same pose, which is what keeps a preview and the game showing the same frame. Only the things that
// happen once (a montage, an effect, a level sequence starting) are not in the pose; they are cues,
// fired as the time passes them.

#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraTypes.h"
#include "DreamNarrativeStageData.h"
#include "DreamNarrativeStagePose.generated.h"

class AActor;

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

	/**
	 * Where the stage camera is for a camera pose: its shots placed by the actors Resolve finds for
	 * their names (None is the stage's own actor), GameView standing in for where the game's view was
	 * when the stage took over. The director and a timeline's preview both ask this.
	 */
	DREAMNARRATIVESTAGE_API FMinimalViewInfo EvaluateDnaStageView(const FDnaStageData& Stage, const FDnaStageCameraPose& Camera,
		const FMinimalViewInfo& GameView, TFunctionRef<AActor*(FName)> Resolve);

	/** True for the entries that happen once rather than holding a state: montage, spawn, stop, play. */
	DREAMNARRATIVESTAGE_API bool IsDnaStageCue(const FDnaStageEntry& Entry);

	/** True for the blocking entries the stage carries an actor through itself: turns, and moves not left to the navigation mesh. */
	DREAMNARRATIVESTAGE_API bool IsDnaStageLeg(const FDnaStageEntry& Entry);

	/**
	 * Where a blocking entry's goal is: a mark of the stage -- its place, facing its own rotation, else toward
	 * the actor it looks at, else as its anchor does -- or else the place of the actor Resolve finds for the
	 * name, which is OutActor. Where tells where an actor stands.
	 */
	DREAMNARRATIVESTAGE_API bool FindDnaStageGoal(const FDnaStageData& Stage, const FDnaStageEntry& Entry, TFunctionRef<AActor*(FName)> Resolve,
		TFunctionRef<FTransform(const AActor*)> Where, FTransform& OutGoal, AActor*& OutActor);

	/**
	 * Where a move or a turn that starts at From ends: a move at its goal, facing as a mark says (an actor as
	 * the goal is walked up to on the walker's own level, facing the way of the walk); a turn where it stands,
	 * toward the goal.
	 */
	DREAMNARRATIVESTAGE_API FTransform GetDnaStageLegEnd(const FDnaStageEntry& Entry, const FTransform& From, const FTransform& Goal, const AActor* GoalActor);

	/** How long a move or a turn takes, in the stage's unit: a move its range (none: at once), a turn its range or its `over`. */
	DREAMNARRATIVESTAGE_API float GetDnaStageLegLength(const FDnaStageEntry& Entry);

	/** How far along a move or a turn is at Time, eased in and out: 0 before it starts, 1 once it is done. */
	DREAMNARRATIVESTAGE_API float GetDnaStageLegProgress(const FDnaStageEntry& Entry, float Time);

	/**
	 * Where the blocking has put each actor it moved or turned by Time (the stage's unit). Origin is where an
	 * actor stood before the stage moved it (for one it never moves, where it stands). Each move starts where
	 * the ones before it left the actor and heads for where its goal was then. Moves left to the navigation
	 * mesh are the game's and are not here, nor is anyone or anywhere not found. The director carries actors
	 * through the same legs; a timeline's preview places the editor's actors with this.
	 */
	DREAMNARRATIVESTAGE_API TMap<AActor*, FTransform> SampleDnaStageBlocking(const FDnaStageData& Stage, float Time,
		TFunctionRef<AActor*(FName)> Resolve, TFunctionRef<FTransform(const AActor*)> Origin);

	/** Seconds a whip takes. */
	constexpr float DnaWhipSeconds = 0.12f;
}
