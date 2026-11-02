// Copyright DreamMoon. All Rights Reserved.
//
// Translation tables. Every localizable text of the compiled scripts goes out as CSV with what a
// translator needs (who says it, in which dialogue, after which line); the translated CSV comes back
// into a localization target's archive, where the Localization Dashboard compiles it like any other.
// The keys are the ones the engine's gatherer collects from the script assets: namespace `DNA/<file>`,
// key the line id (or `stage.<stage>.text<n>` and the like).

#pragma once

#include "CoreMinimal.h"

class UDreamNarrativeScript;

namespace UE::DreamNarrative::Lang
{
	/** One localizable text and what a translator needs to know about it. */
	struct FDnaLocRow
	{
		FString Namespace;
		FString Key;
		/** The text as the source has it; `{0}` stands where an expression goes. */
		FString Source;
		/** In the culture asked for; empty when there is none yet. */
		FString Translation;
		/** `line`, `choice`, or `text` (titles, journal entries, archive, names, stage text). */
		FString Kind;
		/** The speaker; empty for narration and for what is not a line. */
		FString Character;
		FString Dialogue;
		/** The line before it in the same dialogue. */
		FString Previous;
		/** Speaker tags (`老周(皱眉)`), space separated. */
		FString Tags;
		/** The source file, relative to the source directory. */
		FString File;
		/** Its line in File; 0 when not known. */
		int32 Line = 0;
	};

	/** Every localizable text of the scripts, in the order each script has them, once per namespace and key. */
	DREAMNARRATIVECOMPILER_API void CollectDnaLocTexts(const TArray<UDreamNarrativeScript*>& Scripts, TArray<FDnaLocRow>& OutRows);

	/** The rows as CSV: `Namespace,Key,Source,Translation`, then the context columns. */
	DREAMNARRATIVECOMPILER_API FString WriteDnaLocCsv(const TArray<FDnaLocRow>& Rows);

	/** Reads a CSV with (at least) the `Namespace`, `Key`, `Source` and `Translation` columns; false, and why, when it has not. */
	DREAMNARRATIVECOMPILER_API bool ReadDnaLocCsv(const FString& Text, TArray<FDnaLocRow>& OutRows, FText& OutError);

	/** A localization target as the Localization Dashboard lays it out. */
	struct FDnaLocTarget
	{
		/** The folder holding `<Name>.manifest` and a folder per culture. */
		FString Directory;
		FString Name;
		/** Empty: the one the target's `.locmeta` names, when it has one. */
		FString NativeCulture;
	};

	/** The project's target called Name: `Content/Localization/<Name>`. */
	DREAMNARRATIVECOMPILER_API FDnaLocTarget GetDnaLocTarget(const FString& Name);

	/** Fills Translation from the target's archive for Culture where it has one for the same source; returns how many. */
	DREAMNARRATIVECOMPILER_API int32 FillDnaLocTranslations(const FDnaLocTarget& Target, const FString& Culture, TArray<FDnaLocRow>& Rows);

	/** What an import did. */
	struct FDnaLocImport
	{
		/** Written into the archive: new, or changed. */
		int32 Imported = 0;
		/** In the archive as they are already. */
		int32 Unchanged = 0;
		/** Rows with no translation. */
		int32 Empty = 0;
		/** Rows not imported, each with why: a key the target has not gathered, or a source changed since the export. */
		TArray<FString> Skipped;
	};

	/**
	 * Writes the rows' translations into the target's archive for Culture; the Localization Dashboard's
	 * Compile Text then makes them what the game shows. Only keys the target's manifest has (gather it
	 * first) are written, and only where the row's source is the one gathered: a line rewritten after the
	 * export keeps its old translation out. False, and why, when the target cannot be read or the archive
	 * cannot be written.
	 */
	DREAMNARRATIVECOMPILER_API bool ImportDnaLocTranslations(const FDnaLocTarget& Target, const FString& Culture, const TArray<FDnaLocRow>& Rows, FDnaLocImport& OutImport, FText& OutError);

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
