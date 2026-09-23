// Copyright DreamMoon. All Rights Reserved.

#include "Lang/DnaDiagnostic.h"

namespace UE::DreamNarrative::Lang
{
	const TCHAR* LexToString(EDnaSeverity Severity)
	{
		switch (Severity)
		{
		case EDnaSeverity::Error: return TEXT("error");
		case EDnaSeverity::Warning: return TEXT("warning");
		case EDnaSeverity::Info: return TEXT("info");
		}
		return TEXT("?");
	}

	FDnaDiagnosticSink::FDnaDiagnosticSink(FString InFilePath)
		: FilePath(MoveTemp(InFilePath))
	{
	}

	bool FDnaDiagnosticSink::Error(const TCHAR* Code, const FDnaSpan& Span, const FText& Message)
	{
		Add(EDnaSeverity::Error, Code, Span, Message);
		return false;
	}

	void FDnaDiagnosticSink::Warning(const TCHAR* Code, const FDnaSpan& Span, const FText& Message)
	{
		Add(EDnaSeverity::Warning, Code, Span, Message);
	}

	void FDnaDiagnosticSink::Info(const TCHAR* Code, const FDnaSpan& Span, const FText& Message)
	{
		Add(EDnaSeverity::Info, Code, Span, Message);
	}

	void FDnaDiagnosticSink::Add(EDnaSeverity Severity, const TCHAR* Code, const FDnaSpan& Span, const FText& Message)
	{
		FDnaDiagnostic& Diagnostic = Diagnostics.AddDefaulted_GetRef();
		Diagnostic.Code = Code;
		Diagnostic.Severity = Severity;
		Diagnostic.Message = Message;
		Diagnostic.Span = Span;
		Diagnostic.FilePath = FilePath;
		if (Severity == EDnaSeverity::Error)
		{
			++ErrorCount;
		}
	}

	void FDnaDiagnosticSink::Append(FDnaDiagnosticSink&& Other)
	{
		ErrorCount += Other.ErrorCount;
		Diagnostics.Append(MoveTemp(Other.Diagnostics));
		Other.Diagnostics.Reset();
		Other.ErrorCount = 0;
	}

	const FDnaDiagnostic* FDnaDiagnosticSink::FirstError() const
	{
		for (const FDnaDiagnostic& Diagnostic : Diagnostics)
		{
			if (Diagnostic.Severity == EDnaSeverity::Error)
			{
				return &Diagnostic;
			}
		}
		return nullptr;
	}

	FString FDnaDiagnosticSink::ToWireString(const FDnaDiagnostic& Diagnostic)
	{
		return FString::Printf(TEXT("%s: %s"), *Diagnostic.Code, *Diagnostic.Message.BuildSourceString());
	}

	FString FDnaDiagnosticSink::ToLogString(const FDnaDiagnostic& Diagnostic)
	{
		return FString::Printf(TEXT("%s(%d,%d): %s %s: %s"),
			*Diagnostic.FilePath, Diagnostic.Span.Line, Diagnostic.Span.Column,
			LexToString(Diagnostic.Severity), *Diagnostic.Code, *Diagnostic.Message.BuildSourceString());
	}
}
