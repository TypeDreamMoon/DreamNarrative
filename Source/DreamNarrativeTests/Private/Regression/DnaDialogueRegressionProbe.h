// Copyright DreamMoon. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "DreamNarrativeDialogueRunner.h"
#include "DnaDialogueRegressionProbe.generated.h"

UCLASS()
class UDnaDialogueRegressionProbe : public UObject
{
	GENERATED_BODY()
public:
	int32 Ends = 0;
	UFUNCTION()
	void AbortWhenChosen(UDreamNarrativeDialogueRunner* Runner, const FDnaChoiceView& Choice) { Runner->Abort(); }
	UFUNCTION()
	void PauseWhenChosen(UDreamNarrativeDialogueRunner* Runner, const FDnaChoiceView& Choice) { Runner->Pause(); }
	UFUNCTION()
	void CountEnd(UDreamNarrativeDialogueRunner* Runner) { ++Ends; }
};
