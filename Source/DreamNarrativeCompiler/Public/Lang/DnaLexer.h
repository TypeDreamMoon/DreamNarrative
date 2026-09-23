// Copyright DreamMoon. All Rights Reserved.
//
// The lexical layer of a line-oriented grammar has two halves.
//
// The LINE SCANNER cuts the source into physical lines, measures indentation, splits the trailing
// `//` comment off, and validates the indentation policy (DNS1001-1003). It never looks inside a
// line: a line of dialogue is arbitrary text and tokenising it would be wrong.
//
// The SEGMENT TOKENIZER cuts one span of non-text source -- a command line, an expression, the tail
// of a speech line -- into tokens. Full-width punctuation is folded to ASCII here (`:` `,` `()`
// `<>` `=` `!` `+-*/%` and the Chinese quote pairs), so nothing above this layer ever sees it.
// Identifiers are Unicode: an ASCII letter, `_`, or any non-ASCII character that is not
// whitespace or punctuation starts one.

#pragma once

#include "CoreMinimal.h"
#include "Lang/DnaDiagnostic.h"
#include "Lang/DnaSource.h"

namespace UE::DreamNarrative::Lang
{
	// ---------------------------------------------------------------- characters

	/** Full-width punctuation folded to its ASCII form; any other character unchanged. */
	DREAMNARRATIVECOMPILER_API TCHAR NormalizeDnaPunctuation(TCHAR Char);

	DREAMNARRATIVECOMPILER_API bool IsDnaIdentifierStart(TCHAR Char);
	DREAMNARRATIVECOMPILER_API bool IsDnaIdentifierChar(TCHAR Char);

	/** Space, tab, or the full-width space U+3000. */
	DREAMNARRATIVECOMPILER_API bool IsDnaSpace(TCHAR Char);

	/** True for every word the language reserves at line start or in expressions (Architecture §3, proposal §3.4). */
	DREAMNARRATIVECOMPILER_API bool IsDnaReservedWord(const FString& Word);

	// ---------------------------------------------------------------- lines

	/** One physical line after the scanner. */
	struct FDnaRawLine
	{
		/** 1-based. */
		int32 LineNumber = 1;
		/** In indent units; -1 for blank and comment-only lines, whose indentation is not validated. */
		int32 IndentLevel = 0;
		/** The literal leading whitespace, kept for the printer. */
		FString IndentText;
		/** The text between the indentation and the comment, right-trimmed. */
		FString Content;
		/** Everything after `//`, including the slashes. Empty when the line has no comment. */
		FString Comment;
		/** Span of Content. Empty for a blank line. */
		FDnaSpan Span;
		/** Span of the whole line without its terminator. */
		FDnaSpan LineSpan;

		bool IsBlank() const { return Content.IsEmpty() && Comment.IsEmpty(); }
		bool IsCommentOnly() const { return Content.IsEmpty() && !Comment.IsEmpty(); }
		bool IsFloating() const { return Content.IsEmpty(); }
	};

	struct FDnaScanResult
	{
		TArray<FDnaRawLine> Lines;
		/** Characters per indent level as inferred from the first indented line; 0 when nothing is indented. */
		int32 IndentUnit = 0;
		/** The indentation character the file uses, ' ' or '\t'. */
		TCHAR IndentChar = TEXT(' ');
	};

	/**
	 * Scans a whole source into lines.
	 *
	 *   DNS1001  a full-width space (U+3000) in the indentation
	 *   DNS1002  tabs and spaces mixed, in one line or across the file
	 *   DNS1003  an indentation that is not a whole number of units
	 *
	 * Returns false when any error was recorded. Lines are always produced, so the parser can keep
	 * going and report more.
	 */
	DREAMNARRATIVECOMPILER_API bool ScanDnaLines(const FDnaSourceText& Source, FDnaScanResult& Out, FDnaDiagnosticSink& Diagnostics);

	/** Position of the first `//` in Text that is not inside a string literal; INDEX_NONE when none. */
	DREAMNARRATIVECOMPILER_API int32 FindDnaCommentStart(const FString& Text);

	// ---------------------------------------------------------------- tokens

	enum class EDnaTokenKind : uint8
	{
		EndOfInput,
		Identifier,
		Keyword,
		Int,
		Float,
		/** A number with a time unit; Real holds seconds. */
		Duration,
		/** A number followed by `%`; Real holds the number as written (30 for `30%`). */
		Percent,
		/** `"..."`, `“...”` or `「...」`; Text holds the content. */
		String,
		LeftParen, RightParen, Comma, Colon, Dot, DotDot, Hash, At, Arrow,
		Plus, Minus, Star, Slash, PercentSign,
		Assign, PlusAssign, MinusAssign,
		EqualEqual, BangEqual, Less, LessEqual, Greater, GreaterEqual,
		Bang, AmpAmp, PipePipe,
		Unknown,
	};

	/** The words that mean something inside an expression or a modifier tail. Block keywords are
	 *  recognised by the parser from their spelling and do not need an enum. */
	enum class EDnaKeyword : uint8
	{
		None,
		And, Or, Not, True, False, In,
		If, Once, For,
		Chapter,
	};

	struct FDnaToken
	{
		EDnaTokenKind Kind = EDnaTokenKind::EndOfInput;
		EDnaKeyword Keyword = EDnaKeyword::None;
		FString Text;
		int64 Integer = 0;
		double Real = 0.0;
		FDnaSpan Span;

		bool Is(EDnaTokenKind InKind) const { return Kind == InKind; }
		bool IsKeyword(EDnaKeyword InKeyword) const { return Kind == EDnaTokenKind::Keyword && Keyword == InKeyword; }
		bool IsIdentifier(const TCHAR* Name) const { return Kind == EDnaTokenKind::Identifier && Text.Equals(Name, ESearchCase::CaseSensitive); }
		bool IsWord(const TCHAR* Name) const { return (Kind == EDnaTokenKind::Identifier || Kind == EDnaTokenKind::Keyword) && Text.Equals(Name, ESearchCase::CaseSensitive); }
	};

	DREAMNARRATIVECOMPILER_API const TCHAR* LexToString(EDnaTokenKind Kind);
	DREAMNARRATIVECOMPILER_API bool TryGetDnaKeyword(const FString& Word, EDnaKeyword& OutKeyword);

	/**
	 * Tokenises [Offset, Offset + Length) of the source. The array always ends with EndOfInput.
	 *
	 *   DNS1004  unterminated string literal
	 *   DNS1005  a character the tokenizer cannot place (emitted as Unknown so the parser fails there)
	 */
	DREAMNARRATIVECOMPILER_API void TokenizeDnaSegment(const FDnaSourceText& Source, int32 Offset, int32 Length, TArray<FDnaToken>& OutTokens, FDnaDiagnosticSink& Diagnostics);
}
