// Copyright DreamMoon. All Rights Reserved.

#include "Lang/DnaProject.h"
#include "DreamNarrativeSettings.h"
#include "HAL/FileManager.h"
#include "Lang/DnaParser.h"
#include "Lang/DnaValidator.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace UE::DreamNarrative::Lang
{
	FString GetDnaSourceRoot()
	{
		return ResolveDnaProjectPath(UDreamNarrativeSettings::Get()->SourceDirectory);
	}

	FString ResolveDnaProjectPath(const FString& Path)
	{
		const FString Absolute = FPaths::IsRelative(Path) ? FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()) / Path : Path;
		return FPaths::ConvertRelativePathToFull(Absolute);
	}

	void FindDnaSources(const FString& Root, TArray<FString>& OutFiles)
	{
		OutFiles.Reset();
		IFileManager::Get().FindFilesRecursive(OutFiles, *Root, TEXT("*.dna"), true, false, false);
		for (FString& File : OutFiles)
		{
			File = FPaths::ConvertRelativePathToFull(File);
		}
		OutFiles.Sort();
	}

	void LoadDnaProgram(const TArray<FString>& Files, FDnaProgram& OutProgram, FDnaDiagnosticSink& Diagnostics, const TMap<FString, FString>* Texts)
	{
		// -- pass 1: names across the project, for the text-tail `if` rule
		TSet<FString> ProjectNames;
		TMap<FString, FString> Read;
		for (const FString& File : Files)
		{
			FString Text;
			if (const FString* Given = Texts ? Texts->Find(File) : nullptr)
			{
				Text = *Given;
			}
			else if (!FFileHelper::LoadFileToString(Text, *File))
			{
				continue;
			}
			CollectDnaDeclaredNames(FDnaSourceText(File, Text), ProjectNames);
			Read.Add(File, MoveTemp(Text));
		}

		// -- pass 2: parse
		TArray<TSharedPtr<FDnaDocument>> Documents;
		for (const FString& File : Files)
		{
			const FString* Text = Read.Find(File);
			Diagnostics.SetFilePath(File);
			if (!Text)
			{
				Diagnostics.Error(TEXT("DNS9002"), FDnaSpan(), FText::FromString(TEXT("Cannot read the file")));
				continue;
			}
			FDnaParseOptions Options;
			Options.ExternalNames = &ProjectNames;
			TSharedPtr<FDnaDocument> Document = MakeShared<FDnaDocument>();
			ParseDnaDocument(FDnaSourceText(File, *Text), Options, *Document, Diagnostics);
			Documents.Add(Document);
		}

		// -- model and checks
		BuildDnaProgram(Documents, OutProgram, Diagnostics);
		ValidateDnaProgram(OutProgram, Diagnostics);
	}
}
