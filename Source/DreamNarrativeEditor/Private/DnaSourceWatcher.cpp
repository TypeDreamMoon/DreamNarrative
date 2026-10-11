// Copyright DreamMoon. All Rights Reserved.

#include "DnaSourceWatcher.h"
#include "DreamNarrativeSettings.h"
#include "UObject/Package.h"
#include "UObject/ObjectSaveContext.h"
#include "DirectoryWatcherModule.h"
#include "DreamNarrativeEditorModule.h"
#include "Generation/DnaGenerator.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformFileManager.h"
#include "IDirectoryWatcher.h"
#include "Lang/DnaProject.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"

namespace UE::DreamNarrative::Editor
{
	FDnaSourceWatcher::~FDnaSourceWatcher()
	{
		Stop();
	}

	TUniquePtr<IFileHandle> FDnaSourceWatcher::TakeLock(const FString& Path)
	{
		IPlatformFile& Files = FPlatformFileManager::Get().GetPlatformFile();
		Files.CreateDirectoryTree(*FPaths::GetPath(Path));
		// Held open for writing, the file cannot be opened for writing by another process.
		return TUniquePtr<IFileHandle>(Files.OpenWrite(*Path, /*bAppend=*/false, /*bAllowRead=*/true));
	}

	void FDnaSourceWatcher::Start()
	{
		if (IsRunningCommandlet() || FApp::IsUnattended() || !GIsEditor)
		{
			return;
		}
		Root = Lang::GetDnaSourceRoot();
		if (!FPaths::DirectoryExists(Root))
		{
			UE_LOG(LogDreamNarrativeEditor, Log, TEXT("No source folder at %s; nothing to watch"), *Root);
			return;
		}
		Lock = TakeLock(FPaths::ProjectSavedDir() / TEXT("DreamNarrative") / TEXT("Rebuild.lock"));
		FDirectoryWatcherModule& Module = FModuleManager::LoadModuleChecked<FDirectoryWatcherModule>(TEXT("DirectoryWatcher"));
		if (IDirectoryWatcher* Watcher = Module.Get())
		{
			Watcher->RegisterDirectoryChangedCallback_Handle(Root, IDirectoryWatcher::FDirectoryChanged::CreateRaw(this, &FDnaSourceWatcher::HandleChanges),
				WatchHandle, IDirectoryWatcher::WatchOptions::IncludeDirectoryChanges);
		}
		TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FDnaSourceWatcher::Tick), 0.1f);
		SavedHandle = UPackage::PackageSavedWithContextEvent.AddRaw(this, &FDnaSourceWatcher::HandlePackageSaved);
		UE_LOG(LogDreamNarrativeEditor, Log, TEXT("Watching %s%s"), *Root, IsOwner() ? TEXT("") : TEXT(" (another editor rebuilds)"));
	}

	void FDnaSourceWatcher::Stop()
	{
		if (WatchHandle.IsValid())
		{
			if (FDirectoryWatcherModule* Module = FModuleManager::GetModulePtr<FDirectoryWatcherModule>(TEXT("DirectoryWatcher")))
			{
				if (IDirectoryWatcher* Watcher = Module->Get())
				{
					Watcher->UnregisterDirectoryChangedCallback_Handle(Root, WatchHandle);
				}
			}
			WatchHandle.Reset();
		}
		if (TickHandle.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
			TickHandle.Reset();
		}
		UPackage::PackageSavedWithContextEvent.Remove(SavedHandle);
		SavedHandle.Reset();
		Lock.Reset();
	}

	void FDnaSourceWatcher::HandleChanges(const TArray<FFileChangeData>& Changes)
	{
		for (const FFileChangeData& Change : Changes)
		{
			if (Change.Filename.EndsWith(TEXT(".dna")) || Change.Filename.EndsWith(TEXT(".voice.csv")))
			{
				DueAt = FPlatformTime::Seconds() + 0.5;
				return;
			}
		}
	}

	void FDnaSourceWatcher::HandlePackageSaved(const FString& Filename, UPackage* Package, FObjectPostSaveContext Context)
	{
		// A voice DataTable the settings list changed: the lines it answers find their voices again.
		const FString Name = Package ? Package->GetName() : FString();
		for (const FSoftObjectPath& Table : UDreamNarrativeSettings::Get()->VoiceTables)
		{
			if (Table.GetLongPackageName() == Name)
			{
				DueAt = FPlatformTime::Seconds() + 0.5;
				return;
			}
		}
	}

	bool FDnaSourceWatcher::Tick(float DeltaTime)
	{
		if (DueAt > 0.0 && FPlatformTime::Seconds() >= DueAt)
		{
			DueAt = 0.0;
			RebuildNow();
		}
		return true;
	}

	void FDnaSourceWatcher::RebuildNow()
	{
		if (!IsOwner())
		{
			if (!bToldNotOwner)
			{
				UE_LOG(LogDreamNarrativeEditor, Display, TEXT("A .dna source changed; another editor open on this project rebuilds the scripts"));
				bToldNotOwner = true;
			}
			return;
		}
		TArray<FString> Errors;
		int32 Scripts = 0;
		if (Rebuild(Root, /*bSave=*/true, Errors, Scripts))
		{
			UE_LOG(LogDreamNarrativeEditor, Display, TEXT("Rebuilt %d script(s) from %s"), Scripts, *Root);
			return;
		}
		UE_LOG(LogDreamNarrativeEditor, Warning, TEXT("The .dna sources do not compile; the scripts are left as they were:"));
		for (const FString& Error : Errors)
		{
			UE_LOG(LogDreamNarrativeEditor, Warning, TEXT("  %s"), *Error);
		}
	}

	bool FDnaSourceWatcher::Rebuild(const FString& InRoot, bool bSave, TArray<FString>& OutErrors, int32& OutScripts)
	{
		OutScripts = 0;
		TArray<FString> Files;
		Lang::FindDnaSources(InRoot, Files);
		Lang::FDnaProgram Program;
		Lang::FDnaDiagnosticSink Diagnostics;
		Lang::LoadDnaProgram(Files, Program, Diagnostics);
		auto Collect = [&OutErrors, &Diagnostics]()
		{
			for (const Lang::FDnaDiagnostic& Diagnostic : Diagnostics.GetDiagnostics())
			{
				if (Diagnostic.Severity == Lang::EDnaSeverity::Error)
				{
					OutErrors.Add(Lang::FDnaDiagnosticSink::ToLogString(Diagnostic));
				}
			}
		};
		if (Diagnostics.HasErrors())
		{
			Collect();
			return false;
		}
		Lang::FDnaGenerateOptions Options;
		Options.bSave = bSave;
		Options.bTransient = !bSave;
		Options.bReportVoiceOrphans = true;
		Lang::FDnaGenerateResult Result;
		if (!Lang::GenerateDnaProgram(Program, Options, Result, Diagnostics))
		{
			Collect();
			return false;
		}
		OutScripts = Result.Scripts.Num();
		return true;
	}

	namespace
	{
		FAutoConsoleCommand RebuildCommand(
			TEXT("dn.rebuild"),
			TEXT("Compiles the project's .dna sources and saves the scripts, as the watcher does on a change"),
			FConsoleCommandDelegate::CreateLambda([]()
			{
				TArray<FString> Errors;
				int32 Scripts = 0;
				if (FDnaSourceWatcher::Rebuild(Lang::GetDnaSourceRoot(), /*bSave=*/true, Errors, Scripts))
				{
					UE_LOG(LogDreamNarrativeEditor, Display, TEXT("Rebuilt %d script(s)"), Scripts);
					return;
				}
				for (const FString& Error : Errors)
				{
					UE_LOG(LogDreamNarrativeEditor, Warning, TEXT("%s"), *Error);
				}
			}));
	}
}
