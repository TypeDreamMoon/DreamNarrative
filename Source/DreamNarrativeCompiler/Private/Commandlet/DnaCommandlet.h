// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
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
};
