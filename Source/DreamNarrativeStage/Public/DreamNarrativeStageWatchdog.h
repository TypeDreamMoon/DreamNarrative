// Copyright DreamMoon. All Rights Reserved.
//
// Keeps an eye on the stages playing in a world. When the world starts going away -- a level change,
// the end of play -- it ends them while the controller, the camera and the HUD they touched are all
// still there to be put back. And it ends a stage that has stopped moving (its director no longer
// ticks while the world does) or that runs long past its end.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DreamNarrativeStageWatchdog.generated.h"

class UDreamNarrativeStageDirector;

UCLASS()
class DREAMNARRATIVESTAGE_API UDreamNarrativeStageWatchdog : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** A director that starts playing reports in, and out again when it stops. */
	void Watch(UDreamNarrativeStageDirector* Director);
	void Unwatch(UDreamNarrativeStageDirector* Director);

	/** Composes each playing director's time scale over the world's original scale. */
	void SetTimeDilation(UDreamNarrativeStageDirector* Director, float Scale);
	void ReleaseTimeDilation(UDreamNarrativeStageDirector* Director);

	/** The directors playing in this world now. */
	TArray<UDreamNarrativeStageDirector*> GetPlaying() const;

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual ETickableTickType GetTickableTickType() const override { return IsTemplate() ? ETickableTickType::Never : ETickableTickType::Conditional; }
	virtual bool IsTickable() const override { return Playing.Num() > 0; }

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	void HandleWorldTearingDown(UWorld* World);
	void UpdateTimeDilation();
	TMap<TWeakObjectPtr<UDreamNarrativeStageDirector>, float> TimeScales;
	float OriginalTimeDilation = 1.f;
	bool bHasOriginalTimeDilation = false;

	TArray<TWeakObjectPtr<UDreamNarrativeStageDirector>> Playing;
	/** Per director: its advance count when last seen to move, and the world's seconds it has stood still since. */
	TMap<TWeakObjectPtr<UDreamNarrativeStageDirector>, TPair<uint64, float>> Stillness;
	FDelegateHandle TearDownHandle;
};
