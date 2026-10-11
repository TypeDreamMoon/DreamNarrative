// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "DreamNarrativeSettings.generated.h"

/** Project Settings > Plugins > Dream Narrative. */
UCLASS(Config = DreamNarrative, DefaultConfig, meta = (DisplayName = "Dream Narrative"))
class DREAMNARRATIVECORE_API UDreamNarrativeSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UDreamNarrativeSettings();

	/** Directory under the project root that `.dna` sources are looked for in. */
	UPROPERTY(Config, EditAnywhere, Category = "Sources")
	FString SourceDirectory = TEXT("DNarrative");

	/** Content path the compiler writes script assets under, one per source file. */
	UPROPERTY(Config, EditAnywhere, Category = "Sources")
	FString AssetDirectory = TEXT("/Game/Narrative");

	/**
	 * When set, game code writing a fact the source declared as its own is an error. Off, the write
	 * goes through with a warning -- useful while wiring a project up, unsafe to ship with.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Ledger")
	bool bEnforceFactOwnership = true;

	/** Seed for `random` and `chance`. 0 seeds from the clock. */
	UPROPERTY(Config, EditAnywhere, Category = "Ledger")
	int32 RandomSeed = 0;

	/**
	 * How long a line stays up on a channel that moves on by itself, when the line has no `for` and no
	 * voice says otherwise: this many seconds per character of its text...
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Dialogue", meta = (ClampMin = "0.0"))
	float AutoAdvanceSecondsPerCharacter = 0.15f;

	/** ...and never less than this. */
	UPROPERTY(Config, EditAnywhere, Category = "Dialogue", meta = (ClampMin = "0.0"))
	float AutoAdvanceMinimumSeconds = 1.5f;

	/**
	 * Every line and choice must carry its own `#id` (DNS6006). Turn on once the ids are frozen
	 * (`dnc freeze`) and recording or translation has begun: a line without one would move the ids
	 * of the lines after it, and with them their voices, translations and what saves remember.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Dialogue")
	bool bRequireLineIds = false;

	/** Content paths a stage's bare asset names (`montage AM_咆哮`) are looked for under, in order. A name that starts with `/` is a path already. */
	UPROPERTY(Config, EditAnywhere, Category = "Stage")
	TArray<FString> StageAssetRoots = { TEXT("/Game") };

	/**
	 * Post-process materials for the mask verbs, by verb (`distort`, `exposure`, `streaks`, `comic`,
	 * `black`, `white`). A material's scalar parameter `Strength` follows the entry's envelope. Without
	 * one a verb uses the engine's own post-process settings (fringe, exposure bias, bloom, saturation,
	 * gain, offset).
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Stage")
	TMap<FName, TSoftObjectPtr<class UMaterialInterface>> MaskMaterials;

	/** `ramp dark` without `to`: the world's sun and sky lights go down to this share of their intensity. */
	UPROPERTY(Config, EditAnywhere, Category = "Stage", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float LightRampFloor = 0.2f;

	/** A stage still playing this many seconds past its end (its blends included) is ended by the watchdog. */
	UPROPERTY(Config, EditAnywhere, Category = "Stage", meta = (ClampMin = "0.0"))
	float StageWatchdogSeconds = 5.f;

	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	static const UDreamNarrativeSettings* Get() { return GetDefault<UDreamNarrativeSettings>(); }
};
