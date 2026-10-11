// Copyright DreamMoon. All Rights Reserved.
//
// What an actor hears of a stage's blocking. The stage moves and turns the actor itself; an actor that
// implements this (in C++ or in Blueprint) also hears about it, to walk its legs, turn its head, look.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "DnaStageActor.generated.h"

UINTERFACE(BlueprintType, MinimalAPI)
class UDnaStageActor : public UInterface
{
	GENERATED_BODY()
};

class DREAMNARRATIVESTAGE_API IDnaStageActor
{
	GENERATED_BODY()

public:
	/** A stage moves the actor to Destination over Seconds (0: at once), at a walk or a run. */
	UFUNCTION(BlueprintNativeEvent, Category = "Narrative|Stage")
	void StageMoveStarted(FVector Destination, float Seconds, bool bRun);

	/** The move is over: arrived, or the stage cut it short. */
	UFUNCTION(BlueprintNativeEvent, Category = "Narrative|Stage")
	void StageMoveFinished();

	/** Look at Target (or, with none, at Location) while bLooking; false: look where you like again. */
	UFUNCTION(BlueprintNativeEvent, Category = "Narrative|Stage")
	void StageLookAt(AActor* Target, FVector Location, bool bLooking);
};
