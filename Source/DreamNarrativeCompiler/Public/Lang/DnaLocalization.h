// Copyright DreamMoon. All Rights Reserved.
//
// Extraction manifests: what each source file gives to translate, as the generator made it, for a
// translation tool to read (DreamLoc reads them; DreamNarrative does not depend on it). The keys are the
// ones the engine's gatherer collects from the script assets: namespace `DNA/<file>`, key the line id
// (or `stage.<stage>.text<n>` and the like).

#pragma once

#include "CoreMinimal.h"

namespace UE::DreamNarrative::Lang
{
	// ---------------------------------------------------------------- extraction manifests
	//
	// What a source file gives to translate, as the generator made it, for a translation tool to read
	// (DreamLoc reads it; DreamNarrative does not depend on it):
	//
	//   <project>/Intermediate/DLoc/extract/DreamNarrative/<source relative to the project>.json
	//
	//   { "version": 1, "provider": "DreamNarrative", "source": "DNarrative/Ch01/灯塔.dna", "namespace": "DNA/灯塔",
	//     "entries": [ { "key": "老周_初见.1", "src": "你就是新来的?", "at": "老周_初见#老周_初见.1", "kind": "line",
	//                    "stable": false, "context": { "speaker": "老周", ... } }, ... ] }

	/** One text a source file gives to translate. */
	struct FDnaLocEntry
	{
		FString Key;
		/** The FText's source: the pattern, `{0}` where an expression goes, `` ` `` escaping a literal brace. */
		FString Source;
		/** Where in the source file, without a line number: `老周_初见#老周_初见.3`, `quest.修灯#title`. */
		FString At;
		/** line, choice, quest, archive, character, chapter or stage. */
		FString Kind;
		/** The `///` comment lines right above the text's line. */
		FString Note;
		/** For translators, in a fixed order: speaker, speakerDisplay, dialogue, previous, tags, channel, for, voice. */
		TArray<TPair<FString, FString>> Context;
		/** What each placeholder stands for, as written: `{0}` is Args[0]. */
		TArray<FString> Args;
		/**
		 * False for a key numbered by position (no `#id`): a text added before it moves the key onto its
		 * neighbour's text. `dnc freeze` writes FreezeId to fix it where it is.
		 */
		bool bStable = true;
		/** The key it had before a `was` rename of what it belongs to; empty when there was none. */
		FString RenamedFrom;
		/** The line the text is written on. */
		int32 SourceLine = 0;
		/** What `dnc freeze` writes on SourceLine (as `#FreezeId`) so the key stays this one. Empty when stable. */
		FString FreezeId;
	};

	/** The texts of one source file. */
	struct FDnaLocManifest
	{
		/** The source file, absolute. */
		FString SourcePath;
		FString Namespace;
		TArray<FDnaLocEntry> Entries;
	};

	/** The JSON of a manifest, with Source as its `source` (the file relative to the project). */
	DREAMNARRATIVECOMPILER_API FString WriteDnaLocManifest(const FDnaLocManifest& Manifest, const FString& Source);

	/**
	 * Where a source file's manifest goes and how it names the file: under the project's
	 * Intermediate/DLoc/extract/DreamNarrative, by its path relative to the project. False for a file
	 * outside the source directory: it has no manifest.
	 */
	DREAMNARRATIVECOMPILER_API bool GetDnaLocManifestPath(const FString& SourcePath, FString& OutManifestPath, FString& OutSource);

	/** Writes the manifest of a file in the source directory, unless the file holds that text already. False when it cannot be written. */
	DREAMNARRATIVECOMPILER_API bool SaveDnaLocManifest(const FDnaLocManifest& Manifest);
}
