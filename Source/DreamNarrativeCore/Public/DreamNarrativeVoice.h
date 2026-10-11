// Copyright DreamMoon. All Rights Reserved.
//
// How a line finds its voice. A character says it in its declaration: `voice: /Game/VO/老周` looks for
// `<that root>/<the line's id>` (a convention), `voice: table` looks the line's id up in the project's
// voice tables (`*.voice.csv` beside the sources, or the DataTables the settings list). A character
// that says nothing follows the project's default. Either way the compiler finds the asset and writes
// it into the line; the game plays it.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "DreamNarrativeVoice.generated.h"

class USoundBase;

/** Where a character's lines find their voices. */
UENUM(BlueprintType)
enum class EDnaVoiceLookup : uint8
{
	/** No voices: nothing is looked for, nothing is missing. */
	None,
	/** `<voice root>/<line id>`, the id's dots written as underscores (`老周_初见.3` -> `老周_初见_3`). */
	Convention,
	/** The voice tables: line id -> voice. */
	Table,
};

/** A row of a voice DataTable: the row name is the line id. */
USTRUCT(BlueprintType)
struct DREAMNARRATIVECORE_API FDnaVoiceRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Narrative")
	TSoftObjectPtr<USoundBase> Voice;
};
