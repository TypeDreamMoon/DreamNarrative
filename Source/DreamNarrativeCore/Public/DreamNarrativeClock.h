// Copyright DreamMoon. All Rights Reserved.
//
// Narrative time. Everything in the plugin that waits -- a dialogue's `wait`, a line's `for`, a
// stage's duration, a quest's timeout -- asks the ledger's clock; nothing reads the platform clock or
// a world's timers directly. The default clock is the game's: it follows world time through the game
// instance's timer manager, so it stands still while the game is paused and survives level travel.
// Tests and tools install a manual clock instead, on which `wait 20m` takes no time at all.

#pragma once

#include "CoreMinimal.h"
#include "Engine/TimerHandle.h"
#include "UObject/WeakObjectPtrTemplates.h"

class UGameInstance;

/** A callback waiting on a clock. Default-constructed means nothing is scheduled. */
struct FDnaClockHandle
{
	uint64 Id = 0;

	bool IsValid() const { return Id != 0; }
	void Invalidate() { Id = 0; }
};

class DREAMNARRATIVECORE_API IDnaClock
{
public:
	virtual ~IDnaClock() = default;

	/** Seconds of narrative time the clock has run. Only differences between two readings mean anything. */
	virtual double GetSeconds() const = 0;

	/**
	 * Calls Callback once, Seconds of narrative time from now. A delay of zero or less still waits
	 * for the clock to move: a callback never runs inside the call that schedules it.
	 */
	virtual FDnaClockHandle Schedule(double Seconds, FSimpleDelegate Callback) = 0;

	/** Drops a pending callback. The handle is invalidated either way. */
	virtual void Cancel(FDnaClockHandle& Handle) = 0;

	virtual bool IsPending(const FDnaClockHandle& Handle) const = 0;

	/** Seconds of narrative time until a pending callback runs; negative when it is not pending. */
	virtual double GetRemaining(const FDnaClockHandle& Handle) const = 0;
};

/** The game's clock: the game instance's timer manager, ticked with (dilated, pausable) world time. */
class DREAMNARRATIVECORE_API FDnaWorldClock : public IDnaClock
{
public:
	explicit FDnaWorldClock(UGameInstance* InGameInstance);
	virtual ~FDnaWorldClock() override;

	/** World time of the current world; it starts again from zero when a new world loads. */
	virtual double GetSeconds() const override;
	virtual FDnaClockHandle Schedule(double Seconds, FSimpleDelegate Callback) override;
	virtual void Cancel(FDnaClockHandle& Handle) override;
	virtual bool IsPending(const FDnaClockHandle& Handle) const override;
	virtual double GetRemaining(const FDnaClockHandle& Handle) const override;

private:
	TWeakObjectPtr<UGameInstance> GameInstance;
	TMap<uint64, FTimerHandle> Timers;
	uint64 NextId = 1;
};

/**
 * A clock that moves only when told to. Advancing fires every callback that falls due, earliest
 * first and in scheduling order among equals; a callback scheduled while the clock advances fires in
 * the same advance when it falls inside it.
 */
class DREAMNARRATIVECORE_API FDnaManualClock : public IDnaClock
{
public:
	virtual double GetSeconds() const override { return Now; }
	virtual FDnaClockHandle Schedule(double Seconds, FSimpleDelegate Callback) override;
	virtual void Cancel(FDnaClockHandle& Handle) override;
	virtual bool IsPending(const FDnaClockHandle& Handle) const override;
	virtual double GetRemaining(const FDnaClockHandle& Handle) const override;

	/** Moves time forward by Seconds (zero fires what is already due). */
	void Advance(double Seconds);

	/** Moves time to the earliest pending callback and fires everything due then. False when nothing is pending. */
	bool AdvanceToNext();

	/** Seconds until the earliest pending callback; negative when nothing is pending. */
	double GetTimeToNext() const;

	int32 NumPending() const { return Pending.Num(); }

private:
	struct FEntry
	{
		uint64 Id = 0;
		double Due = 0.0;
		FSimpleDelegate Callback;
	};

	/** Fires, one at a time, every entry due at or before Until. Entries are kept in (Due, Id) order. */
	void FireDue(double Until);

	TArray<FEntry> Pending;
	double Now = 0.0;
	uint64 NextId = 1;
};
