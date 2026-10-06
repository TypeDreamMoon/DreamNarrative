// Copyright DreamMoon. All Rights Reserved.

#include "Lang/DnaPrinter.h"
#include "Lang/DnaLexer.h"

namespace UE::DreamNarrative::Lang
{
	// ---------------------------------------------------------------- the buffer

	FDnaSourceEdit::FDnaSourceEdit(const FDnaSourceText& Source, int32 InIndentUnit, TCHAR InIndentChar)
		: IndentUnit(InIndentUnit > 0 ? InIndentUnit : (InIndentChar == TEXT('\t') ? 1 : 2))
		, IndentChar(InIndentChar)
	{
		NewLine = Source.UsesCrLf() ? TEXT("\r\n") : TEXT("\n");
		const FString& Text = Source.GetText();
		const int32 Count = Source.GetLineCount();
		for (int32 Number = 1; Number <= Count; ++Number)
		{
			const FString LineText = Source.GetLineText(Number);
			const int32 Start = Source.GetLineStart(Number);
			const int32 Next = Number < Count ? Source.GetLineStart(Number + 1) : Text.Len();

			FLine& Line = Lines.AddDefaulted_GetRef();
			Line.Original = Number;
			Line.Terminator = Text.Mid(Start + LineText.Len(), Next - Start - LineText.Len());

			int32 IndentEnd = 0;
			while (IndentEnd < LineText.Len() && IsDnaSpace(LineText[IndentEnd])) { ++IndentEnd; }
			Line.Indent = LineText.Left(IndentEnd);
			const FString Rest = LineText.Mid(IndentEnd);
			const int32 CommentAt = FindDnaCommentStart(Rest);
			const FString Body = CommentAt == INDEX_NONE ? Rest : Rest.Left(CommentAt);
			int32 ContentLen = Body.Len();
			while (ContentLen > 0 && IsDnaSpace(Body[ContentLen - 1])) { --ContentLen; }
			Line.Content = Body.Left(ContentLen);
			Line.Tail = Rest.Mid(ContentLen);
		}
	}

	FDnaSourceEdit::FDnaSourceEdit(const FDnaDocument& Document)
		: FDnaSourceEdit(Document.Source, Document.Scan.IndentUnit, Document.Scan.IndentChar)
	{
	}

	FString FDnaSourceEdit::Print() const
	{
		FString Out;
		for (int32 Index = 0; Index < Lines.Num(); ++Index)
		{
			const FLine& Line = Lines[Index];
			Out += Line.Indent;
			Out += Line.Content;
			Out += Line.Tail;
			Out += Line.Terminator;
		}
		return Out;
	}

	int32 FDnaSourceEdit::IndexOf(int32 OriginalLine) const
	{
		return Lines.IndexOfByPredicate([OriginalLine](const FLine& Line) { return Line.Original == OriginalLine; });
	}

	FString FDnaSourceEdit::IndentFor(int32 Level) const
	{
		return FString::ChrN(FMath::Max(0, Level) * IndentUnit, IndentChar);
	}

	FString FDnaSourceEdit::GetContent(int32 OriginalLine) const
	{
		const int32 Index = IndexOf(OriginalLine);
		return Index == INDEX_NONE ? FString() : Lines[Index].Content;
	}

	bool FDnaSourceEdit::SetContent(int32 OriginalLine, const FString& Content)
	{
		const int32 Index = IndexOf(OriginalLine);
		if (Index == INDEX_NONE)
		{
			return false;
		}
		FLine& Line = Lines[Index];
		if (Line.Content != Content)
		{
			// A comment-only line given content keeps a space before its comment.
			if (Line.Content.IsEmpty() && !Line.Tail.IsEmpty() && !Content.IsEmpty() && !IsDnaSpace(Line.Tail[0]))
			{
				Line.Tail = TEXT(" ") + Line.Tail;
			}
			Line.Content = Content;
			bEdited = true;
		}
		return true;
	}

	void FDnaSourceEdit::InsertAt(int32 Index, const TArray<TPair<int32, FString>>& LevelsAndContents)
	{
		// Inserted at the very end of a text whose last line has no terminator: that line needs one now.
		if (Index == Lines.Num() && Lines.Num() > 0 && Lines.Last().Terminator.IsEmpty())
		{
			Lines.Last().Terminator = NewLine;
		}
		TArray<FLine> Fresh;
		for (const TPair<int32, FString>& Each : LevelsAndContents)
		{
			FLine& Line = Fresh.AddDefaulted_GetRef();
			Line.Indent = Each.Value.IsEmpty() ? FString() : IndentFor(Each.Key);
			Line.Content = Each.Value;
			Line.Terminator = NewLine;
		}
		Lines.Insert(Fresh, Index);
		bEdited |= Fresh.Num() > 0;
	}

	bool FDnaSourceEdit::InsertAfter(int32 OriginalLine, const TArray<TPair<int32, FString>>& LevelsAndContents)
	{
		int32 Index = 0;
		if (OriginalLine > 0)
		{
			const int32 At = IndexOf(OriginalLine);
			if (At == INDEX_NONE)
			{
				return false;
			}
			Index = At + 1;
			// After whatever was inserted there before.
			while (Index < Lines.Num() && Lines[Index].Original == 0) { ++Index; }
		}
		InsertAt(Index, LevelsAndContents);
		return true;
	}

	bool FDnaSourceEdit::InsertBefore(int32 OriginalLine, const TArray<TPair<int32, FString>>& LevelsAndContents)
	{
		const int32 At = IndexOf(OriginalLine);
		if (At == INDEX_NONE)
		{
			return false;
		}
		InsertAt(At, LevelsAndContents);
		return true;
	}

	bool FDnaSourceEdit::Remove(int32 FirstOriginalLine, int32 LastOriginalLine)
	{
		const int32 First = IndexOf(FirstOriginalLine);
		const int32 Last = IndexOf(LastOriginalLine);
		if (First == INDEX_NONE || Last == INDEX_NONE || Last < First)
		{
			return false;
		}
		// The text's last line may carry no terminator; whatever becomes last takes over its ending.
		const bool bRemovesEnd = Last == Lines.Num() - 1;
		const FString EndTerminator = Lines[Last].Terminator;
		Lines.RemoveAt(First, Last - First + 1);
		if (bRemovesEnd && Lines.Num() > 0)
		{
			Lines.Last().Terminator = EndTerminator;
		}
		bEdited = true;
		return true;
	}

	// ---------------------------------------------------------------- nodes

	void GetDnaNodeLineRange(const FDnaLineNode& Node, int32& OutFirst, int32& OutLast)
	{
		OutFirst = Node.LineNumber();
		OutLast = Node.LineNumber();
		TFunction<void(const FDnaLineNode&)> Walk = [&OutLast, &Walk](const FDnaLineNode& Each)
		{
			for (const FDnaLineNodePtr& Child : Each.Children)
			{
				if (!Child.IsValid())
				{
					continue;
				}
				// Floating lines after the last content line belong to whatever follows.
				if (!Child->IsFloating())
				{
					OutLast = FMath::Max(OutLast, Child->LineNumber());
				}
				Walk(*Child);
			}
		};
		Walk(Node);
	}

	// ---------------------------------------------------------------- expressions

	namespace
	{
		/** Binding strength: or 1, and 2, not 3, == != 4, < <= > >= in 5, + - 6, * / % 7, unary minus 8, the rest 9. */
		int32 Strength(const FDnaExpr& Expr)
		{
			switch (Expr.Kind)
			{
			case EDnaExprKind::Unary: return Expr.Op == EDnaExprOp::Not ? 3 : 8;
			case EDnaExprKind::In: return 5;
			case EDnaExprKind::Binary:
				switch (Expr.Op)
				{
				case EDnaExprOp::Or: return 1;
				case EDnaExprOp::And: return 2;
				case EDnaExprOp::Eq:
				case EDnaExprOp::Ne: return 4;
				case EDnaExprOp::Lt:
				case EDnaExprOp::Le:
				case EDnaExprOp::Gt:
				case EDnaExprOp::Ge: return 5;
				case EDnaExprOp::Add:
				case EDnaExprOp::Sub: return 6;
				default: return 7;
				}
			case EDnaExprKind::Literal:
				// A negative number binds like unary minus.
				return Expr.Literal.IsNumeric() && Expr.Literal.AsNumber() < 0.0 ? 8 : 9;
			default: return 9;
			}
		}

		FString FormatLiteral(const FDnaValue& Value)
		{
			if (Value.Type == EDnaFactType::Float)
			{
				// Shortest form that reads back as the same float; always with a point, so it stays a float.
				FString Text = FString::Printf(TEXT("%.9g"), Value.Float);
				for (int32 Digits = 1; Digits <= 9; ++Digits)
				{
					const FString Shorter = FString::Printf(TEXT("%.*g"), Digits, Value.Float);
					if (FCString::Atof(*Shorter) == Value.Float)
					{
						Text = Shorter;
						break;
					}
				}
				if (!Text.Contains(TEXT(".")) && !Text.Contains(TEXT("e")) && !Text.Contains(TEXT("n")))
				{
					Text += TEXT(".0");
				}
				return Text;
			}
			return Value.ToString();
		}

		FString Wrap(const FDnaExprPtr& Expr, int32 AtLeast, bool bStrict)
		{
			if (!Expr.IsValid())
			{
				return TEXT("?");
			}
			const FString Text = FormatDnaExpr(*Expr);
			const int32 Own = Strength(*Expr);
			return (bStrict ? Own <= AtLeast : Own < AtLeast) ? TEXT("(") + Text + TEXT(")") : Text;
		}
	}

	FString FormatDnaExpr(const FDnaExpr& Expr)
	{
		switch (Expr.Kind)
		{
		case EDnaExprKind::Literal: return FormatLiteral(Expr.Literal);
		case EDnaExprKind::Name: return Expr.Name;
		case EDnaExprKind::Chapter: return TEXT("chapter");
		case EDnaExprKind::Call:
		{
			TArray<FString> Parts;
			for (const FDnaExprPtr& Arg : Expr.Args) { Parts.Add(Arg.IsValid() ? FormatDnaExpr(*Arg) : TEXT("?")); }
			return FString::Printf(TEXT("%s(%s)"), *Expr.Name, *FString::Join(Parts, TEXT(", ")));
		}
		case EDnaExprKind::Unary:
		{
			const FDnaExprPtr Operand = Expr.Args.Num() > 0 ? Expr.Args[0] : nullptr;
			if (Expr.Op == EDnaExprOp::Not)
			{
				// `not` takes an equality or anything stronger as it is; another `not` too.
				return TEXT("not ") + Wrap(Operand, 3, /*bStrict=*/false);
			}
			return TEXT("-") + Wrap(Operand, 8, /*bStrict=*/false);
		}
		case EDnaExprKind::Binary:
		{
			const int32 Own = Strength(Expr);
			// Left-associative: the left operand may bind as tightly as this; the right one must bind tighter.
			// Comparisons do not chain: both sides must bind tighter.
			const bool bComparison = Own == 4 || Own == 5;
			return FString::Printf(TEXT("%s %s %s"),
				*Wrap(Expr.Args.Num() > 0 ? Expr.Args[0] : nullptr, Own, /*bStrict=*/bComparison),
				LexToString(Expr.Op),
				*Wrap(Expr.Args.Num() > 1 ? Expr.Args[1] : nullptr, Own, /*bStrict=*/true));
		}
		case EDnaExprKind::In:
		{
			TArray<FString> Members;
			for (int32 Index = 1; Index < Expr.Args.Num(); ++Index) { Members.Add(Expr.Args[Index].IsValid() ? FormatDnaExpr(*Expr.Args[Index]) : TEXT("?")); }
			return FString::Printf(TEXT("%s in (%s)"), *Wrap(Expr.Args.Num() > 0 ? Expr.Args[0] : nullptr, 5, /*bStrict=*/true), *FString::Join(Members, TEXT(", ")));
		}
		}
		return FString();
	}
}
