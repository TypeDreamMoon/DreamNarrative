// Copyright DreamMoon. All Rights Reserved.

#include "Commandlet/DnaCommandlet.h"
#include "DreamNarrativeCompilerModule.h"
#include "DreamNarrativeCoreModule.h"
#include "DreamNarrativeSettings.h"
#include "Generation/DnaGenerator.h"
#include "HAL/FileManager.h"
#include "Lang/DnaDump.h"
#include "Lang/DnaEditSession.h"
#include "Lang/DnaIds.h"
#include "Lang/DnaModel.h"
#include "Lang/DnaParser.h"
#include "Lang/DnaProject.h"
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

int32 UDreamNarrativeCommandlet::FreezeIds(const FDnaProgram& Program, const FString& OnlyFile, FDnaDiagnosticSink& Diagnostics)
{
	FDnaGenerateResult Generated;
	if (!Diagnostics.HasErrors())
	{
		FDnaGenerateOptions Options;
		Options.bTransient = true;
		Options.bSave = false;
		GenerateDnaProgram(Program, Options, Generated, Diagnostics);
	}
	LogDiagnostics(Diagnostics);
	if (Diagnostics.HasErrors())
	{
		UE_LOG(LogDreamNarrativeCompiler, Error, TEXT("Freeze: the program does not compile; nothing was written"));
		return Diagnostics.NumErrors();
	}
	TMap<FString, TMap<int32, FString>> BySource;
	CollectDnaGeneratedIds(Generated.Scripts, BySource);
	int32 Errors = 0, Total = 0, Written = 0;
	for (const TPair<FString, TMap<int32, FString>>& Pair : BySource)
	{
		if (Pair.Value.Num() == 0 || (!OnlyFile.IsEmpty() && !FPaths::IsSamePath(Pair.Key, OnlyFile)))
		{
			continue;
		}
		FString Text;
		FText Error;
		FDnaEditSession Session;
		int32 Frozen = 0;
		const bool bRead = FFileHelper::LoadFileToString(Text, *Pair.Key);
		if (bRead) { Session.Open(Pair.Key, Text); }
		if (!bRead || !FreezeDnaIds(Session, Pair.Value, Frozen, Error) || !Session.Save(Error))
		{
			UE_LOG(LogDreamNarrativeCompiler, Error, TEXT("%s: %s"), *Pair.Key, bRead ? *Error.ToString() : TEXT("cannot read the file"));
			++Errors;
			continue;
		}
		UE_LOG(LogDreamNarrativeCompiler, Display, TEXT("Froze %d id(s) in %s"), Frozen, *Pair.Key);
		Total += Frozen;
		++Written;
	}
	UE_LOG(LogDreamNarrativeCompiler, Display, TEXT("Freeze: %d id(s) written into %d file(s), %d error(s)"), Total, Written, Errors);
	return Errors;
}

int32 UDreamNarrativeCommandlet::Main(const FString& Params)
{
	TArray<FString> Tokens, Switches;
	TMap<FString, FString> Values;
	ParseCommandLine(*Params, Tokens, Switches, Values);

	const bool bLint = Switches.Contains(TEXT("Lint"));
	const bool bNoSave = Switches.Contains(TEXT("NoSave"));
	const bool bDump = Switches.Contains(TEXT("Dump"));
	const bool bFreeze = Switches.Contains(TEXT("FreezeIds"));

	// -- sources
	const FString ProjectDir = FPaths::ProjectDir();
	FString Root = Values.Contains(TEXT("Root")) ? Values[TEXT("Root")] : ProjectDir / UDreamNarrativeSettings::Get()->SourceDirectory;
	if (FPaths::IsRelative(Root)) { Root = ProjectDir / Root; }

	TArray<FString> Files;
	FString OnlyFile;
	if (const FString* File = Values.Find(TEXT("File")))
	{
		FString Path = *File;
		if (FPaths::IsRelative(Path)) { Path = ProjectDir / Path; }
		OnlyFile = FPaths::ConvertRelativePathToFull(Path);
	}
	// Freezing reads the whole program: the ids come out of a compile, and a file alone may not compile.
	if (!OnlyFile.IsEmpty() && !bFreeze)
	{
		Files.Add(OnlyFile);
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

	// -- read, parse, model and check
	FDnaDiagnosticSink Diagnostics;
	FDnaProgram Program;
	LoadDnaProgram(Files, Program, Diagnostics);
	if (bDump)
	{
		for (const TSharedPtr<FDnaDocument>& Document : Program.Documents)
		{
			UE_LOG(LogDreamNarrativeCompiler, Display, TEXT("--- %s\n%s"), *Document->Path, *DumpDnaTree(*Document));
		}
	}

	if (bFreeze)
	{
		return FreezeIds(Program, OnlyFile, Diagnostics);
	}

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
