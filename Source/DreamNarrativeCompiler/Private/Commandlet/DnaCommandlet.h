// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "Lang/DnaDiagnostic.h"
#include "Lang/DnaModel.h"
#include "DnaCommandlet.generated.h"

/**
 * Headless DreamNarrative build.
 *
 *   UnrealEditor-Cmd.exe <Project>.uproject -run=DreamNarrative [options]
 *
 *   -File=<path>   compile one source (absolute, or relative to the project directory)
 *   -All           every .dna under the configured source directory (the default without -File)
 *   -Root=<dir>    source directory override
 *   -Lint          parse and validate only; write nothing
 *   -NoSave        generate in memory without writing packages
 *   -Dump          print each file's syntax tree
 *   -VoScript=<file>  write the recording script (every line, its speaker, text and voice) as CSV
 *   -VoStatus      how many lines of each character have a voice, and which do not
 *   -FreezeIds     write `#id` onto every line and choice that has none, with the id it has now;
 *                  with -File only that file is written (the whole program is still read)
 *
 * Exit code is the error count, so a CI gate tests for zero.
 */
UCLASS()
class UDreamNarrativeCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UDreamNarrativeCommandlet();

	virtual int32 Main(const FString& Params) override;

private:
	int32 Voices(const UE::DreamNarrative::Lang::FDnaProgram& Program, const FString& ScriptFile, UE::DreamNarrative::Lang::FDnaDiagnosticSink& Diagnostics);
	int32 FreezeIds(const UE::DreamNarrative::Lang::FDnaProgram& Program, const FString& OnlyFile, UE::DreamNarrative::Lang::FDnaDiagnosticSink& Diagnostics);
};
