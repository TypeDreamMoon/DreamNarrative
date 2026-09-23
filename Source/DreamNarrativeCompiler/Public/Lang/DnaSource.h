// Copyright DreamMoon. All Rights Reserved.
//
// Source text and positions for the DreamNarrative front end. Everything the scanner, the parser and
// the diagnostics say about "where" is an FDnaSpan: a TCHAR range into one source text with the
// 1-based line and column of its start already resolved.

#pragma once

#include "CoreMinimal.h"

namespace UE::DreamNarrative::Lang
{
	/** A half-open range [Offset, Offset + Length) into one FDnaSourceText, plus the 1-based line and
	 *  column of Offset. Line and column are cached because a diagnostic outlives the parse. */
	struct FDnaSpan
	{
		int32 Offset = 0;
		int32 Length = 0;
		int32 Line = 1;
		int32 Column = 1;

		int32 End() const { return Offset + Length; }
		bool IsEmpty() const { return Length <= 0; }

		static FDnaSpan Join(const FDnaSpan& A, const FDnaSpan& B)
		{
			const FDnaSpan& First = (B.Offset < A.Offset) ? B : A;
			FDnaSpan Result;
			Result.Offset = First.Offset;
			Result.Length = FMath::Max(A.End(), B.End()) - Result.Offset;
			Result.Line = First.Line;
			Result.Column = First.Column;
			return Result;
		}
	};

	/** One source text with its line table. Line terminators are `\n`, `\r\n` and a lone `\r`. */
	class DREAMNARRATIVECOMPILER_API FDnaSourceText
	{
	public:
		FDnaSourceText() = default;
		FDnaSourceText(FString InPath, FString InText);

		const FString& GetPath() const { return Path; }
		const FString& GetText() const { return Text; }
		int32 Len() const { return Text.Len(); }
		int32 GetLineCount() const { return LineStarts.Num(); }

		/** True when the text uses `\r\n` for at least one line break. The printer keeps the style. */
		bool UsesCrLf() const { return bUsesCrLf; }

		/** 1-based line and column of an offset; an offset past the end maps to the last position. */
		void GetLineAndColumn(int32 Offset, int32& OutLine, int32& OutColumn) const;

		/** Offset of the first TCHAR of a 1-based line; INDEX_NONE out of range. */
		int32 GetLineStart(int32 Line) const;

		/** The text of a 1-based line, without its terminator. Empty for a line out of range. */
		FString GetLineText(int32 Line) const;

		FDnaSpan MakeSpan(int32 Offset, int32 Length) const;
		FString Slice(const FDnaSpan& Span) const;

	private:
		FString Path;
		FString Text;
		TArray<int32> LineStarts;
		bool bUsesCrLf = false;
	};
}
