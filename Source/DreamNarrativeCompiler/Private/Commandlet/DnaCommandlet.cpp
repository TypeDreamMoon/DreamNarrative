// Copyright DreamMoon. All Rights Reserved.

#include "Commandlet/DnaCommandlet.h"
#include "DreamNarrativeCompilerModule.h"
#include "DreamNarrativeCoreModule.h"
#include "DreamNarrativeSettings.h"
#include "Generation/DnaGenerator.h"
#include "HAL/FileManager.h"
#include "Lang/DnaDump.h"
#include "Lang/DnaModel.h"
#include "Lang/DnaParser.h"
#include "Lang/DnaValidator.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

using namespace UE::DreamNarrative::Lang;

UDreamNarrativeCommandlet::UDreamNarrativeCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

namespace
{
	void LogDiagnostics(const FDnaDiagnosticSink& Sink)
	{
		for (const FDnaDiagnostic& Diagnostic : Sink.GetDiagnostics())
		{
			const FString Line = FDnaDiagnosticSink::ToLogString(Diagnostic);
			switch (Diagnostic.Severity)
			{
			case EDnaSeverity::Error: UE_LOG(LogDreamNarrativeCompiler, Error, TEXT("%s"), *Line); break;
			case EDnaSeverity::Warning: UE_LOG(LogDreamNarrativeCompiler, Warning, TEXT("%s"), *Line); break;
			default: UE_LOG(LogDreamNarrativeCompiler, Display, TEXT("%s"), *Line); break;
			}
		}
	}
}

int32 UDreamNarrativeCommandlet::Main(const FString& Params)
{
	TArray<FString> Tokens, Switches;
	TMap<FString, FString> Values;
	ParseCommandLine(*Params, Tokens, Switches, Values);

	const bool bLint = Switches.Contains(TEXT("Lint"));
	const bool bNoSave = Switches.Contains(TEXT("NoSave"));
	const bool bDump = Switches.Contains(TEXT("Dump"));

	// -- sources
	const FString ProjectDir = FPaths::ProjectDir();
	FString Root = Values.Contains(TEXT("Root")) ? Values[TEXT("Root")] : ProjectDir / UDreamNarrativeSettings::Get()->SourceDirectory;
	if (FPaths::IsRelative(Root)) { Root = ProjectDir / Root; }

	TArray<FString> Files;
	if (const FString* File = Values.Find(TEXT("File")))
	{
		FString Path = *File;
		if (FPaths::IsRelative(Path)) { Path = ProjectDir / Path; }
		Files.Add(FPaths::ConvertRelativePathToFull(Path));
	}
	else
	{
		IFileManager::Get().FindFilesRecursive(Files, *Root, TEXT("*.dna"), true, false, false);
		Files.Sort();
	}
	if (Files.Num() == 0)
	{
		UE_LOG(LogDreamNarrativeCompiler, Warning, TEXT("No .dna sources under %s"), *Root);
		return 0;
	}
	UE_LOG(LogDreamNarrativeCompiler, Display, TEXT("%d source(s)"), Files.Num());

	// -- pass 1: names across the project, for the text-tail `if` rule
	TSet<FString> ProjectNames;
	TMap<FString, FString> Texts;
	for (const FString& File : Files)
	{
		FString Text;
		if (FFileHelper::LoadFileToString(Text, *File))
		{
			CollectDnaDeclaredNames(FDnaSourceText(File, Text), ProjectNames);
			Texts.Add(File, MoveTemp(Text));
		}
	}

	// -- pass 2: parse
	FDnaDiagnosticSink Diagnostics;
	TArray<TSharedPtr<FDnaDocument>> Documents;
	for (const FString& File : Files)
	{
		const FString* Text = Texts.Find(File);
		if (!Text)
		{
			Diagnostics.SetFilePath(File);
			Diagnostics.Error(TEXT("DNS9002"), FDnaSpan(), FText::FromString(TEXT("Cannot read the file")));
			continue;
		}
		FDnaParseOptions Options;
		Options.ExternalNames = &ProjectNames;
		TSharedPtr<FDnaDocument> Document = MakeShared<FDnaDocument>();
		Diagnostics.SetFilePath(File);
		ParseDnaDocument(FDnaSourceText(File, *Text), Options, *Document, Diagnostics);
		if (bDump)
		{
			UE_LOG(LogDreamNarrativeCompiler, Display, TEXT("--- %s\n%s"), *File, *DumpDnaTree(*Document));
		}
		Documents.Add(Document);
	}

	// -- model and checks
	FDnaProgram Program;
	BuildDnaProgram(Documents, Program, Diagnostics);
	ValidateDnaProgram(Program, Diagnostics);

	// -- generate
	FDnaGenerateResult Result;
	if (!bLint && !Diagnostics.HasErrors())
	{
		FDnaGenerateOptions Options;
		Options.bSave = !bNoSave;
		GenerateDnaProgram(Program, Options, Result, Diagnostics);
	}

	LogDiagnostics(Diagnostics);
	UE_LOG(LogDreamNarrativeCompiler, Display, TEXT("%s: %d file(s), %d asset(s) written, %d error(s), %d diagnostic(s)"),
		bLint ? TEXT("Lint") : TEXT("Build"), Files.Num(), Result.NumSaved, Diagnostics.NumErrors(), Diagnostics.Num());
	return Diagnostics.NumErrors();
}
