// Copyright DreamMoon. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "DreamNarrativeStageDirector.h"
#include "DnaStageActor.h"
#include "GameFramework/Actor.h"
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

	/** What the stage said of its sounds and music, in order: `sound SFX_Hit 0.8`, `stop`, `music BGM_A 1.0`, `music game 0.5`. */
	TArray<FString> Heard;

	UFUNCTION()
	void HearSound(const FDnaStageSound& Sound)
	{
		Heard.Add(FString::Printf(TEXT("sound %s %.1f%s"), *Sound.Sound.GetAssetName(), Sound.Volume, Sound.bHeld ? TEXT(" held") : TEXT("")));
	}

	UFUNCTION()
	void HearSoundStopped(int32 Handle) { Heard.Add(TEXT("stop")); }

	UFUNCTION()
	void HearMusic(TSoftObjectPtr<USoundBase> Music, float FadeSeconds)
	{
		Heard.Add(FString::Printf(TEXT("music %s %.1f"), Music.IsNull() ? TEXT("game") : *Music.GetAssetName(), FadeSeconds));
	}

	FString Take()
	{
		const FString Out = FString::Join(Heard, TEXT(", "));
		Heard.Reset();
		return Out;
	}
};

/** An actor that hears what the blocking does to it: `move (500,0,0) run`, `arrived`, `look 乙`, `look off`. */
UCLASS()
class ADnaStageBlockingActor : public AActor, public IDnaStageActor
{
	GENERATED_BODY()
public:
	ADnaStageBlockingActor() { RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root")); }

	TArray<FString> Heard;

	virtual void StageMoveStarted_Implementation(FVector Destination, float Seconds, bool bRun) override
	{
		Heard.Add(FString::Printf(TEXT("move (%.0f,%.0f,%.0f) %.1f%s"), Destination.X, Destination.Y, Destination.Z, Seconds, bRun ? TEXT(" run") : TEXT("")));
	}
	virtual void StageMoveFinished_Implementation() override { Heard.Add(TEXT("arrived")); }
	virtual void StageLookAt_Implementation(AActor* Target, FVector Location, bool bLooking) override
	{
		Heard.Add(bLooking ? FString::Printf(TEXT("look %s"), Target ? *Target->Tags[0].ToString() : TEXT("place")) : FString(TEXT("look off")));
	}

	FString Take()
	{
		const FString Out = FString::Join(Heard, TEXT(", "));
		Heard.Reset();
		return Out;
	}
};
