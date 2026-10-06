// Copyright DreamMoon. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DnaSourceWatcher.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaSourceWatcherTest, "DreamNarrative.Editor.SourceWatcher", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDnaSourceWatcherTest::RunTest(const FString& Parameters)
{
	using namespace UE::DreamNarrative::Editor;

	const FString Scratch = FPaths::ProjectSavedDir() / TEXT("DreamNarrativeTests") / TEXT("Watcher");
	IFileManager::Get().DeleteDirectory(*Scratch, false, true);

	// -- one editor rebuilds: the lock is taken once, and free again when it is let go
	{
		const FString LockPath = Scratch / TEXT("Rebuild.lock");
		TUniquePtr<IFileHandle> First = FDnaSourceWatcher::TakeLock(LockPath);
		TUniquePtr<IFileHandle> Second = FDnaSourceWatcher::TakeLock(LockPath);
		TestTrue(TEXT("the first takes the lock"), First.IsValid());
		TestFalse(TEXT("the second does not"), Second.IsValid());
		First.Reset();
		TestTrue(TEXT("let go, it can be taken again"), FDnaSourceWatcher::TakeLock(LockPath).IsValid());
	}

	// -- a rebuild compiles everything, and generates nothing when anything is wrong
	const FString Root = Scratch / TEXT("Sources");
	FFileHelper::SaveStringToFile(FString(TEXT("character 甲\n\ndialogue 甲_问候 for 甲\n  甲: 你好。\n")), *(Root / TEXT("Good.dna")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	TArray<FString> Errors;
	int32 Scripts = 0;
	TestTrue(TEXT("compiles"), FDnaSourceWatcher::Rebuild(Root, /*bSave=*/false, Errors, Scripts));
	TestEqual(TEXT("one script"), Scripts, 1);
	FFileHelper::SaveStringToFile(FString(TEXT("dialogue 乙_问候 for 乙\n  乙: 嗯。\n")), *(Root / TEXT("Bad.dna")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	Errors.Reset();
	TestFalse(TEXT("a speaker nobody declared stops the build"), FDnaSourceWatcher::Rebuild(Root, /*bSave=*/false, Errors, Scripts));
	TestTrue(TEXT("and says why"), Errors.ContainsByPredicate([](const FString& Error) { return Error.Contains(TEXT("DNS3002")); }));
	TestEqual(TEXT("nothing generated"), Scripts, 0);

	IFileManager::Get().DeleteDirectory(*Scratch, false, true);
	return !HasAnyErrors();
}

#endif
