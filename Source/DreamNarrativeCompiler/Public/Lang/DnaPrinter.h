// Copyright DreamMoon. All Rights Reserved.
//
// The printer: writes edits back into a source text without disturbing anything else.
//
// A source is kept as its physical lines, each with its own terminator. A line nobody touched is
// printed exactly as it was read -- indentation, trailing comment, CRLF or LF -- so a document that
// was not edited prints back byte for byte. An edited line keeps its indentation, its comment and the
// space before the comment; only its content is new. A new line takes the indentation the file uses
// (its unit and character) at the level asked for, and the file's line ending.
//
// Lines are addressed by the line number they had when the source was read; lines inserted since
// are reached through the line they were inserted next to. Content is written as text: an edit that
// changes a condition hands in the condition as written, and the caller parses the result to check it.

#pragma once

#include "CoreMinimal.h"
#include "Lang/DnaExpr.h"
#include "Lang/DnaSyntax.h"

namespace UE::DreamNarrative::Lang
{
	class DREAMNARRATIVECOMPILER_API FDnaSourceEdit
	{
	public:
		/** The lines of Source; IndentUnit and IndentChar as the scanner found them (unit 0: the file indents nothing yet). */
		FDnaSourceEdit(const FDnaSourceText& Source, int32 IndentUnit, TCHAR IndentChar);

		/** The lines of a parsed document. */
		explicit FDnaSourceEdit(const FDnaDocument& Document);

		/** The text as it stands now. */
		FString Print() const;

		/** True when some edit has been made. */
		bool IsEdited() const { return bEdited; }

		/** How many lines there are now. */
		int32 Num() const { return Lines.Num(); }

		// -------------------------------------------------------------- edits

		/** Replaces the content of an original line, keeping its indentation and comment. False when the line is gone or never was. */
		bool SetContent(int32 OriginalLine, const FString& Content);

		/**
		 * Inserts lines after an original line (0: before the first), each at its own indentation
		 * level; they go after anything inserted there before. Returns false when the line is gone.
		 */
		bool InsertAfter(int32 OriginalLine, const TArray<TPair<int32, FString>>& LevelsAndContents);

		/** Inserts lines before an original line. */
		bool InsertBefore(int32 OriginalLine, const TArray<TPair<int32, FString>>& LevelsAndContents);

		/** Removes original lines First..Last, inclusive, and whatever was inserted between them. */
		bool Remove(int32 FirstOriginalLine, int32 LastOriginalLine);

		// -------------------------------------------------------------- looking

		/** The content of an original line as it stands now; empty when it is gone. */
		FString GetContent(int32 OriginalLine) const;

		/** Indentation text for a level, the way this file indents. */
		FString IndentFor(int32 Level) const;

	private:
		struct FLine
		{
			/** The original line number; 0 for a line inserted since. */
			int32 Original = 0;
			FString Indent;
			FString Content;
			/** What follows the content: the space before the comment and the comment, as read. */
			FString Tail;
			FString Terminator;
		};

		int32 IndexOf(int32 OriginalLine) const;
		void InsertAt(int32 Index, const TArray<TPair<int32, FString>>& LevelsAndContents);

		TArray<FLine> Lines;
		FString NewLine;
		int32 IndentUnit = 2;
		TCHAR IndentChar = TEXT(' ');
		bool bEdited = false;
	};

	/** The first and last line a node covers: its own and every line under it, floating lines between them included. */
	DREAMNARRATIVECOMPILER_API void GetDnaNodeLineRange(const FDnaLineNode& Node, int32& OutFirst, int32& OutLast);

	/**
	 * An expression as source, with only the parentheses its structure needs: `not 已会合 and 好感 > 3`.
	 * Percentages print as their fraction (`chance(0.3)`), which means the same.
	 */
	DREAMNARRATIVECOMPILER_API FString FormatDnaExpr(const FDnaExpr& Expr);
}
