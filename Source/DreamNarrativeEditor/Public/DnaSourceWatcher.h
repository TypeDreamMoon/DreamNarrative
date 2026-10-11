// Copyright DreamMoon. All Rights Reserved.
//
// Rebuilds the scripts when a `.dna` file (or a `*.voice.csv`) under the project's source folder changes,
// or a voice DataTable the settings list is saved. With several
// editors open on the project only one rebuilds -- the first to take the lock file in Saved -- so two
// processes never save the same packages (the second one would silently win). A build with errors
// writes nothing: the assets stay as the last good build left them, and the errors go to the log.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"

struct FFileChangeData;
class FObjectPostSaveContext;
class UPackage;
class IFileHandle;

namespace UE::DreamNarrative::Editor
{
	class DREAMNARRATIVEEDITOR_API FDnaSourceWatcher
	{
	public:
		~FDnaSourceWatcher();

		/** Starts watching, unless this process runs a commandlet or unattended (tests, builds). */
		void Start();
		void Stop();

		/** True when this editor holds the rebuild lock. */
		bool IsOwner() const { return Lock.IsValid(); }

		/**
		 * Compiles every `.dna` under Root as one program and, when it compiles, generates the scripts --
		 * saved when bSave. False, with the errors in OutErrors, when it does not compile; nothing is
		 * generated then.
		 */
		static bool Rebuild(const FString& Root, bool bSave, TArray<FString>& OutErrors, int32& OutScripts);

		/** Takes the lock file at Path if no other process holds it. */
		static TUniquePtr<IFileHandle> TakeLock(const FString& Path);

	private:
		void HandleChanges(const TArray<FFileChangeData>& Changes);
		void HandlePackageSaved(const FString& Filename, UPackage* Package, FObjectPostSaveContext Context);
		bool Tick(float DeltaTime);
		void RebuildNow();

		FString Root;
		FDelegateHandle WatchHandle;
		FTSTicker::FDelegateHandle TickHandle;
		FDelegateHandle SavedHandle;
		TUniquePtr<IFileHandle> Lock;
		/** When the last change came in; a burst of saves rebuilds once, half a second after the last. */
		double DueAt = 0.0;
		bool bToldNotOwner = false;
	};
}
