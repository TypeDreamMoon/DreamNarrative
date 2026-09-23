// Copyright DreamMoon. All Rights Reserved.
//
// Diagnostics for the DreamNarrative front end: a DNSnnnn code, a severity, a localisable message
// and a span. Raise helpers are METHODS on the sink and are spelled
// `Sink.Error(TEXT("DNSnnnn"), Span, LOCTEXT(...))` -- code first -- so a generator script can find
// every raise site by that shape. Do not wrap them in a helper that hides the literal code.

#pragma once

#include "CoreMinimal.h"
#include "Lang/DnaSource.h"

namespace UE::DreamNarrative::Lang
{
	enum class EDnaSeverity : uint8
	{
		Error,
		Warning,
		Info,
	};

	DREAMNARRATIVECOMPILER_API const TCHAR* LexToString(EDnaSeverity Severity);

	struct FDnaDiagnostic
	{
		/** DNSnnnn. Never empty. */
		FString Code;
		EDnaSeverity Severity = EDnaSeverity::Error;
		FText Message;
		FDnaSpan Span;
		FString FilePath;
	};

	/** Collects the diagnostics of one run. The parser keeps going after an error where it can, so
	 *  callers ask HasErrors() to decide whether the result is usable. */
	class DREAMNARRATIVECOMPILER_API FDnaDiagnosticSink
	{
	public:
		FDnaDiagnosticSink() = default;
		explicit FDnaDiagnosticSink(FString InFilePath);

		/** Records an error. Returns false so `return Sink.Error(...)` reads naturally. */
		bool Error(const TCHAR* Code, const FDnaSpan& Span, const FText& Message);
		void Warning(const TCHAR* Code, const FDnaSpan& Span, const FText& Message);
		void Info(const TCHAR* Code, const FDnaSpan& Span, const FText& Message);

		bool HasErrors() const { return ErrorCount > 0; }
		int32 NumErrors() const { return ErrorCount; }
		int32 Num() const { return Diagnostics.Num(); }
		const TArray<FDnaDiagnostic>& GetDiagnostics() const { return Diagnostics; }
		const FString& GetFilePath() const { return FilePath; }
		void SetFilePath(FString InFilePath) { FilePath = MoveTemp(InFilePath); }

		void Append(FDnaDiagnosticSink&& Other);
		const FDnaDiagnostic* FirstError() const;

		/** `DNSnnnn: <message>` with the message in its invariant (source) form. */
		static FString ToWireString(const FDnaDiagnostic& Diagnostic);
		/** `<file>(<line>,<col>): error DNSnnnn: <message>` -- the log form. */
		static FString ToLogString(const FDnaDiagnostic& Diagnostic);

	private:
		void Add(EDnaSeverity Severity, const TCHAR* Code, const FDnaSpan& Span, const FText& Message);

		FString FilePath;
		TArray<FDnaDiagnostic> Diagnostics;
		int32 ErrorCount = 0;
	};
}
