// Copyright DreamMoon. All Rights Reserved.
//
// Source text in, concrete syntax tree out. The parser classifies every line by its first token in
// the order proposal §4 gives, parses the line's own meaning, and builds the block structure from
// indentation. It does not resolve names: a speaker that is not a declared character, a fact nobody
// declared, a jump to a node that does not exist are the validator's (DNS3xxx) to report.
//
// One exception is deliberate. The `if` modifier at the end of a text line is only a modifier when
// its expression parses AND refers to a name this file (or the project) declares; otherwise the
// words are text. That rule needs the declared names, so the parser collects them in a pre-pass
// and accepts extra ones from the caller.

#pragma once

#include "CoreMinimal.h"
#include "Lang/DnaDiagnostic.h"
#include "Lang/DnaSyntax.h"

namespace UE::DreamNarrative::Lang
{
	struct FDnaParseOptions
	{
		/** Names declared by other files of the project, for the text-tail `if` rule. May be null. */
		const TSet<FString>* ExternalNames = nullptr;
	};

	/**
	 * Parses one source text. Returns false when any error was recorded; the document is still
	 * filled as far as the parser got, so later passes can report more.
	 *
	 *   DNS2001  a line indented under one that does not open a block
	 *   DNS2002  a block header with nothing under it
	 *   DNS2003  a line that does not belong where it is
	 *   DNS2004  a modifier repeated on one line
	 *   DNS2005  a choice line outside a dialogue
	 *   DNS2006  (info) ` if ` in a text line was treated as text
	 *   DNS2007  expected X, found Y
	 *   DNS2008  trailing tokens after a complete line or expression
	 *   DNS2009  a modifier that is not allowed on this kind of line
	 */
	DREAMNARRATIVECOMPILER_API bool ParseDnaDocument(const FDnaSourceText& Source, const FDnaParseOptions& Options, FDnaDocument& OutDocument, FDnaDiagnosticSink& Diagnostics);

	/** Reads the file (DNS9002 when it cannot) and parses it. */
	DREAMNARRATIVECOMPILER_API bool ParseDnaFile(const FString& FilePath, const FDnaParseOptions& Options, FDnaDocument& OutDocument, FDnaDiagnosticSink& Diagnostics);

	/** The names a source declares at top level, without a full parse. Used to seed ExternalNames. */
	DREAMNARRATIVECOMPILER_API void CollectDnaDeclaredNames(const FDnaSourceText& Source, TSet<FString>& OutNames);

	/** Same, from lines that were already scanned. */
	DREAMNARRATIVECOMPILER_API void CollectDnaDeclaredNamesFromScan(const FDnaScanResult& Scan, TSet<FString>& OutNames);
}
