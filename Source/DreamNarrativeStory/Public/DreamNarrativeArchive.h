// Copyright DreamMoon. All Rights Reserved.
//
// Compiled archives: the texts a game files away as the story reveals them -- weapon stories,
// documents, a bestiary. Each entry unlocks the first time its condition holds and stays unlocked;
// which are unlocked is a fact (ArchiveFact), a profile fact when the archive is one, so an archive
// written `profile` carries over to the next playthrough.

#pragma once

#include "CoreMinimal.h"
#include "DreamNarrativeQuest.h"
#include "DreamNarrativeScript.h"
#include "DreamNarrativeArchive.generated.h"

USTRUCT()
struct DREAMNARRATIVESTORY_API FDnaArchiveEntryData
{
	GENERATED_BODY()

	/** Unlocks the first time this holds. INDEX_NONE: unlocked from the start. */
	UPROPERTY(VisibleAnywhere, Category = "Narrative")
	int32 Condition = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, Category = "Narrative")
	FDnaQuestText Text;

	UPROPERTY(VisibleAnywhere, Category = "Narrative")
	int32 SourceLine = 0;
};

USTRUCT()
struct DREAMNARRATIVESTORY_API FDnaArchiveData
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Narrative")
	FName Name;

	UPROPERTY(VisibleAnywhere, Category = "Narrative")
	FText Title;

	/** `profile`: what is unlocked outlives the save slot and the playthrough. */
	UPROPERTY(VisibleAnywhere, Category = "Narrative")
	bool bProfile = false;

	UPROPERTY(VisibleAnywhere, Category = "Narrative")
	TArray<FDnaArchiveEntryData> Entries;

	UPROPERTY(VisibleAnywhere, Category = "Narrative")
	int32 SourceLine = 0;
};

/** The archives of one script. */
UCLASS()
class DREAMNARRATIVESTORY_API UDnaArchiveSection : public UDreamNarrativeScriptSection
{
	GENERATED_BODY()

public:
	UPROPERTY(VisibleAnywhere, Category = "Narrative")
	TArray<FDnaArchiveData> Archives;
};

/** What a player can read of an archive now. */
USTRUCT(BlueprintType)
struct DREAMNARRATIVESTORY_API FDnaArchiveView
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Narrative")
	FName Name;

	UPROPERTY(BlueprintReadOnly, Category = "Narrative")
	FText Title;

	/** The unlocked entries, in the order they are written, interpolated now. */
	UPROPERTY(BlueprintReadOnly, Category = "Narrative")
	TArray<FText> Entries;

	/** Which entry each of Entries is: its index among all the archive's entries. */
	UPROPERTY(BlueprintReadOnly, Category = "Narrative")
	TArray<int32> EntryIndices;

	/** How many entries the archive has, unlocked or not. */
	UPROPERTY(BlueprintReadOnly, Category = "Narrative")
	int32 Total = 0;
};
