// Copyright DreamMoon. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DreamNarrativeClock.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaManualClockTest, "DreamNarrative.Ledger.ManualClock", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDnaManualClockTest::RunTest(const FString& Parameters)
{
	FDnaManualClock Clock;
	TArray<FString> Fired;
	auto Record = [&Fired, &Clock](const TCHAR* Name)
	{
		return FSimpleDelegate::CreateLambda([&Fired, &Clock, Name]() { Fired.Add(FString::Printf(TEXT("%s@%g"), Name, Clock.GetSeconds())); });
	};

	// -- order: earliest first, scheduling order among equals
	Clock.Schedule(5.0, Record(TEXT("a")));
	Clock.Schedule(2.0, Record(TEXT("b")));
	Clock.Schedule(2.0, Record(TEXT("c")));
	TestEqual(TEXT("three pending"), Clock.NumPending(), 3);
	TestEqual(TEXT("next due in 2s"), Clock.GetTimeToNext(), 2.0);
	Clock.Advance(1.0);
	TestEqual(TEXT("nothing due at 1s"), Fired.Num(), 0);
	Clock.Advance(1.0);
	TestEqual(TEXT("both due at 2s, in order"), FString::Join(Fired, TEXT(" ")), FString(TEXT("b@2 c@2")));
	TestTrue(TEXT("to the next"), Clock.AdvanceToNext());
	TestEqual(TEXT("a at 5s"), Fired.Last(), FString(TEXT("a@5")));
	TestEqual(TEXT("time is 5s"), Clock.GetSeconds(), 5.0);
	TestFalse(TEXT("nothing left"), Clock.AdvanceToNext());
	TestEqual(TEXT("no next"), Clock.GetTimeToNext(), -1.0);

	// -- a callback never runs inside Schedule, even with no delay
	Fired.Reset();
	const FDnaClockHandle Now = Clock.Schedule(0.0, Record(TEXT("now")));
	TestEqual(TEXT("not synchronous"), Fired.Num(), 0);
	TestTrue(TEXT("pending"), Clock.IsPending(Now));
	Clock.Advance(0.0);
	TestEqual(TEXT("fires on the next move"), FString::Join(Fired, TEXT(" ")), FString(TEXT("now@5")));
	TestFalse(TEXT("no longer pending"), Clock.IsPending(Now));

	// -- a callback scheduled while advancing fires in the same advance when it falls inside it
	Fired.Reset();
	Clock.Schedule(1.0, FSimpleDelegate::CreateLambda([&]()
	{
		Fired.Add(TEXT("outer"));
		Clock.Schedule(1.0, Record(TEXT("inner")));
		Clock.Schedule(10.0, Record(TEXT("late")));
	}));
	Clock.Advance(3.0);
	TestEqual(TEXT("chained inside the step"), FString::Join(Fired, TEXT(" ")), FString(TEXT("outer inner@7")));
	TestEqual(TEXT("the late one waits"), Clock.NumPending(), 1);

	// -- cancelling
	FDnaClockHandle Cancelled = Clock.Schedule(1.0, Record(TEXT("cancelled")));
	Clock.Cancel(Cancelled);
	TestFalse(TEXT("handle invalidated"), Cancelled.IsValid());
	Fired.Reset();
	Clock.Advance(2.0);
	TestEqual(TEXT("cancelled never fires"), Fired.Num(), 0);
	return true;
}

#endif
