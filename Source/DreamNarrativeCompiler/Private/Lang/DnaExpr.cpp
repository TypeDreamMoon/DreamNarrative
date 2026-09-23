// Copyright DreamMoon. All Rights Reserved.

#include "Lang/DnaExpr.h"

#define LOCTEXT_NAMESPACE "DreamNarrativeLang"

namespace UE::DreamNarrative::Lang
{
	const TCHAR* LexToString(EDnaExprOp Op)
	{
		switch (Op)
		{
		case EDnaExprOp::None: return TEXT("");
		case EDnaExprOp::Or: return TEXT("or");
		case EDnaExprOp::And: return TEXT("and");
		case EDnaExprOp::Not: return TEXT("not");
		case EDnaExprOp::Eq: return TEXT("==");
		case EDnaExprOp::Ne: return TEXT("!=");
		case EDnaExprOp::Lt: return TEXT("<");
		case EDnaExprOp::Le: return TEXT("<=");
		case EDnaExprOp::Gt: return TEXT(">");
		case EDnaExprOp::Ge: return TEXT(">=");
		case EDnaExprOp::Add: return TEXT("+");
		case EDnaExprOp::Sub: return TEXT("-");
		case EDnaExprOp::Mul: return TEXT("*");
		case EDnaExprOp::Div: return TEXT("/");
		case EDnaExprOp::Mod: return TEXT("%");
		case EDnaExprOp::Neg: return TEXT("-");
		}
		return TEXT("?");
	}

	bool FDnaExpr::RefersToWorld() const
	{
		if (Kind == EDnaExprKind::Name || Kind == EDnaExprKind::Chapter || Kind == EDnaExprKind::Call)
		{
			return true;
		}
		for (const FDnaExprPtr& Arg : Args)
		{
			if (Arg.IsValid() && Arg->RefersToWorld())
			{
				return true;
			}
		}
		return false;
	}

	void FDnaExpr::CollectNames(TArray<const FDnaExpr*>& OutNames) const
	{
		if (Kind == EDnaExprKind::Name)
		{
			OutNames.Add(this);
		}
		for (const FDnaExprPtr& Arg : Args)
		{
			if (Arg.IsValid())
			{
				Arg->CollectNames(OutNames);
			}
		}
	}

	FString FDnaExpr::ToString() const
	{
		switch (Kind)
		{
		case EDnaExprKind::Literal: return Literal.ToString();
		case EDnaExprKind::Name: return Name;
		case EDnaExprKind::Chapter: return TEXT("chapter");
		case EDnaExprKind::Call:
		{
			TArray<FString> Parts;
			for (const FDnaExprPtr& Arg : Args) { Parts.Add(Arg.IsValid() ? Arg->ToString() : TEXT("?")); }
			return FString::Printf(TEXT("%s(%s)"), *Name, *FString::Join(Parts, TEXT(", ")));
		}
		case EDnaExprKind::Unary:
			return FString::Printf(TEXT("(%s %s)"), LexToString(Op), Args.Num() > 0 && Args[0].IsValid() ? *Args[0]->ToString() : TEXT("?"));
		case EDnaExprKind::Binary:
			return FString::Printf(TEXT("(%s %s %s)"),
				Args.Num() > 0 && Args[0].IsValid() ? *Args[0]->ToString() : TEXT("?"), LexToString(Op),
				Args.Num() > 1 && Args[1].IsValid() ? *Args[1]->ToString() : TEXT("?"));
		case EDnaExprKind::In:
		{
			TArray<FString> Parts;
			for (int32 Index = 1; Index < Args.Num(); ++Index) { Parts.Add(Args[Index].IsValid() ? Args[Index]->ToString() : TEXT("?")); }
			return FString::Printf(TEXT("(%s in (%s))"), Args.Num() > 0 && Args[0].IsValid() ? *Args[0]->ToString() : TEXT("?"), *FString::Join(Parts, TEXT(", ")));
		}
		}
		return FString();
	}

	// ---------------------------------------------------------------- parser

	namespace
	{
		class FExprParser
		{
		public:
			FExprParser(const TArray<FDnaToken>& InTokens, int32& InIndex, FDnaDiagnosticSink& InDiagnostics)
				: Tokens(InTokens), Index(InIndex), Diagnostics(InDiagnostics)
			{
			}

			FDnaExprPtr ParseOr()
			{
				FDnaExprPtr Left = ParseAnd();
				while (Left.IsValid() && (Peek().IsKeyword(EDnaKeyword::Or) || Peek().Is(EDnaTokenKind::PipePipe)))
				{
					const FDnaToken OpToken = Take();
					FDnaExprPtr Right = ParseAnd();
					if (!Right.IsValid()) { return nullptr; }
					Left = MakeBinary(EDnaExprOp::Or, Left, Right);
				}
				return Left;
			}

		private:
			const TArray<FDnaToken>& Tokens;
			int32& Index;
			FDnaDiagnosticSink& Diagnostics;

			const FDnaToken& Peek(int32 Ahead = 0) const
			{
				const int32 At = FMath::Min(Index + Ahead, Tokens.Num() - 1);
				return Tokens[At];
			}

			const FDnaToken& Take()
			{
				const FDnaToken& Token = Tokens[FMath::Min(Index, Tokens.Num() - 1)];
				if (Index < Tokens.Num() - 1) { ++Index; }
				return Token;
			}

			FDnaExprPtr Fail(const FDnaToken& At, const FText& Expected)
			{
				Diagnostics.Error(TEXT("DNS2007"), At.Span,
					FText::Format(LOCTEXT("ExprExpected", "Expected {0}, found {1}"), Expected,
						FText::FromString(At.Kind == EDnaTokenKind::EndOfInput ? TEXT("end of line") : At.Text)));
				return nullptr;
			}

			static FDnaExprPtr MakeBinary(EDnaExprOp Op, const FDnaExprPtr& Left, const FDnaExprPtr& Right)
			{
				FDnaExprPtr Node = MakeShared<FDnaExpr>();
				Node->Kind = EDnaExprKind::Binary;
				Node->Op = Op;
				Node->Span = FDnaSpan::Join(Left->Span, Right->Span);
				Node->Args = { Left, Right };
				return Node;
			}

			FDnaExprPtr ParseAnd()
			{
				FDnaExprPtr Left = ParseNot();
				while (Left.IsValid() && (Peek().IsKeyword(EDnaKeyword::And) || Peek().Is(EDnaTokenKind::AmpAmp)))
				{
					Take();
					FDnaExprPtr Right = ParseNot();
					if (!Right.IsValid()) { return nullptr; }
					Left = MakeBinary(EDnaExprOp::And, Left, Right);
				}
				return Left;
			}

			FDnaExprPtr ParseNot()
			{
				if (Peek().IsKeyword(EDnaKeyword::Not) || Peek().Is(EDnaTokenKind::Bang))
				{
					const FDnaToken OpToken = Take();
					FDnaExprPtr Operand = ParseNot();
					if (!Operand.IsValid()) { return nullptr; }
					FDnaExprPtr Node = MakeShared<FDnaExpr>();
					Node->Kind = EDnaExprKind::Unary;
					Node->Op = EDnaExprOp::Not;
					Node->Span = FDnaSpan::Join(OpToken.Span, Operand->Span);
					Node->Args = { Operand };
					return Node;
				}
				return ParseEquality();
			}

			FDnaExprPtr ParseEquality()
			{
				FDnaExprPtr Left = ParseRelational();
				while (Left.IsValid())
				{
					EDnaExprOp Op = EDnaExprOp::None;
					if (Peek().Is(EDnaTokenKind::EqualEqual)) { Op = EDnaExprOp::Eq; }
					else if (Peek().Is(EDnaTokenKind::BangEqual)) { Op = EDnaExprOp::Ne; }
					else { break; }
					Take();
					FDnaExprPtr Right = ParseRelational();
					if (!Right.IsValid()) { return nullptr; }
					Left = MakeBinary(Op, Left, Right);
				}
				return Left;
			}

			FDnaExprPtr ParseRelational()
			{
				FDnaExprPtr Left = ParseAdditive();
				while (Left.IsValid())
				{
					if (Peek().IsKeyword(EDnaKeyword::In))
					{
						Take();
						if (!Peek().Is(EDnaTokenKind::LeftParen))
						{
							return Fail(Peek(), LOCTEXT("ExpectedInList", "'(' after 'in'"));
						}
						Take();
						FDnaExprPtr Node = MakeShared<FDnaExpr>();
						Node->Kind = EDnaExprKind::In;
						Node->Args.Add(Left);
						while (true)
						{
							FDnaExprPtr Member = ParseOr();
							if (!Member.IsValid()) { return nullptr; }
							Node->Args.Add(Member);
							if (Peek().Is(EDnaTokenKind::Comma)) { Take(); continue; }
							if (Peek().Is(EDnaTokenKind::RightParen)) { break; }
							return Fail(Peek(), LOCTEXT("ExpectedCommaOrParen", "',' or ')'"));
						}
						const FDnaToken& Close = Take();
						Node->Span = FDnaSpan::Join(Left->Span, Close.Span);
						Left = Node;
						continue;
					}

					EDnaExprOp Op = EDnaExprOp::None;
					if (Peek().Is(EDnaTokenKind::Less)) { Op = EDnaExprOp::Lt; }
					else if (Peek().Is(EDnaTokenKind::LessEqual)) { Op = EDnaExprOp::Le; }
					else if (Peek().Is(EDnaTokenKind::Greater)) { Op = EDnaExprOp::Gt; }
					else if (Peek().Is(EDnaTokenKind::GreaterEqual)) { Op = EDnaExprOp::Ge; }
					else { break; }
					Take();
					FDnaExprPtr Right = ParseAdditive();
					if (!Right.IsValid()) { return nullptr; }
					Left = MakeBinary(Op, Left, Right);
				}
				return Left;
			}

			FDnaExprPtr ParseAdditive()
			{
				FDnaExprPtr Left = ParseTerm();
				while (Left.IsValid())
				{
					EDnaExprOp Op = EDnaExprOp::None;
					if (Peek().Is(EDnaTokenKind::Plus)) { Op = EDnaExprOp::Add; }
					else if (Peek().Is(EDnaTokenKind::Minus)) { Op = EDnaExprOp::Sub; }
					else { break; }
					Take();
					FDnaExprPtr Right = ParseTerm();
					if (!Right.IsValid()) { return nullptr; }
					Left = MakeBinary(Op, Left, Right);
				}
				return Left;
			}

			FDnaExprPtr ParseTerm()
			{
				FDnaExprPtr Left = ParseUnary();
				while (Left.IsValid())
				{
					EDnaExprOp Op = EDnaExprOp::None;
					if (Peek().Is(EDnaTokenKind::Star)) { Op = EDnaExprOp::Mul; }
					else if (Peek().Is(EDnaTokenKind::Slash)) { Op = EDnaExprOp::Div; }
					else if (Peek().Is(EDnaTokenKind::PercentSign)) { Op = EDnaExprOp::Mod; }
					else { break; }
					Take();
					FDnaExprPtr Right = ParseUnary();
					if (!Right.IsValid()) { return nullptr; }
					Left = MakeBinary(Op, Left, Right);
				}
				return Left;
			}

			FDnaExprPtr ParseUnary()
			{
				if (Peek().Is(EDnaTokenKind::Minus))
				{
					const FDnaToken OpToken = Take();
					FDnaExprPtr Operand = ParseUnary();
					if (!Operand.IsValid()) { return nullptr; }
					// Fold a negated literal so `-3` is a constant, not an operation.
					if (Operand->Kind == EDnaExprKind::Literal && Operand->Literal.IsNumeric())
					{
						if (Operand->Literal.Type == EDnaFactType::Int) { Operand->Literal.Int = -Operand->Literal.Int; }
						else { Operand->Literal.Float = -Operand->Literal.Float; }
						Operand->Span = FDnaSpan::Join(OpToken.Span, Operand->Span);
						return Operand;
					}
					FDnaExprPtr Node = MakeShared<FDnaExpr>();
					Node->Kind = EDnaExprKind::Unary;
					Node->Op = EDnaExprOp::Neg;
					Node->Span = FDnaSpan::Join(OpToken.Span, Operand->Span);
					Node->Args = { Operand };
					return Node;
				}
				return ParsePrimary();
			}

			FDnaExprPtr ParsePrimary()
			{
				const FDnaToken& Token = Peek();
				switch (Token.Kind)
				{
				case EDnaTokenKind::Int:
				{
					Take();
					FDnaExprPtr Node = MakeShared<FDnaExpr>();
					Node->Kind = EDnaExprKind::Literal;
					Node->Literal = FDnaValue::FromInt(static_cast<int32>(Token.Integer));
					Node->Span = Token.Span;
					return Node;
				}
				case EDnaTokenKind::Float:
				{
					Take();
					FDnaExprPtr Node = MakeShared<FDnaExpr>();
					Node->Kind = EDnaExprKind::Literal;
					Node->Literal = FDnaValue::FromFloat(static_cast<float>(Token.Real));
					Node->Span = Token.Span;
					return Node;
				}
				case EDnaTokenKind::Percent:
				{
					Take();
					FDnaExprPtr Node = MakeShared<FDnaExpr>();
					Node->Kind = EDnaExprKind::Literal;
					Node->Literal = FDnaValue::FromFloat(static_cast<float>(Token.Real / 100.0));
					Node->Span = Token.Span;
					return Node;
				}
				case EDnaTokenKind::LeftParen:
				{
					Take();
					FDnaExprPtr Inner = ParseOr();
					if (!Inner.IsValid()) { return nullptr; }
					if (!Peek().Is(EDnaTokenKind::RightParen))
					{
						return Fail(Peek(), LOCTEXT("ExpectedCloseParen", "')'"));
					}
					const FDnaToken& Close = Take();
					Inner->Span = FDnaSpan::Join(Token.Span, Close.Span);
					return Inner;
				}
				case EDnaTokenKind::Keyword:
				{
					if (Token.Keyword == EDnaKeyword::True || Token.Keyword == EDnaKeyword::False)
					{
						Take();
						FDnaExprPtr Node = MakeShared<FDnaExpr>();
						Node->Kind = EDnaExprKind::Literal;
						Node->Literal = FDnaValue::FromBool(Token.Keyword == EDnaKeyword::True);
						Node->Span = Token.Span;
						return Node;
					}
					if (Token.Keyword == EDnaKeyword::Chapter)
					{
						Take();
						FDnaExprPtr Node = MakeShared<FDnaExpr>();
						Node->Kind = EDnaExprKind::Chapter;
						Node->Span = Token.Span;
						return Node;
					}
					return Fail(Token, LOCTEXT("ExpectedValue", "a value"));
				}
				case EDnaTokenKind::Identifier:
				{
					Take();
					FDnaExprPtr Node = MakeShared<FDnaExpr>();
					Node->Span = Token.Span;
					Node->Name = Token.Text;
					Node->Path.Add(Token.Text);
					// dotted path
					while (Peek().Is(EDnaTokenKind::Dot) && (Peek(1).Is(EDnaTokenKind::Identifier) || Peek(1).Is(EDnaTokenKind::Keyword)))
					{
						Take();
						const FDnaToken& Segment = Take();
						Node->Name += TEXT(".") + Segment.Text;
						Node->Path.Add(Segment.Text);
						Node->Span = FDnaSpan::Join(Node->Span, Segment.Span);
					}
					// call
					if (Peek().Is(EDnaTokenKind::LeftParen))
					{
						Take();
						Node->Kind = EDnaExprKind::Call;
						if (!Peek().Is(EDnaTokenKind::RightParen))
						{
							while (true)
							{
								FDnaExprPtr Arg = ParseOr();
								if (!Arg.IsValid()) { return nullptr; }
								Node->Args.Add(Arg);
								if (Peek().Is(EDnaTokenKind::Comma)) { Take(); continue; }
								break;
							}
						}
						if (!Peek().Is(EDnaTokenKind::RightParen))
						{
							return Fail(Peek(), LOCTEXT("ExpectedCloseParen", "')'"));
						}
						const FDnaToken& Close = Take();
						Node->Span = FDnaSpan::Join(Node->Span, Close.Span);
						return Node;
					}
					Node->Kind = EDnaExprKind::Name;
					return Node;
				}
				default:
					return Fail(Token, LOCTEXT("ExpectedValue", "a value"));
				}
			}
		};
	}

	FDnaExprPtr ParseDnaExpression(const TArray<FDnaToken>& Tokens, int32& Index, FDnaDiagnosticSink& Diagnostics)
	{
		if (Tokens.Num() == 0)
		{
			return nullptr;
		}
		FExprParser Parser(Tokens, Index, Diagnostics);
		return Parser.ParseOr();
	}

	FDnaExprPtr ParseDnaExpressionSegment(const FDnaSourceText& Source, int32 Offset, int32 Length, FDnaDiagnosticSink& Diagnostics)
	{
		TArray<FDnaToken> Tokens;
		TokenizeDnaSegment(Source, Offset, Length, Tokens, Diagnostics);
		int32 Index = 0;
		FDnaExprPtr Expr = ParseDnaExpression(Tokens, Index, Diagnostics);
		if (!Expr.IsValid())
		{
			return nullptr;
		}
		if (!Tokens[Index].Is(EDnaTokenKind::EndOfInput))
		{
			Diagnostics.Error(TEXT("DNS2008"), Tokens[Index].Span,
				FText::Format(LOCTEXT("TrailingTokens", "Unexpected '{0}' after the expression"), FText::FromString(Tokens[Index].Text)));
			return nullptr;
		}
		return Expr;
	}
}

#undef LOCTEXT_NAMESPACE
