// Copyright DreamMoon. All Rights Reserved.
//
// Program in, script assets out. One asset per document, named after the source file, under the
// configured asset directory. The generator never edits an asset -- it rebuilds it whole from the
// source every time, which is what "assets are compiler output" means in practice.

#pragma once

#include "CoreMinimal.h"
#include "Lang/DnaDiagnostic.h"
#include "Lang/DnaModel.h"

class UDreamNarrativeScript;

namespace UE::DreamNarrative::Lang
{
	struct FDnaGenerateOptions
	{
		/** Write the packages to disk. Off, the assets live in memory only (tests, -NoSave). */
		bool bSave = true;
		/** Put the assets in the transient package instead of a real one. Implies !bSave. */
		bool bTransient = false;
		/** Content path override; empty uses UDreamNarrativeSettings::AssetDirectory. */
		FString AssetDirectory;
	};

	struct FDnaGenerateResult
	{
		TArray<UDreamNarrativeScript*> Scripts;
		int32 NumSaved = 0;
	};

	/**
	 * Generates every document of the program. Returns false when any error was recorded; a document
	 * with errors produces no asset.
	 *
	 *   DNS4006  a value that must be a constant (fact default, implied fact) is not one
	 *   DNS8001  the package could not be created or saved
	 */
	DREAMNARRATIVECOMPILER_API bool GenerateDnaProgram(const FDnaProgram& Program, const FDnaGenerateOptions& Options, FDnaGenerateResult& OutResult, FDnaDiagnosticSink& Diagnostics);

	/** The package name a source file's asset gets: `<AssetDirectory>/<base file name>`. */
	DREAMNARRATIVECOMPILER_API FString GetDnaPackageName(const FString& SourcePath, const FString& AssetDirectory);

	/**
	 * Lowers one expression that stands outside any document into Pool's expression pool and returns
	 * its index; INDEX_NONE when it cannot be lowered. Validate it first (ValidateDnaExpression):
	 * names resolve the way the validator resolved them.
	 */
	DREAMNARRATIVECOMPILER_API int32 CompileDnaExpression(const FDnaProgram& Program, const FDnaExprPtr& Expr, UDreamNarrativeScript& Pool);

	/** Same, for a value written to TargetFact: a bare member of a tag fact's set resolves against that fact. */
	DREAMNARRATIVECOMPILER_API int32 CompileDnaValueFor(const FDnaProgram& Program, const FDnaExprPtr& Expr, const FString& TargetFact, UDreamNarrativeScript& Pool);

	/** Same, for a value given to parameter Param of Event (an emit's argument). */
	DREAMNARRATIVECOMPILER_API int32 CompileDnaValueForParam(const FDnaProgram& Program, const FDnaExprPtr& Expr, const FString& Event, const FString& Param, UDreamNarrativeScript& Pool);
}
