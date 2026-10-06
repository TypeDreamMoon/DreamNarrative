// Copyright DreamMoon. All Rights Reserved.

#include "DreamNarrativeStageWatchdog.h"
#include "DreamNarrativeSettings.h"
#include "DreamNarrativeStageDirector.h"
#include "DreamNarrativeStageModule.h"
#include "Engine/World.h"

void UDreamNarrativeStageWatchdog::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	TearDownHandle = FWorldDelegates::OnWorldBeginTearDown.AddUObject(this, &UDreamNarrativeStageWatchdog::HandleWorldTearingDown);
}

void UDreamNarrativeStageWatchdog::Deinitialize()
{
	FWorldDelegates::OnWorldBeginTearDown.Remove(TearDownHandle);
	// Whatever the tear-down did not reach.
	HandleWorldTearingDown(GetWorld());
	Super::Deinitialize();
}

bool UDreamNarrativeStageWatchdog::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UDreamNarrativeStageWatchdog::Watch(UDreamNarrativeStageDirector* Director)
{
	Playing.AddUnique(Director);
}

void UDreamNarrativeStageWatchdog::Unwatch(UDreamNarrativeStageDirector* Director)
{
	Playing.RemoveAll([Director](const TWeakObjectPtr<UDreamNarrativeStageDirector>& Each) { return !Each.IsValid() || Each.Get() == Director; });
}

TArray<UDreamNarrativeStageDirector*> UDreamNarrativeStageWatchdog::GetPlaying() const
{
	TArray<UDreamNarrativeStageDirector*> Out;
	for (const TWeakObjectPtr<UDreamNarrativeStageDirector>& Each : Playing)
	{
		if (UDreamNarrativeStageDirector* Director = Each.Get(); Director && Director->IsPlaying())
		{
			Out.Add(Director);
		}
	}
	return Out;
}

void UDreamNarrativeStageWatchdog::HandleWorldTearingDown(UWorld* World)
{
	if (World != GetWorld())
	{
		return;
	}
	for (UDreamNarrativeStageDirector* Director : GetPlaying())
	{
		UE_LOG(LogDreamNarrativeStage, Log, TEXT("The world is going: stage %s ends"), *Director->GetStageName().ToString());
		Director->Stop(EDnaStageEnd::LevelChange);
	}
	Playing.Reset();
}

void UDreamNarrativeStageWatchdog::Tick(float DeltaTime)
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const float Grace = UDreamNarrativeSettings::Get()->StageWatchdogSeconds;
	for (UDreamNarrativeStageDirector* Director : GetPlaying())
	{
		// The watchdog ticks with the world (not while it is paused): a director that has not moved for
		// Grace seconds of that has stopped ticking.
		TPair<uint64, float>& Still = Stillness.FindOrAdd(Director, { Director->GetAdvanceCount(), 0.f });
		if (Still.Key != Director->GetAdvanceCount())
		{
			Still = { Director->GetAdvanceCount(), 0.f };
		}
		else
		{
			Still.Value += DeltaTime;
		}
		const bool bOverrun = Director->GetSeconds() > Director->GetEndSeconds() + Grace;
		const bool bStalled = !Director->bManualAdvance && Still.Value > Grace;
		if (bOverrun || bStalled)
		{
			UE_LOG(LogDreamNarrativeStage, Warning, TEXT("Stage %s on %s %s; the watchdog ends it"), *Director->GetStageName().ToString(),
				*GetNameSafe(Director->GetOwner()), bOverrun ? TEXT("ran long past its end") : TEXT("stopped moving"));
			Director->Stop(EDnaStageEnd::Watchdog);
		}
	}
	Playing.RemoveAll([](const TWeakObjectPtr<UDreamNarrativeStageDirector>& Each) { return !Each.IsValid() || !Each->IsPlaying(); });
	for (auto It = Stillness.CreateIterator(); It; ++It)
	{
		if (!Playing.Contains(It.Key())) { It.RemoveCurrent(); }
	}
}

TStatId UDreamNarrativeStageWatchdog::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UDreamNarrativeStageWatchdog, STATGROUP_Tickables);
}
