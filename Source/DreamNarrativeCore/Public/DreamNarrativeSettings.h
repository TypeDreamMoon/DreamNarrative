// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "DreamNarrativeVoice.h"
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

	/** Where lines find their voices when the speaker's declaration does not say (`voice: 路径` or `voice: table`). */
	UPROPERTY(Config, EditAnywhere, Category = "Voice")
	EDnaVoiceLookup DefaultVoiceLookup = EDnaVoiceLookup::None;

	/** The convention's root for a speaker with none of its own: `<root>/<角色>/<line id>`; narration `<root>/<line id>`. */
	UPROPERTY(Config, EditAnywhere, Category = "Voice")
	FString DefaultVoiceRoot = TEXT("/Game/VO");

	/** Voice DataTables (rows of FDnaVoiceRow, row name = line id), read along with the `*.voice.csv` beside the sources. */
	UPROPERTY(Config, EditAnywhere, Category = "Voice", meta = (AllowedClasses = "/Script/Engine.DataTable"))
	TArray<FSoftObjectPath> VoiceTables;

	/** A voiced line on a channel that moves on by itself stays up this much longer than its voice. */
	UPROPERTY(Config, EditAnywhere, Category = "Voice", meta = (ClampMin = "0.0"))
	float VoiceTailSeconds = 0.3f;

	/**
	 * A voiced line on a channel that moves on by itself waits for the game's NotifyVoiceFinished rather
	 * than the voice's length -- for a game whose audio knows better (streaming, localised voices of other
	 * lengths). It moves on anyway VoiceWaitGraceSeconds past the length, should the game never say.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Voice")
	bool bWaitForVoiceFinished = false;

	UPROPERTY(Config, EditAnywhere, Category = "Voice", meta = (ClampMin = "0.0", EditCondition = "bWaitForVoiceFinished"))
	float VoiceWaitGraceSeconds = 5.f;

	/** A line whose voice is looked for and not found is an error (DNS6003) rather than a warning: for release builds. */
	UPROPERTY(Config, EditAnywhere, Category = "Voice")
	bool bMissingVoiceIsError = false;

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
