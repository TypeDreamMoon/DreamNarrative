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
#include "Lang/DnaLocalization.h"
#include "Lang/DnaVoice.h"
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

int32 UDreamNarrativeCommandlet::Voices(const FDnaProgram& Program, const FString& ScriptFile, FDnaDiagnosticSink& Diagnostics)
{
	FDnaGenerateResult Generated;
	if (!Diagnostics.HasErrors())
	{
		FDnaGenerateOptions Options;
		Options.bTransient = true;
		Options.bSave = false;
		Options.bReportVoiceOrphans = true;
		GenerateDnaProgram(Program, Options, Generated, Diagnostics);
	}
	LogDiagnostics(Diagnostics);
	if (Diagnostics.HasErrors())
	{
		return Diagnostics.NumErrors();
	}
	TArray<FDnaVoiceScriptRow> Rows;
	CollectDnaVoiceScript(Program, Generated.Scripts, Rows);
	if (!ScriptFile.IsEmpty())
	{
		FString Path = ScriptFile;
		if (FPaths::IsRelative(Path)) { Path = FPaths::ProjectDir() / Path; }
		// With a byte order mark: a spreadsheet opens it as UTF-8.
		if (!FFileHelper::SaveStringToFile(WriteDnaVoiceScript(Rows), *Path, FFileHelper::EEncodingOptions::ForceUTF8))
		{
			UE_LOG(LogDreamNarrativeCompiler, Error, TEXT("Cannot write %s"), *Path);
			return 1;
		}
		UE_LOG(LogDreamNarrativeCompiler, Display, TEXT("Wrote %d line(s) to %s"), Rows.Num(), *FPaths::ConvertRelativePathToFull(Path));
	}
	// Per character: lines, voiced, missing (the first few named).
	TMap<FString, TArray<const FDnaVoiceScriptRow*>> ByCharacter;
	for (const FDnaVoiceScriptRow& Row : Rows) { ByCharacter.FindOrAdd(Row.Character.IsEmpty() ? TEXT("(narration)") : Row.Character).Add(&Row); }
	int32 Lines = 0, Found = 0, Missing = 0;
	for (const TPair<FString, TArray<const FDnaVoiceScriptRow*>>& Pair : ByCharacter)
	{
		TArray<FString> Gaps;
		int32 Voiced = 0;
		for (const FDnaVoiceScriptRow* Row : Pair.Value)
		{
			if (Row->Status == TEXT("found")) { ++Voiced; }
			else if (Row->Status == TEXT("missing")) { Gaps.Add(Row->LineId); }
		}
		Lines += Pair.Value.Num();
		Found += Voiced;
		Missing += Gaps.Num();
		const FString Shown = FString::Join(TArrayView<const FString>(Gaps).Left(8), TEXT(", ")) + (Gaps.Num() > 8 ? TEXT(", ...") : TEXT(""));
		UE_LOG(LogDreamNarrativeCompiler, Display, TEXT("%s: %d line(s), %d voiced, %d missing%s%s"), *Pair.Key, Pair.Value.Num(), Voiced, Gaps.Num(),
			Gaps.Num() > 0 ? TEXT(": ") : TEXT(""), *Shown);
	}
	UE_LOG(LogDreamNarrativeCompiler, Display, TEXT("Voices: %d line(s), %d voiced, %d missing"), Lines, Found, Missing);
	return 0;
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
	CollectDnaGeneratedIds(Generated.Localization, BySource);
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

int32 UDreamNarrativeCommandlet::Translations(const FDnaProgram& Program, const TMap<FString, FString>& Values, FDnaDiagnosticSink& Diagnostics)
{
	auto PathOf = [](FString Path) { return FPaths::ConvertRelativePathToFull(FPaths::IsRelative(Path) ? FPaths::ProjectDir() / Path : Path); };
	const FString Culture = Values.FindRef(TEXT("Culture"));
	FDnaLocTarget Target = GetDnaLocTarget(Values.Contains(TEXT("Target")) ? Values[TEXT("Target")] : FString(TEXT("Game")));
	Target.NativeCulture = Values.FindRef(TEXT("NativeCulture"));

	if (const FString* In = Values.Find(TEXT("LocImport")))
	{
		const FString Path = PathOf(*In);
		FString Text;
		TArray<FDnaLocRow> Rows;
		FText Error;
		if (Culture.IsEmpty())
		{
			UE_LOG(LogDreamNarrativeCompiler, Error, TEXT("Import: say which culture the translations are in (-Culture=en)"));
			return 1;
		}
		if (!FFileHelper::LoadFileToString(Text, *Path) || !ReadDnaLocCsv(Text, Rows, Error))
		{
			UE_LOG(LogDreamNarrativeCompiler, Error, TEXT("%s: %s"), *Path, Error.IsEmpty() ? TEXT("cannot read the file") : *Error.ToString());
			return 1;
		}
		FDnaLocImport Import;
		if (!ImportDnaLocTranslations(Target, Culture, Rows, Import, Error))
		{
			UE_LOG(LogDreamNarrativeCompiler, Error, TEXT("Import: %s"), *Error.ToString());
			return 1;
		}
		for (const FString& Skipped : Import.Skipped) { UE_LOG(LogDreamNarrativeCompiler, Warning, TEXT("Not imported: %s"), *Skipped); }
		UE_LOG(LogDreamNarrativeCompiler, Display, TEXT("Import into %s/%s: %d imported, %d unchanged, %d without a translation, %d not imported"),
			*Target.Name, *Culture, Import.Imported, Import.Unchanged, Import.Empty, Import.Skipped.Num());
		return 0;
	}

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
		return Diagnostics.NumErrors();
	}
	TArray<FDnaLocRow> Rows;
	CollectDnaLocTexts(Generated.Scripts, Rows);
	const int32 Filled = Culture.IsEmpty() ? 0 : FillDnaLocTranslations(Target, Culture, Rows);
	const FString Path = PathOf(Values[TEXT("LocExport")]);
	// With a byte order mark: a spreadsheet opens it as UTF-8.
	if (!FFileHelper::SaveStringToFile(WriteDnaLocCsv(Rows), *Path, FFileHelper::EEncodingOptions::ForceUTF8))
	{
		UE_LOG(LogDreamNarrativeCompiler, Error, TEXT("Cannot write %s"), *Path);
		return 1;
	}
	UE_LOG(LogDreamNarrativeCompiler, Display, TEXT("Wrote %d text(s) to %s%s"), Rows.Num(), *Path,
		Culture.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(", %d already translated into %s"), Filled, *Culture));
	return 0;
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
	const bool bVoStatus = Switches.Contains(TEXT("VoStatus"));
	const FString VoScript = Values.Contains(TEXT("VoScript")) ? Values[TEXT("VoScript")] : FString();
	const bool bTranslations = Values.Contains(TEXT("LocExport")) || Values.Contains(TEXT("LocImport"));
	const bool bWholeProgram = bFreeze || bVoStatus || !VoScript.IsEmpty() || bTranslations;

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
	if (!OnlyFile.IsEmpty() && !bWholeProgram)
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
	if (bVoStatus || !VoScript.IsEmpty())
	{
		return Voices(Program, VoScript, Diagnostics);
	}
	if (bTranslations)
	{
		return Translations(Program, Values, Diagnostics);
	}

	// -- generate
	FDnaGenerateResult Result;
	if (!bLint && !Diagnostics.HasErrors())
	{
		FDnaGenerateOptions Options;
		Options.bSave = !bNoSave;
		Options.bReportVoiceOrphans = OnlyFile.IsEmpty();
		GenerateDnaProgram(Program, Options, Result, Diagnostics);
	}

	LogDiagnostics(Diagnostics);
	UE_LOG(LogDreamNarrativeCompiler, Display, TEXT("%s: %d file(s), %d asset(s) written, %d error(s), %d diagnostic(s)"),
		bLint ? TEXT("Lint") : TEXT("Build"), Files.Num(), Result.NumSaved, Diagnostics.NumErrors(), Diagnostics.Num());
	return Diagnostics.NumErrors();
}
