// Copyright DreamMoon. All Rights Reserved.
//
// Listens to the log while it exists and keeps the errors and warnings raised on the game thread:
// what the code under test logged, without what other threads happen to log meanwhile. It hears each
// line the moment it is logged, so Take() needs no flush.

#pragma once

#include "CoreMinimal.h"
#include "Misc/OutputDevice.h"
#include "Misc/ScopeLock.h"

namespace UE::DreamNarrative::Tests
{
	class FDnaLogCapture : public FOutputDevice
	{
	public:
		FDnaLogCapture() { GLog->AddOutputDevice(this); }
		virtual ~FDnaLogCapture() override { GLog->RemoveOutputDevice(this); }

		/** The lines kept since the last call, oldest first, as `Category: Error: text`. */
		TArray<FString> Take()
		{
			FScopeLock Guard(&Lock);
			TArray<FString> Taken = MoveTemp(Lines);
			Lines.Reset();
			return Taken;
		}

		virtual void Serialize(const TCHAR* Message, ELogVerbosity::Type Verbosity, const FName& Category) override
		{
			const ELogVerbosity::Type Level = static_cast<ELogVerbosity::Type>(Verbosity & ELogVerbosity::VerbosityMask);
			if (Level == ELogVerbosity::NoLogging || Level > ELogVerbosity::Warning || !IsInGameThread())
			{
				return;
			}
			FScopeLock Guard(&Lock);
			Lines.Add(FString::Printf(TEXT("%s: %s: %s"), *Category.ToString(), Level == ELogVerbosity::Warning ? TEXT("Warning") : TEXT("Error"), Message));
		}

		// Unbuffered: the redirector hands such a device each line on the thread that logs it, at once.
		virtual bool CanBeUsedOnMultipleThreads() const override { return true; }
		virtual bool CanBeUsedOnAnyThread() const override { return true; }

	private:
		FCriticalSection Lock;
		TArray<FString> Lines;
	};
}
