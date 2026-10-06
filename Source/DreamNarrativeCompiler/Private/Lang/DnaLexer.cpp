// Copyright DreamMoon. All Rights Reserved.

#include "Lang/DnaLexer.h"

#define LOCTEXT_NAMESPACE "DreamNarrativeLang"

namespace UE::DreamNarrative::Lang
{
	// ---------------------------------------------------------------- characters

	TCHAR NormalizeDnaPunctuation(TCHAR Char)
	{
		switch (Char)
		{
		case 0xFF1A: return TEXT(':');   // ：
		case 0xFF0C: return TEXT(',');   // ，
		case 0xFF08: return TEXT('(');   // （
		case 0xFF09: return TEXT(')');   // ）
		case 0xFF1C: return TEXT('<');   // ＜
		case 0xFF1E: return TEXT('>');   // ＞
		case 0xFF1D: return TEXT('=');   // ＝
		case 0xFF01: return TEXT('!');   // ！
		case 0xFF0B: return TEXT('+');   // ＋
		case 0xFF0D: return TEXT('-');   // －
		case 0xFF0A: return TEXT('*');   // ＊
		case 0xFF0F: return TEXT('/');   // ／
		case 0xFF05: return TEXT('%');   // ％
		case 0xFF03: return TEXT('#');   // ＃
		case 0xFF20: return TEXT('@');   // ＠
		case 0x201C: case 0x201D: return TEXT('"');   // “ ”
		default: return Char;
		}
	}

	bool IsDnaSpace(TCHAR Char)
	{
		return Char == TEXT(' ') || Char == TEXT('\t') || Char == 0x3000;
	}

	static bool IsDnaSymbolChar(TCHAR Char)
	{
		if (Char < 0x80)
		{
			return !(FChar::IsAlnum(Char) || Char == TEXT('_'));
		}
		if (FChar::IsWhitespace(Char) || Char == 0x3000 || Char == 0x00A0)
		{
			return true;
		}
		// Latin-1 punctuation, general punctuation (quotes, dashes, ellipsis), CJK symbols and
		// punctuation (。、「」『』【】《》), and the full-width ASCII punctuation ranges.
		if ((Char >= 0x00A1 && Char <= 0x00BF) || (Char >= 0x2000 && Char <= 0x206F)
			|| (Char >= 0x3000 && Char <= 0x303F)
			|| (Char >= 0xFF00 && Char <= 0xFF0F) || (Char >= 0xFF1A && Char <= 0xFF20)
			|| (Char >= 0xFF3B && Char <= 0xFF40) || (Char >= 0xFF5B && Char <= 0xFF65))
		{
			return true;
		}
		return false;
	}

	bool IsDnaIdentifierStart(TCHAR Char)
	{
		if (Char < 0x80)
		{
			return FChar::IsAlpha(Char) || Char == TEXT('_');
		}
		return !IsDnaSymbolChar(Char);
	}

	bool IsDnaIdentifierChar(TCHAR Char)
	{
		if (Char < 0x80)
		{
			return FChar::IsAlnum(Char) || Char == TEXT('_');
		}
		return !IsDnaSymbolChar(Char);
	}

	bool IsDnaReservedWord(const FString& Word)
	{
		static const TSet<FString> Reserved = {
			TEXT("file"), TEXT("character"), TEXT("fact"), TEXT("chapter"), TEXT("quest"), TEXT("dialogue"), TEXT("stage"),
			TEXT("test"), TEXT("explore"), TEXT("channel"), TEXT("event"), TEXT("cooldown"), TEXT("resume"),
			TEXT("if"), TEXT("else"), TEXT("once"), TEXT("set"), TEXT("accept"), TEXT("complete"), TEXT("fail"),
			TEXT("play"), TEXT("emit"), TEXT("wait"), TEXT("advance"), TEXT("end"),
			TEXT("title"), TEXT("priority"), TEXT("track"), TEXT("timeout"), TEXT("available"), TEXT("journal"),
			TEXT("objective"), TEXT("optional"), TEXT("show"), TEXT("on"), TEXT("subquest"), TEXT("reward"),
			TEXT("implied"), TEXT("after"),
			TEXT("duration"), TEXT("base"), TEXT("shots"), TEXT("camera"), TEXT("light"), TEXT("mask"), TEXT("vfx"),
			TEXT("ui"), TEXT("anim"), TEXT("game"), TEXT("sequence"), TEXT("restore"),
			TEXT("and"), TEXT("or"), TEXT("not"), TEXT("true"), TEXT("false"), TEXT("visited"), TEXT("random"),
			TEXT("chance"), TEXT("extern"), TEXT("session"), TEXT("server"), TEXT("client"), TEXT("shared"),
			TEXT("range"), TEXT("in"), TEXT("was"), TEXT("for"), TEXT("player"),
			TEXT("archive"), TEXT("entry"), TEXT("profile"), TEXT("progress"),
		};
		return Reserved.Contains(Word);
	}

	bool TryGetDnaKeyword(const FString& Word, EDnaKeyword& OutKeyword)
	{
		static const TMap<FString, EDnaKeyword> Keywords = {
			{ TEXT("and"), EDnaKeyword::And }, { TEXT("or"), EDnaKeyword::Or }, { TEXT("not"), EDnaKeyword::Not },
			{ TEXT("true"), EDnaKeyword::True }, { TEXT("false"), EDnaKeyword::False }, { TEXT("in"), EDnaKeyword::In },
			{ TEXT("if"), EDnaKeyword::If }, { TEXT("once"), EDnaKeyword::Once }, { TEXT("for"), EDnaKeyword::For },
			{ TEXT("chapter"), EDnaKeyword::Chapter },
		};
		if (const EDnaKeyword* Found = Keywords.Find(Word))
		{
			OutKeyword = *Found;
			return true;
		}
		OutKeyword = EDnaKeyword::None;
		return false;
	}

	const TCHAR* LexToString(EDnaTokenKind Kind)
	{
		switch (Kind)
		{
		case EDnaTokenKind::EndOfInput: return TEXT("end of line");
		case EDnaTokenKind::Identifier: return TEXT("identifier");
		case EDnaTokenKind::Keyword: return TEXT("keyword");
		case EDnaTokenKind::Int: return TEXT("integer");
		case EDnaTokenKind::Float: return TEXT("number");
		case EDnaTokenKind::Duration: return TEXT("duration");
		case EDnaTokenKind::Percent: return TEXT("percentage");
		case EDnaTokenKind::String: return TEXT("string");
		case EDnaTokenKind::LeftParen: return TEXT("(");
		case EDnaTokenKind::RightParen: return TEXT(")");
		case EDnaTokenKind::Comma: return TEXT(",");
		case EDnaTokenKind::Colon: return TEXT(":");
		case EDnaTokenKind::Dot: return TEXT(".");
		case EDnaTokenKind::DotDot: return TEXT("..");
		case EDnaTokenKind::Hash: return TEXT("#");
		case EDnaTokenKind::At: return TEXT("@");
		case EDnaTokenKind::Arrow: return TEXT("->");
		case EDnaTokenKind::Plus: return TEXT("+");
		case EDnaTokenKind::Minus: return TEXT("-");
		case EDnaTokenKind::Star: return TEXT("*");
		case EDnaTokenKind::Slash: return TEXT("/");
		case EDnaTokenKind::PercentSign: return TEXT("%");
		case EDnaTokenKind::Assign: return TEXT("=");
		case EDnaTokenKind::PlusAssign: return TEXT("+=");
		case EDnaTokenKind::MinusAssign: return TEXT("-=");
		case EDnaTokenKind::EqualEqual: return TEXT("==");
		case EDnaTokenKind::BangEqual: return TEXT("!=");
		case EDnaTokenKind::Less: return TEXT("<");
		case EDnaTokenKind::LessEqual: return TEXT("<=");
		case EDnaTokenKind::Greater: return TEXT(">");
		case EDnaTokenKind::GreaterEqual: return TEXT(">=");
		case EDnaTokenKind::Bang: return TEXT("!");
		case EDnaTokenKind::AmpAmp: return TEXT("&&");
		case EDnaTokenKind::PipePipe: return TEXT("||");
		case EDnaTokenKind::Unknown: return TEXT("unknown character");
		}
		return TEXT("?");
	}

	// ---------------------------------------------------------------- comments

	int32 FindDnaCommentStart(const FString& Text)
	{
		// Quotes suppress `//` until their closing partner. The three quote styles the language
		// accepts are tracked separately so `「 ... "` cannot close a `「`.
		TCHAR Closing = 0;
		const int32 Num = Text.Len();
		for (int32 Index = 0; Index < Num; ++Index)
		{
			const TCHAR Char = Text[Index];
			if (Closing != 0)
			{
				if (Char == Closing)
				{
					Closing = 0;
				}
				else if (Closing == TEXT('"') && Char == TEXT('\\'))
				{
					++Index;
				}
				continue;
			}
			if (Char == TEXT('"'))
			{
				Closing = TEXT('"');
			}
			else if (Char == 0x201C)
			{
				Closing = 0x201D;
			}
			else if (Char == 0x300C)
			{
				Closing = 0x300D;
			}
			else if (Char == TEXT('/') && Index + 1 < Num && Text[Index + 1] == TEXT('/'))
			{
				return Index;
			}
		}
		return INDEX_NONE;
	}

	// ---------------------------------------------------------------- line scanner

	bool ScanDnaLines(const FDnaSourceText& Source, FDnaScanResult& Out, FDnaDiagnosticSink& Diagnostics)
	{
		bool bOk = true;
		Out.Lines.Reset();
		Out.IndentUnit = 0;
		Out.IndentChar = TEXT(' ');
		bool bIndentCharDecided = false;

		const int32 LineCount = Source.GetLineCount();
		Out.Lines.Reserve(LineCount);

		for (int32 LineNumber = 1; LineNumber <= LineCount; ++LineNumber)
		{
			const FString LineText = Source.GetLineText(LineNumber);
			const int32 LineStart = Source.GetLineStart(LineNumber);

			FDnaRawLine& Line = Out.Lines.AddDefaulted_GetRef();
			Line.LineNumber = LineNumber;
			Line.LineSpan = Source.MakeSpan(LineStart, LineText.Len());

			// -- indentation
			int32 IndentEnd = 0;
			bool bSawSpace = false;
			bool bSawTab = false;
			bool bSawFullWidth = false;
			while (IndentEnd < LineText.Len() && IsDnaSpace(LineText[IndentEnd]))
			{
				const TCHAR Char = LineText[IndentEnd];
				bSawSpace |= (Char == TEXT(' '));
				bSawTab |= (Char == TEXT('\t'));
				bSawFullWidth |= (Char == 0x3000);
				++IndentEnd;
			}
			Line.IndentText = LineText.Left(IndentEnd);

			// -- comment
			const FString Rest = LineText.Mid(IndentEnd);
			const int32 CommentAt = FindDnaCommentStart(Rest);
			FString Content = CommentAt == INDEX_NONE ? Rest : Rest.Left(CommentAt);
			if (CommentAt != INDEX_NONE)
			{
				Line.Comment = Rest.Mid(CommentAt);
			}
			// Right-trim, but count what was trimmed so the span stays exact.
			int32 ContentLen = Content.Len();
			while (ContentLen > 0 && IsDnaSpace(Content[ContentLen - 1]))
			{
				--ContentLen;
			}
			Content = Content.Left(ContentLen);
			Line.Content = Content;
			Line.Span = Source.MakeSpan(LineStart + IndentEnd, ContentLen);

			if (Line.IsFloating())
			{
				Line.IndentLevel = -1;
				continue;
			}

			// -- indentation policy, only for lines that carry content
			if (bSawFullWidth)
			{
				bOk = Diagnostics.Error(TEXT("DNS1001"), Source.MakeSpan(LineStart, IndentEnd),
					LOCTEXT("FullWidthIndent", "A full-width space (U+3000) is used as indentation; replace it with ordinary spaces"));
			}
			if (bSawSpace && bSawTab)
			{
				bOk = Diagnostics.Error(TEXT("DNS1002"), Source.MakeSpan(LineStart, IndentEnd),
					LOCTEXT("MixedIndentLine", "Tabs and spaces are mixed in the indentation of this line"));
			}

			if (IndentEnd == 0)
			{
				Line.IndentLevel = 0;
				continue;
			}

			const TCHAR ThisChar = bSawTab ? TEXT('\t') : TEXT(' ');
			if (!bIndentCharDecided)
			{
				bIndentCharDecided = true;
				Out.IndentChar = ThisChar;
				Out.IndentUnit = IndentEnd;
			}
			else if (ThisChar != Out.IndentChar && !(bSawSpace && bSawTab))
			{
				bOk = Diagnostics.Error(TEXT("DNS1002"), Source.MakeSpan(LineStart, IndentEnd),
					FText::Format(LOCTEXT("MixedIndentFile", "This line is indented with {0} but the file uses {1}"),
						FText::FromString(ThisChar == TEXT('\t') ? TEXT("tabs") : TEXT("spaces")),
						FText::FromString(Out.IndentChar == TEXT('\t') ? TEXT("tabs") : TEXT("spaces"))));
			}

			if (Out.IndentUnit > 0 && (IndentEnd % Out.IndentUnit) != 0)
			{
				bOk = Diagnostics.Error(TEXT("DNS1003"), Source.MakeSpan(LineStart, IndentEnd),
					FText::Format(LOCTEXT("IndentNotMultiple", "Indentation of {0} is not a whole number of the file's indent unit ({1})"),
						IndentEnd, Out.IndentUnit));
				Line.IndentLevel = FMath::Max(1, FMath::RoundToInt(static_cast<float>(IndentEnd) / Out.IndentUnit));
			}
			else
			{
				Line.IndentLevel = Out.IndentUnit > 0 ? IndentEnd / Out.IndentUnit : 1;
			}
		}

		return bOk;
	}

	// ---------------------------------------------------------------- tokenizer

	namespace
	{
		struct FSegmentCursor
		{
			const FDnaSourceText& Source;
			const FString& Text;
			int32 Pos;
			int32 End;

			bool AtEnd() const { return Pos >= End; }
			TCHAR Peek(int32 Ahead = 0) const { return (Pos + Ahead < End) ? Text[Pos + Ahead] : TEXT('\0'); }
			TCHAR PeekNormalized(int32 Ahead = 0) const { return NormalizeDnaPunctuation(Peek(Ahead)); }
		};

		void PushToken(TArray<FDnaToken>& Tokens, const FDnaSourceText& Source, EDnaTokenKind Kind, int32 Offset, int32 Length, FString Text = FString())
		{
			FDnaToken& Token = Tokens.AddDefaulted_GetRef();
			Token.Kind = Kind;
			Token.Span = Source.MakeSpan(Offset, Length);
			Token.Text = MoveTemp(Text);
		}
	}

	void TokenizeDnaSegment(const FDnaSourceText& Source, int32 Offset, int32 Length, TArray<FDnaToken>& OutTokens, FDnaDiagnosticSink& Diagnostics)
	{
		const FString& Text = Source.GetText();
		FSegmentCursor Cursor{ Source, Text, Offset, FMath::Min(Offset + Length, Text.Len()) };

		while (!Cursor.AtEnd())
		{
			const TCHAR Raw = Cursor.Peek();
			if (IsDnaSpace(Raw) || Raw == TEXT('\r') || Raw == TEXT('\n'))
			{
				++Cursor.Pos;
				continue;
			}

			const int32 Start = Cursor.Pos;
			const TCHAR Char = NormalizeDnaPunctuation(Raw);

			// -- strings
			if (Char == TEXT('"') || Raw == 0x300C)
			{
				const TCHAR Closing = (Raw == 0x300C) ? 0x300D : (Raw == 0x201C ? 0x201D : TEXT('"'));
				const bool bEscapes = (Raw == TEXT('"'));
				++Cursor.Pos;
				FString Value;
				bool bClosed = false;
				while (!Cursor.AtEnd())
				{
					const TCHAR Inner = Cursor.Peek();
					if (Inner == Closing)
					{
						++Cursor.Pos;
						bClosed = true;
						break;
					}
					if (bEscapes && Inner == TEXT('\\') && Cursor.Pos + 1 < Cursor.End)
					{
						const TCHAR Escaped = Cursor.Peek(1);
						switch (Escaped)
						{
						case TEXT('n'): Value.AppendChar(TEXT('\n')); break;
						case TEXT('t'): Value.AppendChar(TEXT('\t')); break;
						default: Value.AppendChar(Escaped); break;
						}
						Cursor.Pos += 2;
						continue;
					}
					Value.AppendChar(Inner);
					++Cursor.Pos;
				}
				if (!bClosed)
				{
					Diagnostics.Error(TEXT("DNS1004"), Source.MakeSpan(Start, Cursor.Pos - Start),
						LOCTEXT("UnterminatedString", "String literal is not closed"));
				}
				PushToken(OutTokens, Source, EDnaTokenKind::String, Start, Cursor.Pos - Start, MoveTemp(Value));
				continue;
			}

			// -- numbers: digits [. digits] [unit | %]
			if (FChar::IsDigit(Raw) || (Raw == TEXT('.') && FChar::IsDigit(Cursor.Peek(1))))
			{
				bool bIsFloat = false;
				while (!Cursor.AtEnd() && FChar::IsDigit(Cursor.Peek()))
				{
					++Cursor.Pos;
				}
				// `1..2` is a range, not a float followed by a dot.
				if (Cursor.Peek() == TEXT('.') && Cursor.Peek(1) != TEXT('.') && FChar::IsDigit(Cursor.Peek(1)))
				{
					bIsFloat = true;
					++Cursor.Pos;
					while (!Cursor.AtEnd() && FChar::IsDigit(Cursor.Peek()))
					{
						++Cursor.Pos;
					}
				}
				const FString Lexeme = Text.Mid(Start, Cursor.Pos - Start);
				const double Number = FCString::Atod(*Lexeme);

				// unit suffix
				double Scale = 0.0;
				int32 UnitLen = 0;
				const TCHAR U0 = Cursor.Peek();
				const TCHAR U1 = Cursor.Peek(1);
				if (U0 == TEXT('m') && U1 == TEXT('s') && !IsDnaIdentifierChar(Cursor.Peek(2))) { Scale = 0.001; UnitLen = 2; }
				else if (U0 == TEXT('s') && !IsDnaIdentifierChar(U1)) { Scale = 1.0; UnitLen = 1; }
				else if (U0 == TEXT('m') && !IsDnaIdentifierChar(U1)) { Scale = 60.0; UnitLen = 1; }
				else if (U0 == TEXT('h') && !IsDnaIdentifierChar(U1)) { Scale = 3600.0; UnitLen = 1; }

				if (UnitLen > 0)
				{
					Cursor.Pos += UnitLen;
					FDnaToken& Token = OutTokens.AddDefaulted_GetRef();
					Token.Kind = EDnaTokenKind::Duration;
					Token.Real = Number * Scale;
					Token.Text = Text.Mid(Start, Cursor.Pos - Start);
					Token.Span = Source.MakeSpan(Start, Cursor.Pos - Start);
					continue;
				}
				if (Cursor.PeekNormalized() == TEXT('%'))
				{
					++Cursor.Pos;
					FDnaToken& Token = OutTokens.AddDefaulted_GetRef();
					Token.Kind = EDnaTokenKind::Percent;
					Token.Real = Number;
					Token.Text = Text.Mid(Start, Cursor.Pos - Start);
					Token.Span = Source.MakeSpan(Start, Cursor.Pos - Start);
					continue;
				}

				FDnaToken& Token = OutTokens.AddDefaulted_GetRef();
				Token.Kind = bIsFloat ? EDnaTokenKind::Float : EDnaTokenKind::Int;
				Token.Real = Number;
				Token.Integer = bIsFloat ? static_cast<int64>(Number) : FCString::Atoi64(*Lexeme);
				Token.Text = Lexeme;
				Token.Span = Source.MakeSpan(Start, Cursor.Pos - Start);
				continue;
			}

			// -- identifiers and keywords
			if (IsDnaIdentifierStart(Raw))
			{
				while (!Cursor.AtEnd() && IsDnaIdentifierChar(Cursor.Peek()))
				{
					++Cursor.Pos;
				}
				FString Word = Text.Mid(Start, Cursor.Pos - Start);
				FDnaToken& Token = OutTokens.AddDefaulted_GetRef();
				Token.Kind = TryGetDnaKeyword(Word, Token.Keyword) ? EDnaTokenKind::Keyword : EDnaTokenKind::Identifier;
				Token.Text = MoveTemp(Word);
				Token.Span = Source.MakeSpan(Start, Cursor.Pos - Start);
				continue;
			}

			// -- punctuation, longest match first
			const TCHAR Next = Cursor.PeekNormalized(1);
			EDnaTokenKind Kind = EDnaTokenKind::Unknown;
			int32 Len = 1;
			switch (Char)
			{
			case TEXT('('): Kind = EDnaTokenKind::LeftParen; break;
			case TEXT(')'): Kind = EDnaTokenKind::RightParen; break;
			case TEXT(','): Kind = EDnaTokenKind::Comma; break;
			case TEXT(':'): Kind = EDnaTokenKind::Colon; break;
			case TEXT('#'): Kind = EDnaTokenKind::Hash; break;
			case TEXT('@'): Kind = EDnaTokenKind::At; break;
			case TEXT('*'): Kind = EDnaTokenKind::Star; break;
			case TEXT('/'): Kind = EDnaTokenKind::Slash; break;
			case TEXT('%'): Kind = EDnaTokenKind::PercentSign; break;
			case TEXT('.'):
				if (Next == TEXT('.')) { Kind = EDnaTokenKind::DotDot; Len = 2; } else { Kind = EDnaTokenKind::Dot; }
				break;
			case TEXT('-'):
				if (Next == TEXT('>')) { Kind = EDnaTokenKind::Arrow; Len = 2; }
				else if (Next == TEXT('=')) { Kind = EDnaTokenKind::MinusAssign; Len = 2; }
				else { Kind = EDnaTokenKind::Minus; }
				break;
			case TEXT('+'):
				if (Next == TEXT('=')) { Kind = EDnaTokenKind::PlusAssign; Len = 2; } else { Kind = EDnaTokenKind::Plus; }
				break;
			case TEXT('='):
				if (Next == TEXT('=')) { Kind = EDnaTokenKind::EqualEqual; Len = 2; } else { Kind = EDnaTokenKind::Assign; }
				break;
			case TEXT('!'):
				if (Next == TEXT('=')) { Kind = EDnaTokenKind::BangEqual; Len = 2; } else { Kind = EDnaTokenKind::Bang; }
				break;
			case TEXT('<'):
				if (Next == TEXT('=')) { Kind = EDnaTokenKind::LessEqual; Len = 2; } else { Kind = EDnaTokenKind::Less; }
				break;
			case TEXT('>'):
				if (Next == TEXT('=')) { Kind = EDnaTokenKind::GreaterEqual; Len = 2; } else { Kind = EDnaTokenKind::Greater; }
				break;
			case TEXT('&'):
				if (Next == TEXT('&')) { Kind = EDnaTokenKind::AmpAmp; Len = 2; }
				break;
			case TEXT('|'):
				if (Next == TEXT('|')) { Kind = EDnaTokenKind::PipePipe; Len = 2; }
				break;
			default:
				break;
			}

			if (Kind == EDnaTokenKind::Unknown)
			{
				Diagnostics.Error(TEXT("DNS1005"), Source.MakeSpan(Start, 1),
					FText::Format(LOCTEXT("UnknownChar", "Unexpected character '{0}'"), FText::FromString(FString::Chr(Raw))));
			}
			Cursor.Pos += Len;
			PushToken(OutTokens, Source, Kind, Start, Len, Text.Mid(Start, Len));
		}

		PushToken(OutTokens, Source, EDnaTokenKind::EndOfInput, Cursor.End, 0);
	}

	bool TryParseDnaDuration(const FString& Text, double& OutSeconds)
	{
		const FDnaSourceText Source(TEXT("<duration>"), Text.TrimStartAndEnd());
		TArray<FDnaToken> Tokens;
		FDnaDiagnosticSink Scratch;
		TokenizeDnaSegment(Source, 0, Source.Len(), Tokens, Scratch);
		if (Tokens.Num() != 2 || !Tokens[0].Is(EDnaTokenKind::Duration) || Scratch.HasErrors())
		{
			return false;
		}
		OutSeconds = Tokens[0].Real;
		return true;
	}
}

#undef LOCTEXT_NAMESPACE
