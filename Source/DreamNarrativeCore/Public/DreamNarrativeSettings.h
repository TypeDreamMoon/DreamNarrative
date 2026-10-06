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

	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	static const UDreamNarrativeSettings* Get() { return GetDefault<UDreamNarrativeSettings>(); }
};
