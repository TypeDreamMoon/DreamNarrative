// Copyright DreamMoon. All Rights Reserved.
//
// The one file of the runtime modules that may touch a timer manager or read world time: every other
// piece of narrative code waits through IDnaClock. The static checks enforce that.

#include "DreamNarrativeClock.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "TimerManager.h"

// ---------------------------------------------------------------- world clock

FDnaWorldClock::FDnaWorldClock(UGameInstance* InGameInstance)
	: GameInstance(InGameInstance)
{
}

FDnaWorldClock::~FDnaWorldClock()
{
	if (UGameInstance* Instance = GameInstance.Get())
	{
		for (TPair<uint64, FTimerHandle>& Pair : Timers)
		{
			Instance->GetTimerManager().ClearTimer(Pair.Value);
		}
	}
	Timers.Reset();
}

double FDnaWorldClock::GetSeconds() const
{
	const UGameInstance* Instance = GameInstance.Get();
	const UWorld* World = Instance ? Instance->GetWorld() : nullptr;
	return World ? World->GetTimeSeconds() : 0.0;
}

FDnaClockHandle FDnaWorldClock::Schedule(double Seconds, FSimpleDelegate Callback)
{
	FDnaClockHandle Handle;
	UGameInstance* Instance = GameInstance.Get();
	if (!Instance)
	{
		return Handle;
	}
	Handle.Id = NextId++;
	const uint64 Id = Handle.Id;
	FTimerDelegate Fire = FTimerDelegate::CreateLambda([this, Id, Callback = MoveTemp(Callback)]()
	{
		// Forget the timer before calling out: the callback may schedule the next one.
		Timers.Remove(Id);
		Callback.ExecuteIfBound();
	});
	FTimerHandle& Timer = Timers.Add(Id);
	// A rate of zero or less would clear the timer instead of setting it; the next tick is the
	// shortest wait a timer manager has.
	if (Seconds > 0.0)
	{
		Instance->GetTimerManager().SetTimer(Timer, MoveTemp(Fire), static_cast<float>(Seconds), false);
	}
	else
	{
		Timer = Instance->GetTimerManager().SetTimerForNextTick(MoveTemp(Fire));
	}
	return Handle;
}

void FDnaWorldClock::Cancel(FDnaClockHandle& Handle)
{
	if (FTimerHandle* Timer = Timers.Find(Handle.Id))
	{
		if (UGameInstance* Instance = GameInstance.Get())
		{
			Instance->GetTimerManager().ClearTimer(*Timer);
		}
		Timers.Remove(Handle.Id);
	}
	Handle.Invalidate();
}

bool FDnaWorldClock::IsPending(const FDnaClockHandle& Handle) const
{
	return Handle.IsValid() && Timers.Contains(Handle.Id);
}

double FDnaWorldClock::GetRemaining(const FDnaClockHandle& Handle) const
{
	const FTimerHandle* Timer = Handle.IsValid() ? Timers.Find(Handle.Id) : nullptr;
	const UGameInstance* Instance = GameInstance.Get();
	if (!Timer || !Instance)
	{
		return -1.0;
	}
	// A next-tick timer reports nothing remaining; it is still pending.
	return FMath::Max(0.0, static_cast<double>(Instance->GetTimerManager().GetTimerRemaining(*Timer)));
}

// ---------------------------------------------------------------- manual clock

FDnaClockHandle FDnaManualClock::Schedule(double Seconds, FSimpleDelegate Callback)
{
	FEntry Entry;
	Entry.Id = NextId++;
	Entry.Due = Now + FMath::Max(Seconds, 0.0);
	Entry.Callback = MoveTemp(Callback);

	// Keep (Due, Id) order; ids only grow, so a new entry goes after every entry due at the same time.
	int32 Index = Pending.Num();
	while (Index > 0 && Pending[Index - 1].Due > Entry.Due)
	{
		--Index;
	}
	FDnaClockHandle Handle;
	Handle.Id = Entry.Id;
	Pending.Insert(MoveTemp(Entry), Index);
	return Handle;
}

void FDnaManualClock::Cancel(FDnaClockHandle& Handle)
{
	Pending.RemoveAll([Id = Handle.Id](const FEntry& Entry) { return Entry.Id == Id; });
	Handle.Invalidate();
}

bool FDnaManualClock::IsPending(const FDnaClockHandle& Handle) const
{
	return Handle.IsValid() && Pending.ContainsByPredicate([Id = Handle.Id](const FEntry& Entry) { return Entry.Id == Id; });
}

double FDnaManualClock::GetRemaining(const FDnaClockHandle& Handle) const
{
	const FEntry* Entry = Handle.IsValid() ? Pending.FindByPredicate([Id = Handle.Id](const FEntry& Candidate) { return Candidate.Id == Id; }) : nullptr;
	return Entry ? FMath::Max(0.0, Entry->Due - Now) : -1.0;
}

void FDnaManualClock::Advance(double Seconds)
{
	const double Until = Now + FMath::Max(Seconds, 0.0);
	FireDue(Until);
	Now = Until;
}

bool FDnaManualClock::AdvanceToNext()
{
	if (Pending.Num() == 0)
	{
		return false;
	}
	const double Until = FMath::Max(Now, Pending[0].Due);
	FireDue(Until);
	Now = Until;
	return true;
}

double FDnaManualClock::GetTimeToNext() const
{
	return Pending.Num() > 0 ? FMath::Max(Pending[0].Due - Now, 0.0) : -1.0;
}

void FDnaManualClock::FireDue(double Until)
{
	// One at a time from the front: a callback may schedule or cancel others, and time reads as the
	// moment the callback was due while it runs.
	while (Pending.Num() > 0 && Pending[0].Due <= Until)
	{
		FEntry Entry = MoveTemp(Pending[0]);
		Pending.RemoveAt(0);
		Now = FMath::Max(Now, Entry.Due);
		Entry.Callback.ExecuteIfBound();
	}
}
