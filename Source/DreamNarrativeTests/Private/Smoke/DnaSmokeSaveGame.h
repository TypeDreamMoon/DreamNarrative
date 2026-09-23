// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamNarrativeLedger.h"
#include "GameFramework/SaveGame.h"
#include "DnaSmokeSaveGame.generated.h"

/** What a game's save holds for the narrative: the ledger snapshot, as an ordinary property. */
UCLASS()
class UDnaSmokeSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FDnaLedgerSnapshot Narrative;
};
