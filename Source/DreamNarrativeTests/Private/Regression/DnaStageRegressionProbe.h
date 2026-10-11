// Copyright DreamMoon. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "DreamNarrativeStageDirector.h"
#include "DnaStageRegressionProbe.generated.h"

UCLASS()
class UDnaStageRegressionProbe : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY()
	TObjectPtr<UDreamNarrativeStageDirector> Director;

	UFUNCTION()
	void StopAtStart(FName Stage) { if (Director) { Director->Stop(); } }

	UFUNCTION()
	void StopWhenHidden(bool bVisible) { if (!bVisible && Director) { Director->Stop(); } }
};
