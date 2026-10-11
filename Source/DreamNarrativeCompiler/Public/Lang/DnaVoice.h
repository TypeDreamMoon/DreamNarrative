// Copyright DreamMoon. All Rights Reserved.
//
// Voice tables: line id -> voice, for characters whose declaration says `voice: table`. They are
// `*.voice.csv` files beside the sources (a header row names the columns; `LineId` and `Voice` are
// read, any others -- the ones `dnc vo-script` writes -- are left alone) and the DataTables the
// settings list (rows of FDnaVoiceRow, the row name being the line id).

#pragma once

#include "CoreMinimal.h"
#include "DreamNarrativeVoice.h"
#include "Lang/DnaDiagnostic.h"

class UDreamNarrativeScript;

namespace UE::DreamNarrative::Lang
{
	/** What one line id answers to in a voice table, and where that was written. */
	struct FDnaVoiceEntry
	{
		/** A content path, or an asset name looked for under the default voice root. */
		FString Voice;
		/** The CSV file or DataTable it came from. */
		FString Source;
		/** Its row in a CSV file, counting from 1; 0 for a DataTable. */
		int32 Line = 0;
	};

	/** Every `*.voice.csv` under Root, sorted. */
	DREAMNARRATIVECOMPILER_API void FindDnaVoiceCsvFiles(const FString& Root, TArray<FString>& OutFiles);

	/**
	 * Reads voice tables into line id -> entry: the CSV texts (path -> text) and, when bDataTables, the
	 * DataTables the settings list. Rows with no voice are skipped (a recording script not recorded
	 * yet). A line id answered twice, in one table or across two, is DNS6005; a table that cannot be
	 * read is DNS9002.
	 */
	DREAMNARRATIVECOMPILER_API void ReadDnaVoiceTables(const TMap<FString, FString>& CsvTexts, bool bDataTables, TMap<FString, FDnaVoiceEntry>& OutEntries, FDnaDiagnosticSink& Diagnostics);

	/** The asset name the convention looks for: the line id with its dots written as underscores. */
	DREAMNARRATIVECOMPILER_API FString GetDnaVoiceAssetName(const FString& LineId);

	/** Splits one CSV record into its fields: commas, double quotes, `""` inside quotes. */
	DREAMNARRATIVECOMPILER_API void SplitDnaCsvRecord(const FString& Record, TArray<FString>& OutFields);

	/** One CSV field, quoted when it has to be. */
	DREAMNARRATIVECOMPILER_API FString QuoteDnaCsvField(const FString& Field);

	class FDnaProgram;

	/**
	 * How a speaker's lines find their voices: its declaration's `voice:` (a root, or `table`), else the
	 * project's default. OutRoot is the convention's root: the declared one, else `<default root>/<speaker>`
	 * (`<default root>` for narration, Speaker None).
	 */
	DREAMNARRATIVECOMPILER_API EDnaVoiceLookup GetDnaVoiceLookup(const FDnaProgram& Program, FName Speaker, FString& OutRoot);

	/** One row of a recording script: a line, who says it, what it says, and where its voice stands. */
	struct FDnaVoiceScriptRow
	{
		FString LineId;
		/** The voice found, as a content path; empty when none was. */
		FString Voice;
		/** The speaker; empty for narration. */
		FString Character;
		FString Text;
		FString Dialogue;
		/** The line before it in the same dialogue, for the actor's sake. */
		FString Previous;
		/** Speaker tags (`老周(皱眉)`), space separated. */
		FString Tags;
		/** `for` seconds; 0 when the line has none. */
		float For = 0.f;
		/** `found`, `missing`, or `none` (the speaker's lines look for no voice). */
		FString Status;
		/** Under the convention: the asset the voice is looked for as. */
		FString Expected;
		FString Source;
		int32 SourceLine = 0;
	};

	/** The recording script of the compiled scripts: every spoken or narrated line, by character, in the order the dialogues have them. */
	DREAMNARRATIVECOMPILER_API void CollectDnaVoiceScript(const FDnaProgram& Program, const TArray<UDreamNarrativeScript*>& Scripts, TArray<FDnaVoiceScriptRow>& OutRows);

	/** The rows as CSV, `LineId` and `Voice` first: filled in, the same file reads back as a voice table. */
	DREAMNARRATIVECOMPILER_API FString WriteDnaVoiceScript(const TArray<FDnaVoiceScriptRow>& Rows);
}
