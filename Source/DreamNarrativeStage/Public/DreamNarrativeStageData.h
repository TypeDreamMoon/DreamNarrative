// Copyright DreamMoon. All Rights Reserved.
//
// The compiled form of a `stage` block: its shots, its timed entries track by track, and its
// `restore` block. Times are in the stage's own unit -- seconds for an absolute stage, a fraction of
// the line (0 to 1) for one attached to a line -- and so are the spans its entries take (a blend, a
// ramp, a fade, a push); the director scales them when it plays.

#pragma once

#include "CoreMinimal.h"
#include "DreamNarrativeScript.h"
#include "DreamNarrativeStageData.generated.h"

UENUM(BlueprintType)
enum class EDnaStageTrack : uint8
{
	Camera,
	Light,
	Mask,
	Vfx,
	Ui,
	Anim,
	Game,
	Sequence,
	Audio,
};

UENUM(BlueprintType)
enum class EDnaStageVerb : uint8
{
	None,

	// camera
	/** `blend from game 时长`: the stage camera takes over from where the game's view is. */
	BlendFromGame,
	/** `blend back 时长`: the view goes back to the game. */
	BlendBack,
	/** `cut -> 机位`. */
	Cut,
	/** `blend -> 机位 时长`. */
	BlendTo,
	/** `whip -> 机位`: a blend so fast it reads as a whip pan. */
	Whip,
	/** `push 距离cm over 时长`: dolly in along the view. */
	Push,

	// light
	/** `ramp dark 时长 [to 比例]`: the world's sun and sky lights go down. */
	RampDark,
	/** `restore 时长`: they come back. */
	RestoreLight,
	/** `key 角色 on|off`: a key light on an actor. */
	KeyLight,

	// mask
	/** `distort strength n`. */
	Distort,
	/** `exposure peak +n ev`. */
	Exposure,
	Streaks,
	Comic,
	/** `cut black`: black at once. */
	CutBlack,
	/** `fade black 时长`. */
	FadeBlack,
	/** `fade white 时长`. */
	FadeWhite,

	// vfx
	/** `spawn 路径 [at 角色] [persist]`. */
	Spawn,
	/** `stop`: every effect the stage spawned that does not persist. */
	StopVfx,

	// ui
	HideHud,
	ShowHud,
	/** `text "…" [style 名]`. */
	Text,
	/** `clear`: the text goes. */
	ClearText,

	// anim
	/** `montage 名 [actor 角色] [warp 目标 角色]`. */
	Montage,

	// game
	InputLock,
	/** `hitstop 0.2x`: the world slows to a near stop. */
	Hitstop,
	/** `time 0.5x`: the world runs slower or faster. */
	TimeScale,
	/** `damage window`: the game hears that hits count now. */
	DamageWindow,

	// sequence
	/** `play 关卡序列`. */
	PlaySequence,

	// audio (the game plays them; the director says when)
	/** `sound 资源 [at 角色] [volume n]`: once at its time, or held through its range. */
	Sound,
	/** `music 资源 [fade 时长]`: the music changes; when the stage lets go it goes back. */
	Music,
};

/**
 * A named camera position under `shots`: `名 local (x, y, z) [rot (pitch, yaw, roll)] [of 角色] [look 角色] [fov n]`.
 * An anchor no actor answers to is the world itself: its local space is world space.
 */
USTRUCT(BlueprintType)
struct DREAMNARRATIVESTAGE_API FDnaStageShot
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Narrative")
	FName Name;

	/** The actor whose local space Offset is in: `of 角色`, else the look target, else the stage's own actor. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Narrative")
	FName Anchor;

	/** Centimetres, in the anchor's local space. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Narrative")
	FVector Offset = FVector::ZeroVector;

	/** The actor the camera looks at; None to look at the anchor. Ignored when the shot has a rotation. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Narrative")
	FName LookAt;

	/** `rot (pitch, yaw, roll)`: the camera's rotation in the anchor's space, instead of looking at something. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Narrative")
	bool bHasRotation = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Narrative")
	FRotator Rotation = FRotator::ZeroRotator;

	/** Degrees; 0 keeps the game's field of view. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Narrative")
	float Fov = 0.f;

	UPROPERTY(VisibleAnywhere, Category = "Narrative")
	int32 SourceLine = 0;
};

/** One timed entry of a track, or one entry of the `restore` block (whose times are zero). */
USTRUCT(BlueprintType)
struct DREAMNARRATIVESTAGE_API FDnaStageEntry
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Narrative")
	EDnaStageTrack Track = EDnaStageTrack::Camera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Narrative")
	EDnaStageVerb Verb = EDnaStageVerb::None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Narrative")
	float Start = 0.f;

	/** For a range (`1.9s..2.1s`); equal to Start for a point. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Narrative")
	float End = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Narrative")
	bool bRange = false;

	/** How long a blend, a ramp, a fade or a push takes, in the stage's unit; 0 for at once. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Narrative")
	float Over = 0.f;

	/** strength, ev, centimetres, a time multiplier or a light share, by verb. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Narrative")
	float Amount = 0.f;

	/** `on` for a key light; `persist` for a spawned effect; `hard` for a cut meant to be seen. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Narrative")
	bool bFlag = false;

	/** A shot (camera), an actor (key light, montage, spawn `at`). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Narrative")
	FName Target;

	/** A montage's warp target name (`warp 目标 角色`): Target is the actor, WarpTarget the name the montage knows. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Narrative")
	FName WarpTarget;

	/** The actor a warp target follows. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Narrative")
	FName WarpActor;

	/** A content path or a bare asset name (effect, montage, level sequence). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Narrative")
	FString Asset;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Narrative")
	FText Text;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Narrative")
	FName Style;

	UPROPERTY(VisibleAnywhere, Category = "Narrative")
	int32 SourceLine = 0;
};

USTRUCT(BlueprintType)
struct DREAMNARRATIVESTAGE_API FDnaStageData
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Narrative")
	FName Name;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Narrative")
	FName WasName;

	/** `base: line`: times are fractions of the line the stage is attached to. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Narrative")
	bool bLineRelative = false;

	/** Seconds for an absolute stage; 1 (the whole line) for a line-relative one. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Narrative")
	float Duration = 0.f;

	/** `gameplay: 标签`: a gameplay event with this tag plays the stage (through the GAS bridge). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Narrative")
	FName GameplayTag;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Narrative")
	TArray<FDnaStageShot> Shots;

	/** Every track's entries, by start time (source order among equal starts). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Narrative")
	TArray<FDnaStageEntry> Entries;

	/** The `restore` block: what is done, in reverse order, when the stage ends. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Narrative")
	TArray<FDnaStageEntry> Restore;

	UPROPERTY(VisibleAnywhere, Category = "Narrative")
	int32 SourceLine = 0;

	const FDnaStageShot* FindShot(FName Shot) const { return Shots.FindByPredicate([Shot](const FDnaStageShot& Each) { return Each.Name == Shot; }); }

	/**
	 * How long past its end the stage's blends, fades, ramps and pushes still run, in its unit: a
	 * `blend back 0.4s` at the very end runs 0.4 s longer. Its restore entries count from the end.
	 */
	float GetTail() const;
};

/** The stages a script declares. */
UCLASS()
class DREAMNARRATIVESTAGE_API UDnaStageSection : public UDreamNarrativeScriptSection
{
	GENERATED_BODY()

public:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Narrative")
	TArray<FDnaStageData> Stages;

	const FDnaStageData* FindStage(FName Stage) const { return Stages.FindByPredicate([Stage](const FDnaStageData& Each) { return Each.Name == Stage; }); }
};

namespace DreamNarrative
{
	DREAMNARRATIVESTAGE_API const TCHAR* LexToString(EDnaStageTrack Track);
	DREAMNARRATIVESTAGE_API const TCHAR* LexToString(EDnaStageVerb Verb);
}
