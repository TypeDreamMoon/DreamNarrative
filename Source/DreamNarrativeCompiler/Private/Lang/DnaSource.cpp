// Copyright DreamMoon. All Rights Reserved.

#include "Lang/DnaSource.h"

namespace UE::DreamNarrative::Lang
{
	FDnaSourceText::FDnaSourceText(FString InPath, FString InText)
		: Path(MoveTemp(InPath))
		, Text(MoveTemp(InText))
	{
		LineStarts.Add(0);
		const int32 Num = Text.Len();
		for (int32 Index = 0; Index < Num; ++Index)
		{
			const TCHAR Char = Text[Index];
			if (Char == TEXT('\n'))
			{
				LineStarts.Add(Index + 1);
			}
			else if (Char == TEXT('\r'))
			{
				if (Index + 1 < Num && Text[Index + 1] == TEXT('\n'))
				{
					bUsesCrLf = true;
					++Index;
				}
				LineStarts.Add(Index + 1);
			}
		}
	}

	void FDnaSourceText::GetLineAndColumn(int32 Offset, int32& OutLine, int32& OutColumn) const
	{
		Offset = FMath::Clamp(Offset, 0, Text.Len());
		int32 Low = 0;
		int32 High = LineStarts.Num() - 1;
		while (Low < High)
		{
			const int32 Mid = (Low + High + 1) / 2;
			if (LineStarts[Mid] <= Offset)
			{
				Low = Mid;
			}
			else
			{
				High = Mid - 1;
			}
		}
		OutLine = Low + 1;
		OutColumn = Offset - LineStarts[Low] + 1;
	}

	int32 FDnaSourceText::GetLineStart(int32 Line) const
	{
		return LineStarts.IsValidIndex(Line - 1) ? LineStarts[Line - 1] : INDEX_NONE;
	}

	FString FDnaSourceText::GetLineText(int32 Line) const
	{
		if (!LineStarts.IsValidIndex(Line - 1))
		{
			return FString();
		}
		const int32 Start = LineStarts[Line - 1];
		int32 End = LineStarts.IsValidIndex(Line) ? LineStarts[Line] : Text.Len();
		while (End > Start && (Text[End - 1] == TEXT('\n') || Text[End - 1] == TEXT('\r')))
		{
			--End;
		}
		return Text.Mid(Start, End - Start);
	}

	FDnaSpan FDnaSourceText::MakeSpan(int32 Offset, int32 Length) const
	{
		FDnaSpan Span;
		Span.Offset = Offset;
		Span.Length = Length;
		GetLineAndColumn(Offset, Span.Line, Span.Column);
		return Span;
	}

	FString FDnaSourceText::Slice(const FDnaSpan& Span) const
	{
		if (Span.Offset < 0 || Span.Offset >= Text.Len() || Span.Length <= 0)
		{
			return FString();
		}
		return Text.Mid(Span.Offset, FMath::Min(Span.Length, Text.Len() - Span.Offset));
	}
}
