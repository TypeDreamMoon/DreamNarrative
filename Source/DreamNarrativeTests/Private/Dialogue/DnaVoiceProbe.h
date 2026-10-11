// Copyright DreamMoon. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "DreamNarrativeDialogueRunner.h"
#include "DnaVoiceProbe.generated.h"

/** Writes down every voice event the story raises: `Requested 甲_一`, ... */
UCLASS()
class UDnaVoiceProbe : public UObject
{
	GENERATED_BODY()
public:
	TArray<FString> Events;
	TArray<TWeakObjectPtr<AActor>> Speakers;

	UFUNCTION()
	void Hear(UDreamNarrativeDialogueRunner* Runner, EDnaVoiceEvent Event, const FDnaLineView& Line, AActor* Speaker)
	{
		static const TCHAR* Names[] = { TEXT("Requested"), TEXT("Stopped"), TEXT("Finished") };
		Events.Add(FString::Printf(TEXT("%s %s"), Names[static_cast<int32>(Event)], *Line.Id.ToString()));
		Speakers.Add(Speaker);
	}

	/** The events since the last call, one string. */
	FString Take()
	{
		const FString Out = FString::Join(Events, TEXT(", "));
		Events.Reset();
		return Out;
	}
};
