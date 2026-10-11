// Copyright DreamMoon. All Rights Reserved.

#include "DreamNarrativeEditorSettings.h"
#include "DreamNarrativeEditorModule.h"
#include "HAL/PlatformProcess.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"

#define LOCTEXT_NAMESPACE "DreamNarrativeEditor"

UDreamNarrativeEditorSettings::UDreamNarrativeEditorSettings()
{
	SourceLink = TEXT("vscode://file/{file}:{line}");
}

namespace UE::DreamNarrative::Editor
{
	FString MakeSourceLink(const FString& Template, const FString& File, int32 Line)
	{
		FString Full = FPaths::ConvertRelativePathToFull(File);
		FPaths::NormalizeFilename(Full);
		return Template.Replace(TEXT("{file}"), *Full).Replace(TEXT("{line}"), *FString::FromInt(FMath::Max(1, Line)));
	}

	void OpenSource(const FString& File, int32 Line)
	{
		const FString Link = MakeSourceLink(UDreamNarrativeEditorSettings::Get()->SourceLink, File, Line);
		const int32 Scheme = Link.Find(TEXT("://"));
		if (Scheme > 1 && !Link.Left(Scheme).Contains(TEXT(" ")))
		{
			FString Error;
			FPlatformProcess::LaunchURL(*Link, nullptr, &Error);
			if (!Error.IsEmpty())
			{
				UE_LOG(LogDreamNarrativeEditor, Warning, TEXT("Cannot open %s: %s"), *Link, *Error);
			}
			return;
		}
		// A command line: the program, then its arguments.
		FString Program = Link;
		FString Arguments;
		if (Link.StartsWith(TEXT("\"")))
		{
			const int32 Close = Link.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, 1);
			Program = Close > 0 ? Link.Mid(1, Close - 1) : Link.Mid(1);
			Arguments = Close > 0 ? Link.Mid(Close + 1).TrimStart() : FString();
		}
		else
		{
			Link.Split(TEXT(" "), &Program, &Arguments);
		}
		FProcHandle Handle = FPlatformProcess::CreateProc(*Program, *Arguments, /*bLaunchDetached=*/true, /*bLaunchHidden=*/false, /*bLaunchReallyHidden=*/false, nullptr, 0, nullptr, nullptr);
		if (!Handle.IsValid())
		{
			UE_LOG(LogDreamNarrativeEditor, Warning, TEXT("Cannot run %s"), *Link);
		}
		FPlatformProcess::CloseProc(Handle);
	}

	EDnaConflictChoice AskAboutConflicts(const TArray<FString>& Files)
	{
		const FText Message = FText::Format(LOCTEXT("ConflictPrompt",
			"These files changed on disk since the panel read them, and the panel has unsaved edits to them:\n\n{0}\n\n"
			"Yes: discard the panel's edits and read the files again.\n"
			"No: overwrite the files with the panel's edits; the change on disk is lost.\n"
			"Cancel: write nothing and keep both versions."),
			FText::FromString(FString::Join(Files, TEXT("\n"))));
		switch (FMessageDialog::Open(EAppMsgType::YesNoCancel, Message, LOCTEXT("ConflictTitle", "Source changed on disk")))
		{
		case EAppReturnType::Yes: return EDnaConflictChoice::Discard;
		case EAppReturnType::No: return EDnaConflictChoice::Overwrite;
		default: return EDnaConflictChoice::Keep;
		}
	}
}

#undef LOCTEXT_NAMESPACE

