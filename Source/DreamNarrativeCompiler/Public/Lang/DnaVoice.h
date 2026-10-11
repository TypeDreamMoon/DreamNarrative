// Copyright DreamMoon. All Rights Reserved.
//
// Voice tables: line id -> voice, for characters whose declaration says `voice: table`. They are
// `*.voice.csv` files beside the sources (a header row names the columns; `LineId` and `Voice` are
// read, any others -- the ones `dnc vo-script` writes -- are left alone) and the DataTables the
// settings list (rows of FDnaVoiceRow, the row name being the line id).

#pragma once

#include "CoreMinimal.h"
#include "Lang/DnaDiagnostic.h"

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
}
