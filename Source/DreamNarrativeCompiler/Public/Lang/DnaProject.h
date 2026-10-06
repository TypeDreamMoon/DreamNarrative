// Copyright DreamMoon. All Rights Reserved.
//
// A project's sources read the way the build reads them: every .dna under the source directory,
// names collected across all of them first (a text line's trailing `if` counts as a condition only
// when it names something declared somewhere), then parsed, modelled and checked as one program.

#pragma once

#include "CoreMinimal.h"
#include "Lang/DnaModel.h"

namespace UE::DreamNarrative::Lang
{
	/** The project's source directory, absolute: <project>/<Project Settings > Dream Narrative > Source Directory>. */
	DREAMNARRATIVECOMPILER_API FString GetDnaSourceRoot();

	/** Every .dna under Root, absolute and sorted. */
	DREAMNARRATIVECOMPILER_API void FindDnaSources(const FString& Root, TArray<FString>& OutFiles);

	/**
	 * Reads, parses, models and checks Files as one program. A file that cannot be read is DNS9002.
	 * Texts, when given, stand in for what is on disk (an editor's unsaved text, say).
	 */
	DREAMNARRATIVECOMPILER_API void LoadDnaProgram(const TArray<FString>& Files, FDnaProgram& OutProgram, FDnaDiagnosticSink& Diagnostics,
		const TMap<FString, FString>* Texts = nullptr);
}
