// Copyright DreamMoon. All Rights Reserved.

#include "Lang/DnaParser.h"
#include "DreamNarrativeExpression.h"
#include "Misc/FileHelper.h"

#define LOCTEXT_NAMESPACE "DreamNarrativeLang"

namespace UE::DreamNarrative::Lang
{
	namespace
	{
		enum class EContext : uint8
		{
			TopLevel,
			File,
			Character,
			FactGroup,
			Chapter,
			Implied,
			Quest,
			Journal,
			Objective,
			On,
			Dialogue,
			Stage,
			Track,
			Shots,
			Restore,
			/** The steps of a `test`. */
			Test,
			/** The steps and properties of an `explore`. */
			Explore,
			/** The expectations under a test's `talk` or `start`. */
			TestDialogue,
			/** The entries of a test's `given`: facts, and the chapter, visits and once-choices of a starting state. */
			Given,
			/** The properties of a `channel`. */
			Channel,
			/** The parameters of an `event`. */
			EventParams,
			/** The expectations inside a test's `channel` block. */
			TestChannel,
			/** The title and entries of an `archive`. */
			Archive,
		};

		enum class EModifierSet : uint8
		{
			/** if, for, @stage, #id */
			Text,
			/** if, once, #id */
			Choice,
			/** #id alone: an objective's `show:` */
			Id,
		};

		bool IsTrackName(const FString& Word)
		{
			return Word == TEXT("camera") || Word == TEXT("light") || Word == TEXT("mask") || Word == TEXT("vfx")
				|| Word == TEXT("ui") || Word == TEXT("anim") || Word == TEXT("game") || Word == TEXT("sequence") || Word == TEXT("audio") || Word == TEXT("blocking");
		}

		bool IsDeclarationWord(const FString& Word)
		{
			return Word == TEXT("character") || Word == TEXT("fact") || Word == TEXT("chapter") || Word == TEXT("quest")
				|| Word == TEXT("dialogue") || Word == TEXT("stage") || Word == TEXT("channel") || Word == TEXT("event");
		}

		/** The run of identifier characters at the start of Text. Empty when Text does not start with one. */
		FString FirstWord(const FString& Text)
		{
			int32 Len = 0;
			if (Text.Len() > 0 && IsDnaIdentifierStart(Text[0]))
			{
				while (Len < Text.Len() && IsDnaIdentifierChar(Text[Len]))
				{
					++Len;
				}
			}
			return Text.Left(Len);
		}

		int32 SkipSpaces(const FString& Text, int32 Index)
		{
			while (Index < Text.Len() && IsDnaSpace(Text[Index]))
			{
				++Index;
			}
			return Index;
		}

		bool WordAt(const FString& Text, int32 Index, const TCHAR* Word)
		{
			const int32 WordLen = FCString::Strlen(Word);
			if (Index + WordLen > Text.Len())
			{
				return false;
			}
			for (int32 Offset = 0; Offset < WordLen; ++Offset)
			{
				if (Text[Index + Offset] != Word[Offset])
				{
					return false;
				}
			}
			const int32 After = Index + WordLen;
			return After >= Text.Len() || !IsDnaIdentifierChar(Text[After]);
		}

		class FParser
		{
		public:
			FParser(const FDnaSourceText& InSource, const FDnaParseOptions& InOptions, FDnaDocument& InDocument, FDnaDiagnosticSink& InDiagnostics)
				: Source(InSource), Options(InOptions), Document(InDocument), Diagnostics(InDiagnostics)
			{
			}

			bool Run()
			{
				Document.Path = Source.GetPath();
				Document.Source = Source;
				Document.Roots.Reset();
				Document.DeclaredNames.Reset();

				if (!ScanDnaLines(Source, Document.Scan, Diagnostics))
				{
					bOk = false;
				}

				CollectDeclaredNames();
				BuildTree();

				for (const FDnaLineNodePtr& Root : Document.Roots)
				{
					if (Root.IsValid() && !Root->IsFloating())
					{
						ClassifyLine(*Root, EContext::TopLevel);
					}
				}
				return bOk;
			}

		private:
			const FDnaSourceText& Source;
			const FDnaParseOptions& Options;
			FDnaDocument& Document;
			FDnaDiagnosticSink& Diagnostics;
			bool bOk = true;

			// ---------------------------------------------------------------- utilities

			bool Fail(const TCHAR* Code, const FDnaSpan& Span, const FText& Message)
			{
				bOk = false;
				return Diagnostics.Error(Code, Span, Message);
			}

			/** Absolute offset of a position inside a line's content. */
			int32 Abs(const FDnaLineNode& Node, int32 RelativeIndex) const
			{
				return Node.Raw.Span.Offset + RelativeIndex;
			}

			FDnaSpan SpanOf(const FDnaLineNode& Node, int32 RelativeStart, int32 Length) const
			{
				return Source.MakeSpan(Abs(Node, RelativeStart), Length);
			}

			void Tokenize(const FDnaLineNode& Node, int32 RelativeStart, TArray<FDnaToken>& OutTokens)
			{
				const int32 Length = FMath::Max(0, Node.Raw.Content.Len() - RelativeStart);
				TokenizeDnaSegment(Source, Abs(Node, RelativeStart), Length, OutTokens, Diagnostics);
			}

			void TokenizeRange(const FDnaLineNode& Node, int32 RelativeStart, int32 RelativeEnd, TArray<FDnaToken>& OutTokens, FDnaDiagnosticSink& Sink)
			{
				TokenizeDnaSegment(Source, Abs(Node, RelativeStart), FMath::Max(0, RelativeEnd - RelativeStart), OutTokens, Sink);
			}

			bool ExpectEnd(const TArray<FDnaToken>& Tokens, int32 Index)
			{
				if (Tokens.IsValidIndex(Index) && !Tokens[Index].Is(EDnaTokenKind::EndOfInput))
				{
					return Fail(TEXT("DNS2008"), Tokens[Index].Span,
						FText::Format(LOCTEXT("Trailing", "Unexpected '{0}' here"), FText::FromString(Tokens[Index].Text)));
				}
				return true;
			}

			bool ExpectIdentifier(const TArray<FDnaToken>& Tokens, int32& Index, const FText& What, FString& OutText)
			{
				const FDnaToken& Token = Tokens[FMath::Min(Index, Tokens.Num() - 1)];
				if (Token.Is(EDnaTokenKind::Identifier))
				{
					OutText = Token.Text;
					++Index;
					return true;
				}
				return Fail(TEXT("DNS2007"), Token.Span,
					FText::Format(LOCTEXT("Expected", "Expected {0}, found {1}"), What,
						FText::FromString(Token.Is(EDnaTokenKind::EndOfInput) ? TEXT("end of line") : Token.Text)));
			}

			/** A dotted name: identifier (. identifier)*. Keyword segments are accepted after a dot (`猎狼.completed`). */
			bool ExpectDottedName(const TArray<FDnaToken>& Tokens, int32& Index, const FText& What, FString& OutText)
			{
				if (!ExpectIdentifier(Tokens, Index, What, OutText))
				{
					return false;
				}
				while (Tokens.IsValidIndex(Index + 1) && Tokens[Index].Is(EDnaTokenKind::Dot)
					&& (Tokens[Index + 1].Is(EDnaTokenKind::Identifier) || Tokens[Index + 1].Is(EDnaTokenKind::Keyword)))
				{
					OutText += TEXT(".") + Tokens[Index + 1].Text;
					Index += 2;
				}
				return true;
			}

			FDnaExprPtr ExpectExpression(const TArray<FDnaToken>& Tokens, int32& Index)
			{
				FDnaExprPtr Expr = ParseDnaExpression(Tokens, Index, Diagnostics);
				if (!Expr.IsValid())
				{
					bOk = false;
				}
				return Expr;
			}

			bool ReferencesKnownName(const FDnaExpr& Expr) const
			{
				if (Expr.Kind == EDnaExprKind::Chapter || Expr.Kind == EDnaExprKind::Call)
				{
					return true;
				}
				TArray<const FDnaExpr*> Names;
				Expr.CollectNames(Names);
				for (const FDnaExpr* Name : Names)
				{
					if (Name->Path.Num() == 0) { continue; }
					const FString& Root = Name->Path[0];
					if (Document.DeclaredNames.Contains(Root) || (Options.ExternalNames && Options.ExternalNames->Contains(Root)))
					{
						return true;
					}
				}
				return false;
			}

			// ---------------------------------------------------------------- pre-pass and tree

			void CollectDeclaredNames()
			{
				CollectDnaDeclaredNamesFromScan(Document.Scan, Document.DeclaredNames);
			}

			static void Attach(const FDnaLineNodePtr& Node, FDnaLineNode* Parent, TArray<FDnaLineNodePtr>& Roots)
			{
				Node->Parent = Parent;
				if (Parent)
				{
					Parent->Children.Add(Node);
				}
				else
				{
					Roots.Add(Node);
				}
			}

			void BuildTree()
			{
				TArray<FDnaLineNode*> Stack;
				for (const FDnaRawLine& Raw : Document.Scan.Lines)
				{
					FDnaLineNodePtr Node = MakeShared<FDnaLineNode>();
					Node->Raw = Raw;

					if (Raw.IsFloating())
					{
						Node->Kind = Raw.IsBlank() ? EDnaLineKind::Blank : EDnaLineKind::CommentOnly;
						Attach(Node, Stack.Num() > 0 ? Stack.Last() : nullptr, Document.Roots);
						continue;
					}

					const int32 Level = Raw.IndentLevel;
					while (Stack.Num() > 0 && Stack.Last()->Raw.IndentLevel >= Level)
					{
						Stack.Pop();
					}
					if (Stack.Num() == 0 && Level > 0)
					{
						Fail(TEXT("DNS2001"), Raw.Span, LOCTEXT("IndentAtTop", "This line is indented but there is nothing above it to belong to"));
					}
					Attach(Node, Stack.Num() > 0 ? Stack.Last() : nullptr, Document.Roots);
					Stack.Add(Node.Get());
				}
			}

			// ---------------------------------------------------------------- classification

			void ClassifyChildren(FDnaLineNode& Parent, EContext Context)
			{
				for (const FDnaLineNodePtr& Child : Parent.Children)
				{
					if (Child.IsValid() && !Child->IsFloating())
					{
						ClassifyLine(*Child, Context);
					}
				}
			}

			void MarkChildrenUnknown(FDnaLineNode& Parent)
			{
				for (const FDnaLineNodePtr& Child : Parent.Children)
				{
					if (Child.IsValid() && !Child->IsFloating())
					{
						Child->Kind = EDnaLineKind::Unknown;
						MarkChildrenUnknown(*Child);
					}
				}
			}

			void RequireNoChildren(FDnaLineNode& Node)
			{
				TArray<FDnaLineNode*> Content;
				Node.GetContentChildren(Content);
				for (FDnaLineNode* Child : Content)
				{
					Fail(TEXT("DNS2001"), Child->Raw.Span,
						FText::Format(LOCTEXT("IndentUnderLeaf", "This line is indented under a {0} line, which does not open a block"),
							FText::FromString(LexToString(Node.Kind))));
					Child->Kind = EDnaLineKind::Unknown;
					MarkChildrenUnknown(*Child);
				}
			}

			void RequireBody(FDnaLineNode& Node)
			{
				TArray<FDnaLineNode*> Content;
				Node.GetContentChildren(Content);
				if (Content.Num() == 0)
				{
					Fail(TEXT("DNS2002"), Node.Raw.Span,
						FText::Format(LOCTEXT("EmptyBlock", "'{0}' opens a block but nothing is indented under it"),
							FText::FromString(Node.Raw.Content)));
				}
			}

			void ClassifyContinuations(FDnaLineNode& Node)
			{
				for (const FDnaLineNodePtr& Child : Node.Children)
				{
					if (!Child.IsValid() || Child->IsFloating()) { continue; }
					Child->Kind = EDnaLineKind::Continuation;
					ParseText(*Child, 0, Child->Raw.Content.Len(), Child->Payload.Text);
					ClassifyContinuations(*Child);
				}
			}

			void ClassifyLine(FDnaLineNode& Node, EContext Context)
			{
				switch (Context)
				{
				case EContext::TopLevel: ClassifyTopLevel(Node); break;
				case EContext::File:
				case EContext::Character:
				case EContext::Journal:
				case EContext::Channel:
					ClassifyPropertyOnly(Node);
					break;
				case EContext::Objective: ClassifyObjectiveProperty(Node); break;
				case EContext::EventParams: ClassifyEventParam(Node); break;
				case EContext::Archive: ClassifyArchiveLine(Node); break;
				case EContext::TestChannel: ClassifyTestExpectation(Node, /*bInChannel=*/true); break;
				case EContext::FactGroup: ClassifyFactEntry(Node); break;
				case EContext::Chapter: ClassifyChapterLine(Node); break;
				case EContext::Implied: ClassifyImplication(Node); break;
				case EContext::Quest: ClassifyQuestLine(Node); break;
				case EContext::On: ClassifyOnLine(Node); break;
				case EContext::Dialogue: ClassifyDialogueLine(Node); break;
				case EContext::Stage: ClassifyStageLine(Node); break;
				case EContext::Track: ClassifyStageEntry(Node); break;
				case EContext::Shots: ClassifyNamedEntry(Node, EDnaLineKind::Shot); break;
				case EContext::Restore: ClassifyNamedEntry(Node, EDnaLineKind::RestoreEntry); break;
				case EContext::Test: ClassifyTestStep(Node, /*bExplore=*/false); break;
				case EContext::Explore: ClassifyTestStep(Node, /*bExplore=*/true); break;
				case EContext::TestDialogue: ClassifyTestExpectation(Node, /*bInChannel=*/false); break;
				case EContext::Given: ClassifyGivenEntry(Node); break;
				}
			}

			void Unexpected(FDnaLineNode& Node, const FText& Where)
			{
				Node.Kind = EDnaLineKind::Unknown;
				Fail(TEXT("DNS2003"), Node.Raw.Span,
					FText::Format(LOCTEXT("UnexpectedLine", "This line does not belong {0}"), Where));
				MarkChildrenUnknown(Node);
			}

			// ---------------------------------------------------------------- top level

			void ClassifyTopLevel(FDnaLineNode& Node)
			{
				const FString& Content = Node.Raw.Content;
				const FString Word = FirstWord(Content);

				if (Word == TEXT("file"))
				{
					Node.Kind = EDnaLineKind::File;
					TArray<FDnaToken> Tokens;
					Tokenize(Node, 0, Tokens);
					ExpectEnd(Tokens, 1);
					ClassifyChildren(Node, EContext::File);
					return;
				}
				if (Word == TEXT("character")) { ParseCharacterHeader(Node); return; }
				if (Word == TEXT("fact")) { ParseFactHeader(Node); return; }
				if (Word == TEXT("chapter")) { ParseChapterHeader(Node); return; }
				if (Word == TEXT("quest")) { ParseQuestHeader(Node); return; }
				if (Word == TEXT("dialogue")) { ParseDialogueHeader(Node); return; }
				if (Word == TEXT("stage")) { ParseStageHeader(Node); return; }
				if (Word == TEXT("channel")) { ParseNamedBlockHeader(Node, EDnaLineKind::Channel, LOCTEXT("ChannelName", "a channel name"), EContext::Channel, /*bAllowGameplay=*/false); return; }
				if (Word == TEXT("event")) { ParseNamedBlockHeader(Node, EDnaLineKind::Event, LOCTEXT("EventName", "an event name"), EContext::EventParams, /*bAllowGameplay=*/true); return; }
				if (Word == TEXT("archive")) { ParseArchiveHeader(Node); return; }
				if (Word == TEXT("test")) { ParseTestHeader(Node, EDnaLineKind::Test); return; }
				if (Word == TEXT("explore")) { ParseTestHeader(Node, EDnaLineKind::Explore); return; }

				if (Content.StartsWith(TEXT("- ")) || Content == TEXT("-"))
				{
					Node.Kind = EDnaLineKind::Unknown;
					Fail(TEXT("DNS2005"), Node.Raw.Span, LOCTEXT("ChoiceOutside", "A choice can only appear inside a dialogue"));
					MarkChildrenUnknown(Node);
					return;
				}
				Unexpected(Node, LOCTEXT("AtTopLevel", "at the top level; expected file, character, fact, chapter, quest, dialogue, stage, channel, event, archive, test or explore"));
			}

			/** `archive 名 [profile]`, then its title and entries. */
			void ParseArchiveHeader(FDnaLineNode& Node)
			{
				Node.Kind = EDnaLineKind::Archive;
				TArray<FDnaToken> Tokens;
				Tokenize(Node, 0, Tokens);
				int32 Index = 1;
				if (ExpectDottedName(Tokens, Index, LOCTEXT("ArchiveName", "an archive name"), Node.Payload.Name))
				{
					if (Tokens[Index].IsWord(TEXT("profile")))
					{
						Node.Payload.bProfile = true;
						++Index;
					}
					ExpectEnd(Tokens, Index);
				}
				RequireBody(Node);
				ClassifyChildren(Node, EContext::Archive);
			}

			/** `title: 标题` or `entry [if 条件] [#id]` with its text in the lines under it. */
			void ClassifyArchiveLine(FDnaLineNode& Node)
			{
				if (FirstWord(Node.Raw.Content) == TEXT("entry"))
				{
					Node.Kind = EDnaLineKind::ArchiveEntry;
					TArray<FDnaToken> Tokens;
					Tokenize(Node, 0, Tokens);
					int32 Index = 1;
					bool bHeaderOk = true;
					if (Tokens[Index].IsKeyword(EDnaKeyword::If))
					{
						++Index;
						Node.Payload.Condition = ExpectExpression(Tokens, Index);
						bHeaderOk = Node.Payload.Condition.IsValid();
					}
					// The key its text is translated under.
					if (bHeaderOk && Tokens[Index].Is(EDnaTokenKind::Hash) && !ReadId(Tokens, Index, Node.Payload.Modifiers.Id))
					{
						Fail(TEXT("DNS2007"), Tokens[Index].Span, LOCTEXT("EntryId", "Expected an id after '#'"));
						bHeaderOk = false;
					}
					if (bHeaderOk)
					{
						ExpectEnd(Tokens, Index);
					}
					RequireBody(Node);
					ClassifyContinuations(Node);
					return;
				}
				if (TryParseProperty(Node))
				{
					return;
				}
				Unexpected(Node, LOCTEXT("InArchive", "inside an archive; expected 'title:' or 'entry'"));
			}

			/**
			 * `channel 名` / `event 名 [gameplay 标签]`: a name, for an event the gameplay event that raises
			 * it, and a block of Context lines (which may be empty).
			 */
			void ParseNamedBlockHeader(FDnaLineNode& Node, EDnaLineKind Kind, const FText& What, EContext Context, bool bAllowGameplay)
			{
				Node.Kind = Kind;
				TArray<FDnaToken> Tokens;
				Tokenize(Node, 0, Tokens);
				int32 Index = 1;
				if (ExpectIdentifier(Tokens, Index, What, Node.Payload.Name))
				{
					const bool bTagOk = !(bAllowGameplay && Tokens[Index].IsWord(TEXT("gameplay")))
						|| ExpectDottedName(Tokens, ++Index, LOCTEXT("EventGameplayTag", "a gameplay tag after 'gameplay'"), Node.Payload.GameplayTag);
					if (bTagOk)
					{
						ExpectEnd(Tokens, Index);
					}
				}
				ClassifyChildren(Node, Context);
			}

			/** `名: type ...` inside an event: a parameter, typed like a fact; only the value clauses mean anything. */
			void ClassifyEventParam(FDnaLineNode& Node)
			{
				TArray<FDnaToken> Tokens;
				Tokenize(Node, 0, Tokens);
				if (!Tokens[0].Is(EDnaTokenKind::Identifier) || !Tokens[1].Is(EDnaTokenKind::Colon))
				{
					Unexpected(Node, LOCTEXT("InEvent", "inside an event; expected '参数: type ...'"));
					return;
				}
				Node.Kind = EDnaLineKind::EventParam;
				FDnaLinePayload& P = Node.Payload;
				P.Name = Tokens[0].Text;
				int32 Index = 2;
				if (ParseFactSpec(Node, Tokens, Index, /*bAllowSource=*/true)
					&& (P.bExtern || P.bSession || P.bProfile || P.bProgress || P.Authority != EDnaAuthority::Shared || !P.WasName.IsEmpty() || !P.GameplayTag.IsEmpty()))
				{
					Fail(TEXT("DNS2019"), Node.Raw.Span, LOCTEXT("ParamModifier",
						"An event parameter takes a type, `in (...)`, `range`, a default and `from`; extern, session, profile, progress, server, client, was and gameplay belong to facts"));
				}
				RequireNoChildren(Node);
			}

			/** `[player] [was 旧名]` and friends: the optional tail of a declaration header. */
			bool ParseHeaderTail(FDnaLineNode& Node, const TArray<FDnaToken>& Tokens, int32& Index, bool bAllowPlayer, bool bAllowAfter, bool bAllowDialogueClauses)
			{
				while (!Tokens[Index].Is(EDnaTokenKind::EndOfInput))
				{
					const FDnaToken& Token = Tokens[Index];
					if (Token.IsWord(TEXT("was")))
					{
						++Index;
						if (!ExpectIdentifier(Tokens, Index, LOCTEXT("OldName", "the previous name after 'was'"), Node.Payload.WasName)) { return false; }
						continue;
					}
					if (bAllowPlayer && Token.IsWord(TEXT("player")))
					{
						Node.Payload.bPlayer = true;
						++Index;
						continue;
					}
					if (bAllowAfter && Token.IsWord(TEXT("after")))
					{
						++Index;
						if (!ExpectIdentifier(Tokens, Index, LOCTEXT("AfterName", "a chapter name after 'after'"), Node.Payload.Target)) { return false; }
						continue;
					}
					if (bAllowDialogueClauses && Token.IsKeyword(EDnaKeyword::For))
					{
						++Index;
						if (!ExpectIdentifier(Tokens, Index, LOCTEXT("ForName", "a character name after 'for'"), Node.Payload.ForCharacter)) { return false; }
						continue;
					}
					if (bAllowDialogueClauses && Token.IsKeyword(EDnaKeyword::If))
					{
						++Index;
						Node.Payload.Condition = ExpectExpression(Tokens, Index);
						if (!Node.Payload.Condition.IsValid()) { return false; }
						continue;
					}
					if (bAllowDialogueClauses && Token.IsWord(TEXT("priority")))
					{
						++Index;
						if (!Tokens[Index].Is(EDnaTokenKind::Int))
						{
							return Fail(TEXT("DNS2007"), Tokens[Index].Span, LOCTEXT("PriorityInt", "Expected a whole number after 'priority'"));
						}
						Node.Payload.Priority = static_cast<int32>(Tokens[Index].Integer);
						++Index;
						continue;
					}
					if (bAllowDialogueClauses && Token.IsWord(TEXT("on")))
					{
						++Index;
						if (!ExpectIdentifier(Tokens, Index, LOCTEXT("OnName", "an event name after 'on'"), Node.Payload.OnEvent)) { return false; }
						continue;
					}
					if (bAllowDialogueClauses && Token.IsWord(TEXT("channel")))
					{
						++Index;
						if (!ExpectIdentifier(Tokens, Index, LOCTEXT("ChannelClause", "a channel name after 'channel'"), Node.Payload.Channel)) { return false; }
						continue;
					}
					if (bAllowDialogueClauses && Token.IsKeyword(EDnaKeyword::Once))
					{
						Node.Payload.bOnceEntry = true;
						++Index;
						continue;
					}
					if (bAllowDialogueClauses && Token.IsWord(TEXT("cooldown")))
					{
						++Index;
						if (!Tokens[Index].Is(EDnaTokenKind::Duration) || Tokens[Index].Real <= 0.0)
						{
							return Fail(TEXT("DNS2007"), Tokens[Index].Span, LOCTEXT("CooldownDuration", "Expected a duration such as 30s after 'cooldown'"));
						}
						Node.Payload.CooldownSeconds = Tokens[Index].Real;
						++Index;
						continue;
					}
					if (bAllowDialogueClauses && Token.IsWord(TEXT("resume")))
					{
						Node.Payload.bResume = true;
						++Index;
						continue;
					}
					return ExpectEnd(Tokens, Index);
				}
				return true;
			}

			/**
			 * `(参数 = 值, ...)` after an emit's event name, when there is one. Index is on the token after
			 * the name and is left after the closing parenthesis.
			 */
			bool ParseEmitArgs(const TArray<FDnaToken>& Tokens, int32& Index, TArray<FDnaEmitArg>& Out)
			{
				if (!Tokens[Index].Is(EDnaTokenKind::LeftParen))
				{
					return true;
				}
				++Index;
				while (true)
				{
					FDnaEmitArg Arg;
					Arg.Span = Tokens[Index].Span;
					if (!ExpectIdentifier(Tokens, Index, LOCTEXT("EmitArgName", "a parameter name"), Arg.Name)) { return false; }
					if (!Tokens[Index].Is(EDnaTokenKind::Assign))
					{
						return Fail(TEXT("DNS2007"), Tokens[Index].Span, LOCTEXT("EmitArgAssign", "Expected '=' after the parameter name"));
					}
					++Index;
					Arg.Value = ExpectExpression(Tokens, Index);
					if (!Arg.Value.IsValid()) { return false; }
					if (Out.ContainsByPredicate([&Arg](const FDnaEmitArg& Other) { return Other.Name == Arg.Name; }))
					{
						Fail(TEXT("DNS2020"), Arg.Span, FText::Format(LOCTEXT("EmitArgTwice", "'{0}' is given twice"), FText::FromString(Arg.Name)));
					}
					Out.Add(MoveTemp(Arg));
					if (Tokens[Index].Is(EDnaTokenKind::Comma))
					{
						++Index;
						continue;
					}
					break;
				}
				if (!Tokens[Index].Is(EDnaTokenKind::RightParen))
				{
					return Fail(TEXT("DNS2007"), Tokens[Index].Span, LOCTEXT("EmitArgsClose", "Expected ',' or ')' in the arguments"));
				}
				++Index;
				return true;
			}

			void ParseCharacterHeader(FDnaLineNode& Node)
			{
				Node.Kind = EDnaLineKind::Character;
				TArray<FDnaToken> Tokens;
				Tokenize(Node, 0, Tokens);
				int32 Index = 1;
				if (ExpectIdentifier(Tokens, Index, LOCTEXT("CharacterName", "a character name"), Node.Payload.Name))
				{
					ParseHeaderTail(Node, Tokens, Index, /*bAllowPlayer=*/true, false, false);
				}
				ClassifyChildren(Node, EContext::Character);
			}

			/** Type and modifiers of a fact, after the colon. Shared by top-level and grouped facts. */
			bool ParseFactSpec(FDnaLineNode& Node, const TArray<FDnaToken>& Tokens, int32& Index, bool bAllowSource = false)
			{
				const FDnaToken& TypeToken = Tokens[FMath::Min(Index, Tokens.Num() - 1)];
				if (!TypeToken.Is(EDnaTokenKind::Identifier) || !::DreamNarrative::TryParseFactType(TypeToken.Text, Node.Payload.FactType))
				{
					return Fail(TEXT("DNS2007"), TypeToken.Span,
						FText::Format(LOCTEXT("FactType", "Expected a fact type (bool, int, float or tag), found {0}"),
							FText::FromString(TypeToken.Is(EDnaTokenKind::EndOfInput) ? TEXT("end of line") : TypeToken.Text)));
				}
				++Index;
				return ParseFactModifiers(Node, Tokens, Index, /*bAllowValueClauses=*/true, bAllowSource);
			}

			bool ParseFactModifiers(FDnaLineNode& Node, const TArray<FDnaToken>& Tokens, int32& Index, bool bAllowValueClauses, bool bAllowSource = false)
			{
				FDnaLinePayload& P = Node.Payload;
				while (!Tokens[Index].Is(EDnaTokenKind::EndOfInput))
				{
					const FDnaToken& Token = Tokens[Index];
					if (Token.IsWord(TEXT("extern"))) { P.bExtern = true; ++Index; continue; }
					if (Token.IsWord(TEXT("profile"))) { P.bProfile = true; ++Index; continue; }
					if (Token.IsWord(TEXT("progress"))) { P.bProgress = true; ++Index; continue; }
					if (Token.IsWord(TEXT("gameplay")))
					{
						++Index;
						if (!ExpectDottedName(Tokens, Index, LOCTEXT("FactGameplayTag", "a gameplay tag after 'gameplay'"), P.GameplayTag)) { return false; }
						continue;
					}
					if (bAllowSource && Token.IsWord(TEXT("from")))
					{
						++Index;
						if (Tokens[Index].IsWord(TEXT("magnitude")))
						{
							P.GameplaySource = EDnaGameplaySource::Magnitude;
							++Index;
							continue;
						}
						const bool bInstigator = Tokens[Index].IsWord(TEXT("instigator"));
						if ((bInstigator || Tokens[Index].IsWord(TEXT("target"))) && Index + 1 < Tokens.Num() && Tokens[Index + 1].IsWord(TEXT("tags")))
						{
							Index += 2;
							P.GameplaySource = bInstigator ? EDnaGameplaySource::InstigatorTags : EDnaGameplaySource::TargetTags;
							if (!ExpectDottedName(Tokens, Index, LOCTEXT("SourceTag", "a gameplay tag after 'tags'"), P.GameplaySourceTag)) { return false; }
							continue;
						}
						return Fail(TEXT("DNS2007"), Tokens[Index].Span, LOCTEXT("GameplaySource", "Expected magnitude, instigator tags or target tags after 'from'"));
					}
					if (Token.IsWord(TEXT("session"))) { P.bSession = true; ++Index; continue; }
					if (Token.IsWord(TEXT("server"))) { P.Authority = EDnaAuthority::Server; ++Index; continue; }
					if (Token.IsWord(TEXT("client"))) { P.Authority = EDnaAuthority::Client; ++Index; continue; }
					if (Token.IsWord(TEXT("shared"))) { P.Authority = EDnaAuthority::Shared; ++Index; continue; }
					if (Token.IsWord(TEXT("was")))
					{
						++Index;
						if (!ExpectIdentifier(Tokens, Index, LOCTEXT("OldName", "the previous name after 'was'"), P.WasName)) { return false; }
						continue;
					}
					if (bAllowValueClauses && Token.IsWord(TEXT("range")))
					{
						++Index;
						const FDnaToken& Low = Tokens[Index];
						if (!(Low.Is(EDnaTokenKind::Int) || Low.Is(EDnaTokenKind::Float)))
						{
							return Fail(TEXT("DNS2007"), Low.Span, LOCTEXT("RangeLow", "Expected a number after 'range'"));
						}
						++Index;
						if (!Tokens[Index].Is(EDnaTokenKind::DotDot))
						{
							return Fail(TEXT("DNS2007"), Tokens[Index].Span, LOCTEXT("RangeDots", "Expected '..' in the range"));
						}
						++Index;
						const FDnaToken& High = Tokens[Index];
						if (!(High.Is(EDnaTokenKind::Int) || High.Is(EDnaTokenKind::Float)))
						{
							return Fail(TEXT("DNS2007"), High.Span, LOCTEXT("RangeHigh", "Expected a number after '..'"));
						}
						++Index;
						P.bHasRange = true;
						P.RangeMin = Low.Real;
						P.RangeMax = High.Real;
						continue;
					}
					if (bAllowValueClauses && Token.IsKeyword(EDnaKeyword::In))
					{
						++Index;
						if (!Tokens[Index].Is(EDnaTokenKind::LeftParen))
						{
							return Fail(TEXT("DNS2007"), Tokens[Index].Span, LOCTEXT("DomainParen", "Expected '(' after 'in'"));
						}
						++Index;
						while (true)
						{
							FString Member;
							if (!ExpectIdentifier(Tokens, Index, LOCTEXT("DomainMember", "a member name"), Member)) { return false; }
							P.Domain.Add(Member);
							if (Tokens[Index].Is(EDnaTokenKind::Comma)) { ++Index; continue; }
							break;
						}
						if (!Tokens[Index].Is(EDnaTokenKind::RightParen))
						{
							return Fail(TEXT("DNS2007"), Tokens[Index].Span, LOCTEXT("DomainClose", "Expected ',' or ')' in the member list"));
						}
						++Index;
						continue;
					}
					if (bAllowValueClauses && Token.Is(EDnaTokenKind::Assign))
					{
						++Index;
						P.DefaultExpr = ExpectExpression(Tokens, Index);
						if (!P.DefaultExpr.IsValid()) { return false; }
						continue;
					}
					return ExpectEnd(Tokens, Index);
				}
				return true;
			}

			void ParseFactHeader(FDnaLineNode& Node)
			{
				TArray<FDnaToken> Tokens;
				Tokenize(Node, 0, Tokens);
				int32 Index = 1;
				if (!ExpectIdentifier(Tokens, Index, LOCTEXT("FactName", "a fact name"), Node.Payload.Name))
				{
					Node.Kind = EDnaLineKind::Unknown;
					MarkChildrenUnknown(Node);
					return;
				}
				if (Tokens[Index].Is(EDnaTokenKind::Colon))
				{
					Node.Kind = EDnaLineKind::Fact;
					++Index;
					ParseFactSpec(Node, Tokens, Index);
					RequireNoChildren(Node);
					return;
				}
				Node.Kind = EDnaLineKind::FactGroup;
				ParseFactModifiers(Node, Tokens, Index, /*bAllowValueClauses=*/false);
				RequireBody(Node);
				ClassifyChildren(Node, EContext::FactGroup);
			}

			void ClassifyFactEntry(FDnaLineNode& Node)
			{
				TArray<FDnaToken> Tokens;
				Tokenize(Node, 0, Tokens);
				int32 Index = 0;
				if (!Tokens[0].Is(EDnaTokenKind::Identifier) || !Tokens[1].Is(EDnaTokenKind::Colon))
				{
					Unexpected(Node, LOCTEXT("InFactGroup", "inside a fact group; expected '名: type ...'"));
					return;
				}
				Node.Kind = EDnaLineKind::Fact;
				Node.Payload.bInGroup = true;
				Node.Payload.Name = Tokens[0].Text;
				Index = 2;
				ParseFactSpec(Node, Tokens, Index);
				RequireNoChildren(Node);
			}

			void ParseChapterHeader(FDnaLineNode& Node)
			{
				Node.Kind = EDnaLineKind::Chapter;
				TArray<FDnaToken> Tokens;
				Tokenize(Node, 0, Tokens);
				int32 Index = 1;
				if (ExpectIdentifier(Tokens, Index, LOCTEXT("ChapterName", "a chapter name"), Node.Payload.Name))
				{
					ParseHeaderTail(Node, Tokens, Index, false, /*bAllowAfter=*/true, false);
				}
				ClassifyChildren(Node, EContext::Chapter);
			}

			void ParseQuestHeader(FDnaLineNode& Node)
			{
				Node.Kind = EDnaLineKind::Quest;
				TArray<FDnaToken> Tokens;
				Tokenize(Node, 0, Tokens);
				int32 Index = 1;
				if (ExpectIdentifier(Tokens, Index, LOCTEXT("QuestName", "a quest name"), Node.Payload.Name))
				{
					ParseHeaderTail(Node, Tokens, Index, false, false, false);
				}
				ClassifyChildren(Node, EContext::Quest);
			}

			void ParseDialogueHeader(FDnaLineNode& Node)
			{
				Node.Kind = EDnaLineKind::Dialogue;
				TArray<FDnaToken> Tokens;
				Tokenize(Node, 0, Tokens);
				int32 Index = 1;
				if (ExpectIdentifier(Tokens, Index, LOCTEXT("DialogueName", "a dialogue name"), Node.Payload.Name))
				{
					ParseHeaderTail(Node, Tokens, Index, false, false, /*bAllowDialogueClauses=*/true);
				}
				if (!Node.Payload.ForCharacter.IsEmpty() && !Node.Payload.OnEvent.IsEmpty())
				{
					Fail(TEXT("DNS2017"), Node.Raw.Span, LOCTEXT("ForAndOn", "A dialogue is an entry for a character (`for`) or for an event (`on`), not both"));
				}
				RequireBody(Node);
				ClassifyChildren(Node, EContext::Dialogue);
			}

			void ParseStageHeader(FDnaLineNode& Node)
			{
				Node.Kind = EDnaLineKind::Stage;
				TArray<FDnaToken> Tokens;
				Tokenize(Node, 0, Tokens);
				int32 Index = 1;
				if (ExpectIdentifier(Tokens, Index, LOCTEXT("StageName", "a stage name"), Node.Payload.Name))
				{
					ParseHeaderTail(Node, Tokens, Index, false, false, false);
				}
				ClassifyChildren(Node, EContext::Stage);
			}

			// ---------------------------------------------------------------- properties

			/**
			 * `key: value`. Returns false (without reporting) when the line is not shaped like one. With bTextTakesId
			 * the value may end in an `#id`, which is not part of it.
			 */
			bool TryParseProperty(FDnaLineNode& Node, bool bTextTakesId = false)
			{
				const FString& Content = Node.Raw.Content;
				const FString Word = FirstWord(Content);
				if (Word.IsEmpty())
				{
					return false;
				}
				const int32 ColonAt = SkipSpaces(Content, Word.Len());
				if (ColonAt >= Content.Len() || NormalizeDnaPunctuation(Content[ColonAt]) != TEXT(':'))
				{
					return false;
				}
				Node.Kind = EDnaLineKind::Property;
				Node.Payload.Name = Word;
				const int32 ValueStart = SkipSpaces(Content, ColonAt + 1);
				if (bTextTakesId)
				{
					ParseTextWithModifiers(Node, ValueStart, EModifierSet::Id);
					Node.Payload.Value = Node.Payload.Text.Raw;
				}
				else
				{
					Node.Payload.Value = Content.Mid(ValueStart);
					ParseText(Node, ValueStart, Content.Len(), Node.Payload.Text);
				}
				RequireNoChildren(Node);
				return true;
			}

			void ClassifyPropertyOnly(FDnaLineNode& Node)
			{
				if (!TryParseProperty(Node))
				{
					Unexpected(Node, LOCTEXT("InPropertyBlock", "here; expected 'key: value'"));
				}
			}

			/** An objective's properties; its `show:` text may end in an `#id`, the key it is translated under. */
			void ClassifyObjectiveProperty(FDnaLineNode& Node)
			{
				if (!TryParseProperty(Node, /*bTextTakesId=*/FirstWord(Node.Raw.Content) == TEXT("show")))
				{
					Unexpected(Node, LOCTEXT("InPropertyBlock", "here; expected 'key: value'"));
				}
			}

			// ---------------------------------------------------------------- chapter

			void ClassifyChapterLine(FDnaLineNode& Node)
			{
				const FString Word = FirstWord(Node.Raw.Content);
				if (Word == TEXT("implied"))
				{
					Node.Kind = EDnaLineKind::Implied;
					TArray<FDnaToken> Tokens;
					Tokenize(Node, 0, Tokens);
					ExpectEnd(Tokens, 1);
					RequireBody(Node);
					ClassifyChildren(Node, EContext::Implied);
					return;
				}
				if (Word == TEXT("advance"))
				{
					Node.Kind = EDnaLineKind::Advance;
					TArray<FDnaToken> Tokens;
					Tokenize(Node, 0, Tokens);
					int32 Index = 1;
					if (!Tokens[Index].Is(EDnaTokenKind::Arrow))
					{
						Fail(TEXT("DNS2007"), Tokens[Index].Span, LOCTEXT("AdvanceArrow", "Expected '->' after 'advance'"));
					}
					else
					{
						++Index;
						if (ExpectIdentifier(Tokens, Index, LOCTEXT("AdvanceTarget", "a chapter name after '->'"), Node.Payload.Target))
						{
							if (Tokens[Index].IsKeyword(EDnaKeyword::If))
							{
								++Index;
								Node.Payload.Condition = ExpectExpression(Tokens, Index);
							}
							ExpectEnd(Tokens, Index);
						}
					}
					RequireNoChildren(Node);
					return;
				}
				if (TryParseProperty(Node))
				{
					return;
				}
				Unexpected(Node, LOCTEXT("InChapter", "inside a chapter; expected 'title:', 'implied' or 'advance -> ...'"));
			}

			void ClassifyImplication(FDnaLineNode& Node)
			{
				TArray<FDnaToken> Tokens;
				Tokenize(Node, 0, Tokens);
				int32 Index = 0;
				if (!ExpectDottedName(Tokens, Index, LOCTEXT("ImpliedFact", "a fact name"), Node.Payload.Name))
				{
					Node.Kind = EDnaLineKind::Unknown;
					return;
				}
				Node.Kind = EDnaLineKind::Implication;
				if (!Tokens[Index].Is(EDnaTokenKind::Assign))
				{
					Fail(TEXT("DNS2007"), Tokens[Index].Span, LOCTEXT("ImpliedAssign", "Expected '=' after the fact name"));
					return;
				}
				++Index;
				Node.Payload.Expr = ExpectExpression(Tokens, Index);
				if (Node.Payload.Expr.IsValid())
				{
					ExpectEnd(Tokens, Index);
				}
				RequireNoChildren(Node);
			}

			// ---------------------------------------------------------------- quest

			void ClassifyQuestLine(FDnaLineNode& Node)
			{
				const FString& Content = Node.Raw.Content;
				const FString Word = FirstWord(Content);

				if (Word == TEXT("track"))
				{
					Node.Kind = EDnaLineKind::Flag;
					Node.Payload.Name = Word;
					TArray<FDnaToken> Tokens;
					Tokenize(Node, 0, Tokens);
					ExpectEnd(Tokens, 1);
					RequireNoChildren(Node);
					return;
				}
				if (Word == TEXT("journal"))
				{
					Node.Kind = EDnaLineKind::Journal;
					RequireBody(Node);
					ClassifyChildren(Node, EContext::Journal);
					return;
				}
				if (Word == TEXT("available") || Word == TEXT("fail"))
				{
					Node.Kind = Word == TEXT("available") ? EDnaLineKind::Available : EDnaLineKind::FailIf;
					TArray<FDnaToken> Tokens;
					Tokenize(Node, 0, Tokens);
					int32 Index = 1;
					if (!Tokens[Index].IsKeyword(EDnaKeyword::If))
					{
						Fail(TEXT("DNS2007"), Tokens[Index].Span,
							FText::Format(LOCTEXT("RuleIf", "Expected 'if' after '{0}'"), FText::FromString(Word)));
					}
					else
					{
						++Index;
						Node.Payload.Condition = ExpectExpression(Tokens, Index);
						if (Node.Payload.Condition.IsValid()) { ExpectEnd(Tokens, Index); }
					}
					RequireNoChildren(Node);
					return;
				}
				if (Word == TEXT("objective"))
				{
					Node.Kind = EDnaLineKind::Objective;
					TArray<FDnaToken> Tokens;
					Tokenize(Node, 0, Tokens);
					int32 Index = 1;
					Node.Payload.Condition = ExpectExpression(Tokens, Index);
					if (Node.Payload.Condition.IsValid())
					{
						if (Tokens[Index].IsWord(TEXT("optional")))
						{
							Node.Payload.bOptional = true;
							++Index;
						}
						ExpectEnd(Tokens, Index);
					}
					ClassifyChildren(Node, EContext::Objective);
					return;
				}
				if (Word == TEXT("on"))
				{
					Node.Kind = EDnaLineKind::On;
					TArray<FDnaToken> Tokens;
					Tokenize(Node, 0, Tokens);
					const FDnaToken& Which = Tokens[1];
					if (Which.IsWord(TEXT("accept"))) { Node.Payload.OnKind = EDnaOnKind::Accept; }
					else if (Which.IsWord(TEXT("complete"))) { Node.Payload.OnKind = EDnaOnKind::Complete; }
					else if (Which.IsWord(TEXT("fail"))) { Node.Payload.OnKind = EDnaOnKind::Fail; }
					else if (Which.IsWord(TEXT("timeout"))) { Node.Payload.OnKind = EDnaOnKind::Timeout; }
					else
					{
						Fail(TEXT("DNS2007"), Which.Span, LOCTEXT("OnKind", "Expected accept, complete, fail or timeout after 'on'"));
					}
					Node.Payload.Name = Which.Text;
					ExpectEnd(Tokens, 2);
					RequireBody(Node);
					ClassifyChildren(Node, EContext::On);
					return;
				}
				if (Word == TEXT("subquest"))
				{
					Node.Kind = EDnaLineKind::Subquest;
					TArray<FDnaToken> Tokens;
					Tokenize(Node, 0, Tokens);
					int32 Index = 1;
					if (ExpectIdentifier(Tokens, Index, LOCTEXT("SubquestName", "a quest name after 'subquest'"), Node.Payload.Name))
					{
						if (Tokens[Index].IsWord(TEXT("auto")))
						{
							Node.Payload.bAuto = true;
							++Index;
						}
						ExpectEnd(Tokens, Index);
					}
					RequireNoChildren(Node);
					return;
				}
				if (TryParseProperty(Node))
				{
					return;
				}
				Unexpected(Node, LOCTEXT("InQuest", "inside a quest"));
			}

			void ClassifyOnLine(FDnaLineNode& Node)
			{
				const FString Word = FirstWord(Node.Raw.Content);
				if (Word == TEXT("reward"))
				{
					Node.Kind = EDnaLineKind::Reward;
					TArray<FDnaToken> Tokens;
					Tokenize(Node, 0, Tokens);
					int32 Index = 1;
					if (!ExpectIdentifier(Tokens, Index, LOCTEXT("RewardCategory", "a reward category"), Node.Payload.RewardCategory))
					{
						return;
					}
					if (Tokens[Index].Is(EDnaTokenKind::Int))
					{
						Node.Payload.RewardAmount = static_cast<int32>(Tokens[Index].Integer);
						++Index;
					}
					else if (Tokens[Index].Is(EDnaTokenKind::Identifier))
					{
						Node.Payload.RewardItem = Tokens[Index].Text;
						++Index;
						const FDnaToken& Count = Tokens[Index];
						if (Count.Is(EDnaTokenKind::Identifier) && Count.Text.Len() > 1 && Count.Text[0] == TEXT('x') && Count.Text.Mid(1).IsNumeric())
						{
							Node.Payload.RewardAmount = FCString::Atoi(*Count.Text.Mid(1));
							++Index;
						}
					}
					else
					{
						Fail(TEXT("DNS2007"), Tokens[Index].Span, LOCTEXT("RewardWhat", "Expected an item name or an amount after the reward category"));
						return;
					}
					ExpectEnd(Tokens, Index);
					RequireNoChildren(Node);
					return;
				}
				if (TryParseCommand(Node))
				{
					return;
				}
				Unexpected(Node, LOCTEXT("InOn", "inside an 'on' block; expected a command or 'reward'"));
			}

			// ---------------------------------------------------------------- dialogue

			bool TryParseCommand(FDnaLineNode& Node)
			{
				const FString Word = FirstWord(Node.Raw.Content);
				EDnaCommandKind Kind = EDnaCommandKind::None;
				if (Word == TEXT("set")) { Kind = EDnaCommandKind::Set; }
				else if (Word == TEXT("accept")) { Kind = EDnaCommandKind::Accept; }
				else if (Word == TEXT("complete")) { Kind = EDnaCommandKind::Complete; }
				else if (Word == TEXT("fail")) { Kind = EDnaCommandKind::Fail; }
				else if (Word == TEXT("advance")) { Kind = EDnaCommandKind::Advance; }
				else if (Word == TEXT("play")) { Kind = EDnaCommandKind::Play; }
				else if (Word == TEXT("emit")) { Kind = EDnaCommandKind::Emit; }
				else if (Word == TEXT("wait")) { Kind = EDnaCommandKind::Wait; }
				else { return false; }

				Node.Kind = EDnaLineKind::Command;
				Node.Payload.Command = Kind;
				TArray<FDnaToken> Tokens;
				Tokenize(Node, 0, Tokens);
				int32 Index = 1;

				switch (Kind)
				{
				case EDnaCommandKind::Set:
				{
					if (!ExpectDottedName(Tokens, Index, LOCTEXT("SetTarget", "a fact name after 'set'"), Node.Payload.Name)) { break; }
					const FDnaToken& Op = Tokens[Index];
					if (!(Op.Is(EDnaTokenKind::Assign) || Op.Is(EDnaTokenKind::PlusAssign) || Op.Is(EDnaTokenKind::MinusAssign)))
					{
						Fail(TEXT("DNS2007"), Op.Span, LOCTEXT("SetOp", "Expected '=', '+=' or '-=' after the fact name"));
						break;
					}
					Node.Payload.AssignOp = Op.Kind;
					++Index;
					Node.Payload.Expr = ExpectExpression(Tokens, Index);
					if (Node.Payload.Expr.IsValid()) { ExpectEnd(Tokens, Index); }
					break;
				}
				case EDnaCommandKind::Accept:
				case EDnaCommandKind::Complete:
				case EDnaCommandKind::Fail:
				case EDnaCommandKind::Advance:
					if (ExpectIdentifier(Tokens, Index, LOCTEXT("CommandName", "a name"), Node.Payload.Name))
					{
						ExpectEnd(Tokens, Index);
					}
					break;
				case EDnaCommandKind::Emit:
					if (ExpectIdentifier(Tokens, Index, LOCTEXT("EmitName", "an event name after 'emit'"), Node.Payload.Name)
						&& ParseEmitArgs(Tokens, Index, Node.Payload.EmitArgs))
					{
						ExpectEnd(Tokens, Index);
					}
					break;
				case EDnaCommandKind::Play:
					if (ExpectIdentifier(Tokens, Index, LOCTEXT("PlayName", "a stage name after 'play'"), Node.Payload.Name))
					{
						if (Tokens[Index].IsWord(TEXT("async")))
						{
							Node.Payload.bAsync = true;
							++Index;
						}
						ExpectEnd(Tokens, Index);
					}
					break;
				case EDnaCommandKind::Wait:
					if (!Tokens[Index].Is(EDnaTokenKind::Duration))
					{
						Fail(TEXT("DNS2007"), Tokens[Index].Span, LOCTEXT("WaitDuration", "Expected a duration such as 1.5s after 'wait'"));
					}
					else
					{
						Node.Payload.Seconds = Tokens[Index].Real;
						++Index;
						ExpectEnd(Tokens, Index);
					}
					break;
				default:
					break;
				}
				RequireNoChildren(Node);
				return true;
			}

			/** `说话人(情绪, 动作): ` -- true when Content starts like a speech line. */
			bool TryMatchSpeaker(const FString& Content, FString& OutSpeaker, TArray<FString>& OutTags, int32& OutTextStart) const
			{
				const FString Word = FirstWord(Content);
				if (Word.IsEmpty() || IsDnaReservedWord(Word))
				{
					return false;
				}
				int32 Index = SkipSpaces(Content, Word.Len());
				TArray<FString> Tags;
				if (Index < Content.Len() && NormalizeDnaPunctuation(Content[Index]) == TEXT('('))
				{
					int32 Close = Index + 1;
					while (Close < Content.Len() && NormalizeDnaPunctuation(Content[Close]) != TEXT(')'))
					{
						++Close;
					}
					if (Close >= Content.Len())
					{
						return false;
					}
					FString Inside = Content.Mid(Index + 1, Close - Index - 1);
					for (int32 CharIndex = 0; CharIndex < Inside.Len(); ++CharIndex)
					{
						Inside[CharIndex] = NormalizeDnaPunctuation(Inside[CharIndex]);
					}
					TArray<FString> Split;
					Inside.ParseIntoArray(Split, TEXT(","), /*InCullEmpty=*/true);
					for (FString& Tag : Split)
					{
						Tag.TrimStartAndEndInline();
						if (!Tag.IsEmpty()) { Tags.Add(Tag); }
					}
					Index = SkipSpaces(Content, Close + 1);
				}
				if (Index >= Content.Len() || NormalizeDnaPunctuation(Content[Index]) != TEXT(':'))
				{
					return false;
				}
				OutSpeaker = Word;
				OutTags = MoveTemp(Tags);
				OutTextStart = SkipSpaces(Content, Index + 1);
				return true;
			}

			void ClassifyDialogueLine(FDnaLineNode& Node)
			{
				const FString& Content = Node.Raw.Content;

				if (Content.StartsWith(TEXT("->")))
				{
					Node.Kind = EDnaLineKind::Jump;
					TArray<FDnaToken> Tokens;
					Tokenize(Node, 0, Tokens);
					int32 Index = 1;
					if (Tokens[Index].IsWord(TEXT("end")))
					{
						Node.Payload.bEnd = true;
						++Index;
						ExpectEnd(Tokens, Index);
					}
					else if (ExpectDottedName(Tokens, Index, LOCTEXT("JumpTarget", "a dialogue name or 'end' after '->'"), Node.Payload.Target))
					{
						ExpectEnd(Tokens, Index);
					}
					RequireNoChildren(Node);
					return;
				}

				if (Content.StartsWith(TEXT("- ")) || Content == TEXT("-"))
				{
					Node.Kind = EDnaLineKind::Choice;
					const int32 TextStart = SkipSpaces(Content, 1);
					ParseTextWithModifiers(Node, TextStart, EModifierSet::Choice);
					ClassifyChildren(Node, EContext::Dialogue);
					return;
				}

				if (Content.StartsWith(TEXT("> ")) || Content == TEXT(">"))
				{
					Node.Kind = EDnaLineKind::Narration;
					const int32 TextStart = SkipSpaces(Content, 1);
					ParseTextWithModifiers(Node, TextStart, EModifierSet::Text);
					ClassifyContinuations(Node);
					return;
				}

				const FString Word = FirstWord(Content);
				if (Word == TEXT("if"))
				{
					Node.Kind = EDnaLineKind::If;
					ParseConditionAfterWord(Node, Word.Len());
					RequireBody(Node);
					ClassifyChildren(Node, EContext::Dialogue);
					return;
				}
				if (Word == TEXT("else"))
				{
					const int32 After = SkipSpaces(Content, Word.Len());
					if (WordAt(Content, After, TEXT("if")))
					{
						Node.Kind = EDnaLineKind::ElseIf;
						ParseConditionAfterWord(Node, After + 2);
					}
					else
					{
						Node.Kind = EDnaLineKind::Else;
						TArray<FDnaToken> Tokens;
						Tokenize(Node, 0, Tokens);
						ExpectEnd(Tokens, 1);
					}
					RequireBody(Node);
					ClassifyChildren(Node, EContext::Dialogue);
					return;
				}
				if (TryParseCommand(Node))
				{
					return;
				}
				if (!Word.IsEmpty() && IsDnaReservedWord(Word))
				{
					Unexpected(Node, FText::Format(LOCTEXT("KeywordInDialogue", "inside a dialogue: '{0}' is not a dialogue line"), FText::FromString(Word)));
					return;
				}

				FString Speaker;
				TArray<FString> Tags;
				int32 TextStart = 0;
				if (TryMatchSpeaker(Content, Speaker, Tags, TextStart))
				{
					Node.Kind = EDnaLineKind::Speech;
					Node.Payload.Name = Speaker;
					Node.Payload.SpeakerTags = MoveTemp(Tags);
					ParseTextWithModifiers(Node, TextStart, EModifierSet::Text);
					ClassifyContinuations(Node);
					return;
				}

				Node.Kind = EDnaLineKind::Narration;
				ParseTextWithModifiers(Node, 0, EModifierSet::Text);
				ClassifyContinuations(Node);
			}

			void ParseConditionAfterWord(FDnaLineNode& Node, int32 RelativeStart)
			{
				TArray<FDnaToken> Tokens;
				Tokenize(Node, RelativeStart, Tokens);
				int32 Index = 0;
				if (Tokens[0].Is(EDnaTokenKind::EndOfInput))
				{
					Fail(TEXT("DNS2007"), Node.Raw.Span, LOCTEXT("ConditionMissing", "Expected a condition after 'if'"));
					return;
				}
				Node.Payload.Condition = ExpectExpression(Tokens, Index);
				if (Node.Payload.Condition.IsValid())
				{
					ExpectEnd(Tokens, Index);
				}
			}

			// ---------------------------------------------------------------- text and modifiers

			/** Parses Content[Start, End) as text with `{expr}` interpolations into Out. */
			void ParseText(const FDnaLineNode& Node, int32 Start, int32 End, FDnaText& Out)
			{
				const FString& Content = Node.Raw.Content;
				End = FMath::Min(End, Content.Len());
				Out.Raw = Content.Mid(Start, FMath::Max(0, End - Start));
				Out.Span = SpanOf(Node, Start, FMath::Max(0, End - Start));
				Out.Parts.Reset();

				FString Literal;
				auto Flush = [&]()
				{
					if (!Literal.IsEmpty())
					{
						FDnaTextPart& Part = Out.Parts.AddDefaulted_GetRef();
						Part.Literal = Literal;
						Literal.Reset();
					}
				};

				int32 Index = Start;
				while (Index < End)
				{
					const TCHAR Char = Content[Index];
					if (Char == TEXT('\\') && Index + 1 < End
						&& (Content[Index + 1] == TEXT('{') || Content[Index + 1] == TEXT('}') || Content[Index + 1] == TEXT('\\')))
					{
						Literal.AppendChar(Content[Index + 1]);
						Index += 2;
						continue;
					}
					if (Char == TEXT('{') || Char == 0xFF5B)
					{
						int32 Close = Index + 1;
						while (Close < End && Content[Close] != TEXT('}') && Content[Close] != 0xFF5D)
						{
							++Close;
						}
						if (Close >= End)
						{
							Fail(TEXT("DNS2007"), SpanOf(Node, Index, End - Index), LOCTEXT("InterpolationOpen", "Expected '}' to close the interpolation"));
							Literal += Content.Mid(Index, End - Index);
							Index = End;
							break;
						}
						Flush();
						FDnaTextPart& Part = Out.Parts.AddDefaulted_GetRef();
						Part.Expr = ParseDnaExpressionSegment(Source, Abs(Node, Index + 1), Close - Index - 1, Diagnostics);
						if (!Part.Expr.IsValid())
						{
							bOk = false;
							Part.Literal = Content.Mid(Index, Close - Index + 1);
						}
						Index = Close + 1;
						continue;
					}
					Literal.AppendChar(Char);
					++Index;
				}
				Flush();
			}

			/** Text with a trailing modifier list. Decides where the text ends and the modifiers begin. */
			void ParseTextWithModifiers(FDnaLineNode& Node, int32 TextStart, EModifierSet Allowed)
			{
				const FString& Content = Node.Raw.Content;
				int32 TextEnd = Content.Len();
				bool bSawIfCandidate = false;

				for (int32 Index = TextStart; Index < Content.Len(); ++Index)
				{
					if (Index != TextStart && !IsDnaSpace(Content[Index - 1]))
					{
						continue;
					}
					const TCHAR Char = NormalizeDnaPunctuation(Content[Index]);
					const bool bIsIf = Allowed != EModifierSet::Id && WordAt(Content, Index, TEXT("if"));
					const bool bCandidate = Char == TEXT('#') || (Allowed != EModifierSet::Id && (Char == TEXT('@') || bIsIf
						|| WordAt(Content, Index, TEXT("once")) || WordAt(Content, Index, TEXT("for")) || WordAt(Content, Index, TEXT("voice"))));
					if (!bCandidate)
					{
						continue;
					}
					bSawIfCandidate |= bIsIf;

					FDnaModifiers Mods;
					FDnaDiagnosticSink Scratch(Diagnostics.GetFilePath());
					if (TryParseModifierTail(Node, Index, Allowed, Mods, Scratch))
					{
						Diagnostics.Append(MoveTemp(Scratch));
						Mods.TailOffset = Abs(Node, Index);
						Node.Payload.Modifiers = MoveTemp(Mods);
						TextEnd = Index;
						while (TextEnd > TextStart && IsDnaSpace(Content[TextEnd - 1]))
						{
							--TextEnd;
						}
						bSawIfCandidate = false;
						break;
					}
				}

				if (bSawIfCandidate)
				{
					Diagnostics.Info(TEXT("DNS2006"), Node.Raw.Span,
						LOCTEXT("IfAsText", "' if ' in this line was treated as text; a condition must parse and refer to a declared name"));
				}

				ParseText(Node, TextStart, TextEnd, Node.Payload.Text);
			}

			/**
			 * `#id` at Tokens[Index] (the `#`): false when no id follows. An id may go on with dots, the way generated
			 * ones do (`老周_初见.3`, `老周_初见.c2`): the pieces written against each other, with no space between,
			 * are one id. Index moves past it.
			 */
			bool ReadId(const TArray<FDnaToken>& Tokens, int32& Index, FString& OutId) const
			{
				if (!Tokens[Index].Is(EDnaTokenKind::Hash) || !Tokens[Index + 1].Is(EDnaTokenKind::Identifier))
				{
					return false;
				}
				const int32 IdStart = Tokens[Index + 1].Span.Offset;
				int32 IdEnd = Tokens[Index + 1].Span.End();
				int32 Next = Index + 2;
				while (Tokens[Next].Span.Offset == IdEnd && !Tokens[Next].Is(EDnaTokenKind::EndOfInput))
				{
					const bool bDotted = Tokens[Next].Is(EDnaTokenKind::Dot) && Tokens[Next + 1].Span.Offset == Tokens[Next].Span.End()
						&& (Tokens[Next + 1].Is(EDnaTokenKind::Identifier) || Tokens[Next + 1].Is(EDnaTokenKind::Int));
					// `.3` lexes as a number of its own.
					const bool bFraction = Tokens[Next].Is(EDnaTokenKind::Float) && Source.GetText()[Tokens[Next].Span.Offset] == TEXT('.');
					if (bDotted) { IdEnd = Tokens[Next + 1].Span.End(); Next += 2; }
					else if (bFraction) { IdEnd = Tokens[Next].Span.End(); Next += 1; }
					else { break; }
				}
				OutId = Source.GetText().Mid(IdStart, IdEnd - IdStart);
				Index = Next;
				return true;
			}

			/** Tries to read Content[Start, end) as a complete modifier list. Diagnostics go to Sink so a failed attempt leaves no trace. */
			bool TryParseModifierTail(const FDnaLineNode& Node, int32 Start, EModifierSet Allowed, FDnaModifiers& Out, FDnaDiagnosticSink& Sink)
			{
				TArray<FDnaToken> Tokens;
				TokenizeRange(Node, Start, Node.Raw.Content.Len(), Tokens, Sink);
				if (Sink.HasErrors())
				{
					return false;
				}

				bool bSeenIf = false, bSeenOnce = false, bSeenFor = false, bSeenStage = false, bSeenId = false, bSeenVoice = false;
				auto Duplicate = [&](bool& bSeen, const FDnaToken& Token)
				{
					if (bSeen)
					{
						Sink.Error(TEXT("DNS2004"), Token.Span,
							FText::Format(LOCTEXT("DuplicateModifier", "The '{0}' modifier appears twice on this line"), FText::FromString(Token.Text)));
					}
					bSeen = true;
				};
				auto NotAllowed = [&](const FDnaToken& Token)
				{
					Sink.Error(TEXT("DNS2009"), Token.Span,
						FText::Format(LOCTEXT("ModifierNotAllowed", "The '{0}' modifier is not allowed on this kind of line"), FText::FromString(Token.Text)));
				};

				int32 Index = 0;
				while (!Tokens[Index].Is(EDnaTokenKind::EndOfInput))
				{
					const FDnaToken& Token = Tokens[Index];
					if (Token.Is(EDnaTokenKind::Hash))
					{
						if (!ReadId(Tokens, Index, Out.Id)) { return false; }
						Duplicate(bSeenId, Token);
						continue;
					}
					if (Allowed == EModifierSet::Id)
					{
						return false;
					}
					if (Token.Is(EDnaTokenKind::At))
					{
						if (!Tokens[Index + 1].Is(EDnaTokenKind::Identifier)) { return false; }
						if (Allowed != EModifierSet::Text) { NotAllowed(Token); }
						Duplicate(bSeenStage, Token);
						Out.Stage = Tokens[Index + 1].Text;
						Index += 2;
						continue;
					}
					if (Token.IsKeyword(EDnaKeyword::Once))
					{
						if (Allowed != EModifierSet::Choice) { NotAllowed(Token); }
						Duplicate(bSeenOnce, Token);
						Out.bOnce = true;
						++Index;
						continue;
					}
					if (Token.IsKeyword(EDnaKeyword::For))
					{
						if (!Tokens[Index + 1].Is(EDnaTokenKind::Duration)) { return false; }
						if (Allowed != EModifierSet::Text) { NotAllowed(Token); }
						Duplicate(bSeenFor, Token);
						Out.bHasDuration = true;
						Out.DurationSeconds = Tokens[Index + 1].Real;
						Index += 2;
						continue;
					}
					if (Token.Is(EDnaTokenKind::Identifier) && Token.Text == TEXT("voice") && !Tokens[Index + 1].Is(EDnaTokenKind::EndOfInput))
					{
						// A path or a name, read as written up to the next space: `/Game/VO/老周/开场`, `VO_特殊版`.
						const FString& Text = Source.GetText();
						const int32 Start = Tokens[Index + 1].Span.Offset;
						int32 End = Start;
						while (End < Text.Len() && !IsDnaSpace(Text[End]) && Text[End] != TEXT('\n') && Text[End] != TEXT('\r')) { ++End; }
						if (Allowed != EModifierSet::Text) { NotAllowed(Token); }
						Duplicate(bSeenVoice, Token);
						Out.Voice = Text.Mid(Start, End - Start);
						Index += 1;
						while (!Tokens[Index].Is(EDnaTokenKind::EndOfInput) && Tokens[Index].Span.Offset < End) { ++Index; }
						continue;
					}
					if (Token.IsKeyword(EDnaKeyword::If))
					{
						++Index;
						FDnaExprPtr Expr = ParseDnaExpression(Tokens, Index, Sink);
						if (!Expr.IsValid() || !ReferencesKnownName(*Expr))
						{
							return false;
						}
						Duplicate(bSeenIf, Token);
						Out.Condition = Expr;
						continue;
					}
					return false;
				}
				return true;
			}

			// ---------------------------------------------------------------- stage

			void ClassifyStageLine(FDnaLineNode& Node)
			{
				const FString Word = FirstWord(Node.Raw.Content);
				if (Word == TEXT("duration"))
				{
					Node.Kind = EDnaLineKind::Duration;
					TArray<FDnaToken> Tokens;
					Tokenize(Node, 0, Tokens);
					if (!Tokens[1].Is(EDnaTokenKind::Duration))
					{
						Fail(TEXT("DNS2007"), Tokens[1].Span, LOCTEXT("DurationValue", "Expected a duration such as 6.0s after 'duration'"));
					}
					else
					{
						Node.Payload.Seconds = Tokens[1].Real;
						ExpectEnd(Tokens, 2);
					}
					RequireNoChildren(Node);
					return;
				}
				if (IsTrackName(Word))
				{
					Node.Kind = EDnaLineKind::Track;
					Node.Payload.Name = Word;
					TArray<FDnaToken> Tokens;
					Tokenize(Node, 0, Tokens);
					ExpectEnd(Tokens, 1);
					RequireBody(Node);
					ClassifyChildren(Node, EContext::Track);
					return;
				}
				if (Word == TEXT("shots") || Word == TEXT("marks") || Word == TEXT("restore"))
				{
					Node.Kind = Word == TEXT("shots") ? EDnaLineKind::Shots : Word == TEXT("marks") ? EDnaLineKind::Marks : EDnaLineKind::Restore;
					TArray<FDnaToken> Tokens;
					Tokenize(Node, 0, Tokens);
					ExpectEnd(Tokens, 1);
					RequireBody(Node);
					ClassifyChildren(Node, Word == TEXT("restore") ? EContext::Restore : EContext::Shots);
					return;
				}
				if (TryParseProperty(Node))
				{
					return;
				}
				Unexpected(Node, LOCTEXT("InStage", "inside a stage; expected duration, base:, a track, shots or restore"));
			}

			static bool IsTimeToken(const FDnaToken& Token)
			{
				return Token.Is(EDnaTokenKind::Duration) || Token.Is(EDnaTokenKind::Percent) || Token.Is(EDnaTokenKind::Int) || Token.Is(EDnaTokenKind::Float);
			}

			void ClassifyStageEntry(FDnaLineNode& Node)
			{
				TArray<FDnaToken> Tokens;
				Tokenize(Node, 0, Tokens);
				int32 Index = 0;
				if (!IsTimeToken(Tokens[0]))
				{
					Unexpected(Node, LOCTEXT("InTrack", "inside a track; an entry starts with a time such as 0.0s, 1.9s..2.1s or 34%"));
					return;
				}
				Node.Kind = EDnaLineKind::StageEntry;
				FDnaLinePayload& P = Node.Payload;
				P.bPercent = Tokens[0].Is(EDnaTokenKind::Percent);
				P.bBareTime = Tokens[0].Is(EDnaTokenKind::Int) || Tokens[0].Is(EDnaTokenKind::Float);
				P.TimeStart = Tokens[0].Real;
				++Index;
				if (Tokens[Index].Is(EDnaTokenKind::DotDot))
				{
					++Index;
					if (!IsTimeToken(Tokens[Index]))
					{
						Fail(TEXT("DNS2007"), Tokens[Index].Span, LOCTEXT("RangeEnd", "Expected a time after '..'"));
						return;
					}
					P.bIsRange = true;
					P.bEndPercent = Tokens[Index].Is(EDnaTokenKind::Percent);
					P.bBareTime |= Tokens[Index].Is(EDnaTokenKind::Int) || Tokens[Index].Is(EDnaTokenKind::Float);
					P.TimeEnd = Tokens[Index].Real;
					++Index;
				}
				if (!(Tokens[Index].Is(EDnaTokenKind::Identifier) || Tokens[Index].Is(EDnaTokenKind::Keyword)))
				{
					Fail(TEXT("DNS2007"), Tokens[Index].Span, LOCTEXT("EntryVerb", "Expected a verb after the time"));
					return;
				}
				P.Name = Tokens[Index].Text;
				++Index;
				for (; Index < Tokens.Num(); ++Index)
				{
					// A trailing `#id` is the key the entry's text is translated under, not an argument.
					int32 After = Index;
					FString Id;
					if (Tokens[Index].Is(EDnaTokenKind::Hash) && ReadId(Tokens, After, Id) && Tokens[After].Is(EDnaTokenKind::EndOfInput))
					{
						P.Modifiers.Id = Id;
						break;
					}
					if (!Tokens[Index].Is(EDnaTokenKind::EndOfInput)) { P.Args.Add(Tokens[Index]); }
				}
				RequireNoChildren(Node);
			}

			void ClassifyNamedEntry(FDnaLineNode& Node, EDnaLineKind Kind)
			{
				TArray<FDnaToken> Tokens;
				Tokenize(Node, 0, Tokens);
				if (!(Tokens[0].Is(EDnaTokenKind::Identifier) || Tokens[0].Is(EDnaTokenKind::Keyword)))
				{
					Unexpected(Node, LOCTEXT("InNamedBlock", "here; expected a name followed by its arguments"));
					return;
				}
				Node.Kind = Kind;
				Node.Payload.Name = Tokens[0].Text;
				for (int32 Index = 1; Index < Tokens.Num(); ++Index)
				{
					if (!Tokens[Index].Is(EDnaTokenKind::EndOfInput)) { Node.Payload.Args.Add(Tokens[Index]); }
				}
				RequireNoChildren(Node);
			}

			// ---------------------------------------------------------------- test

			/** `test 名` or `explore 名`: the same steps, and an explore block's properties besides. */
			void ParseTestHeader(FDnaLineNode& Node, EDnaLineKind Kind)
			{
				const bool bExplore = Kind == EDnaLineKind::Explore;
				Node.Kind = Kind;
				TArray<FDnaToken> Tokens;
				Tokenize(Node, 0, Tokens);
				int32 Index = 1;
				if (ExpectIdentifier(Tokens, Index, bExplore ? LOCTEXT("ExploreName", "a name for the exploration") : LOCTEXT("TestName", "a test name"), Node.Payload.Name))
				{
					ExpectEnd(Tokens, Index);
				}
				RequireBody(Node);
				ClassifyChildren(Node, bExplore ? EContext::Explore : EContext::Test);
			}

			void ClassifyTestStep(FDnaLineNode& Node, bool bExplore)
			{
				const FString& Content = Node.Raw.Content;
				const FString Word = FirstWord(Content);
				FDnaLinePayload& P = Node.Payload;
				TArray<FDnaToken> Tokens;
				int32 Index = 1;
				// Tokenised only once the line is known to be a step, so a stray line reports one error, not two.
				auto BeginStep = [this, &Node, &P, &Tokens](EDnaTestStepKind Kind)
				{
					Node.Kind = EDnaLineKind::TestStep;
					P.TestStep = Kind;
					Tokenize(Node, 0, Tokens);
				};

				if (Word == TEXT("seed"))
				{
					BeginStep(EDnaTestStepKind::Seed);
					if (!Tokens[Index].Is(EDnaTokenKind::Int))
					{
						Fail(TEXT("DNS2007"), Tokens[Index].Span, LOCTEXT("SeedInt", "Expected a whole number after 'seed'"));
					}
					else
					{
						P.Seed = static_cast<int32>(Tokens[Index].Integer);
						ExpectEnd(Tokens, Index + 1);
					}
					RequireNoChildren(Node);
					return;
				}
				if (Word == TEXT("given"))
				{
					BeginStep(EDnaTestStepKind::Given);
					ExpectEnd(Tokens, 1);
					RequireBody(Node);
					ClassifyChildren(Node, EContext::Given);
					return;
				}
				if (Word == TEXT("talk") || Word == TEXT("start"))
				{
					const bool bTalk = Word == TEXT("talk");
					BeginStep(bTalk ? EDnaTestStepKind::Talk : EDnaTestStepKind::Start);
					const FText What = bTalk ? LOCTEXT("TalkName", "a character name after 'talk'") : LOCTEXT("StartName", "a dialogue name after 'start'");
					if (ExpectIdentifier(Tokens, Index, What, P.Name))
					{
						if (Tokens[Index].IsWord(TEXT("strict")))
						{
							P.bStrict = true;
							++Index;
						}
						ExpectEnd(Tokens, Index);
					}
					// No body is fine: `talk 村长` alone runs the conversation and expects nothing of it.
					ClassifyChildren(Node, EContext::TestDialogue);
					return;
				}
				if (Word == TEXT("expect"))
				{
					BeginStep(EDnaTestStepKind::Expect);
					if (Tokens[Index].Is(EDnaTokenKind::EndOfInput))
					{
						Fail(TEXT("DNS2007"), Node.Raw.Span, LOCTEXT("ExpectCondition", "Expected a condition after 'expect'"));
					}
					else
					{
						P.Condition = ExpectExpression(Tokens, Index);
						if (P.Condition.IsValid()) { ExpectEnd(Tokens, Index); }
					}
					RequireNoChildren(Node);
					return;
				}
				if (Word == TEXT("set"))
				{
					BeginStep(EDnaTestStepKind::Set);
					if (ExpectDottedName(Tokens, Index, LOCTEXT("TestSetTarget", "a fact name after 'set'"), P.Name))
					{
						const FDnaToken& Op = Tokens[Index];
						if (!(Op.Is(EDnaTokenKind::Assign) || Op.Is(EDnaTokenKind::PlusAssign) || Op.Is(EDnaTokenKind::MinusAssign)))
						{
							Fail(TEXT("DNS2007"), Op.Span, LOCTEXT("TestSetOp", "Expected '=', '+=' or '-=' after the fact name"));
						}
						else
						{
							P.AssignOp = Op.Kind;
							++Index;
							P.Expr = ExpectExpression(Tokens, Index);
							if (P.Expr.IsValid()) { ExpectEnd(Tokens, Index); }
						}
					}
					RequireNoChildren(Node);
					return;
				}
				if (Word == TEXT("wait"))
				{
					BeginStep(EDnaTestStepKind::Wait);
					if (!Tokens[Index].Is(EDnaTokenKind::Duration))
					{
						Fail(TEXT("DNS2007"), Tokens[Index].Span, LOCTEXT("TestWait", "Expected a duration such as 20m after 'wait'"));
					}
					else
					{
						P.Seconds = Tokens[Index].Real;
						ExpectEnd(Tokens, Index + 1);
					}
					RequireNoChildren(Node);
					return;
				}
				if (Word == TEXT("jump"))
				{
					BeginStep(EDnaTestStepKind::Jump);
					if (ExpectIdentifier(Tokens, Index, LOCTEXT("JumpName", "a chapter name after 'jump'"), P.Target))
					{
						ExpectEnd(Tokens, Index);
					}
					RequireNoChildren(Node);
					return;
				}
				if (Word == TEXT("newgame"))
				{
					BeginStep(EDnaTestStepKind::NewGame);
					ExpectEnd(Tokens, 1);
					RequireNoChildren(Node);
					return;
				}
				if (Word == TEXT("select"))
				{
					BeginStep(EDnaTestStepKind::Select);
					if (ExpectIdentifier(Tokens, Index, LOCTEXT("SelectName", "a chapter name after 'select'"), P.Target))
					{
						ExpectEnd(Tokens, Index);
					}
					RequireNoChildren(Node);
					return;
				}
				if (Word == TEXT("archive"))
				{
					// `archive 资料: 文本` -- the text is read as text -- or `archive 资料 = n`.
					Node.Kind = EDnaLineKind::TestStep;
					P.TestStep = EDnaTestStepKind::Archive;
					P.Count = INDEX_NONE;
					int32 End = SkipSpaces(Content, Word.Len());
					const int32 NameStart = End;
					while (End < Content.Len())
					{
						const FString Part = FirstWord(Content.Mid(End));
						End += Part.Len();
						if (Part.IsEmpty() || End >= Content.Len() || Content[End] != TEXT('.'))
						{
							break;
						}
						++End;
					}
					P.Name = Content.Mid(NameStart, End - NameStart);
					const int32 After = SkipSpaces(Content, End);
					const TCHAR Mark = After < Content.Len() ? NormalizeDnaPunctuation(Content[After]) : TEXT('\0');
					if (!P.Name.IsEmpty() && Mark == TEXT(':'))
					{
						ParseText(Node, SkipSpaces(Content, After + 1), Content.Len(), P.Text);
						ClassifyContinuations(Node);
						return;
					}
					const FString Number = Content.Mid(SkipSpaces(Content, After + 1)).TrimEnd();
					if (!P.Name.IsEmpty() && Mark == TEXT('=') && !Number.IsEmpty() && Number.IsNumeric() && !Number.Contains(TEXT(".")) && !Number.StartsWith(TEXT("-")))
					{
						P.Count = FCString::Atoi(*Number);
					}
					else
					{
						Fail(TEXT("DNS2007"), Node.Raw.Span, LOCTEXT("ArchiveShape", "Expected 'archive 资料: 文本' or 'archive 资料 = 条数'"));
					}
					RequireNoChildren(Node);
					return;
				}
				if (Word == TEXT("save"))
				{
					BeginStep(EDnaTestStepKind::Save);
					ExpectEnd(Tokens, 1);
					RequireNoChildren(Node);
					return;
				}
				if (Word == TEXT("rolls"))
				{
					BeginStep(EDnaTestStepKind::Rolls);
					ParseRolls(Tokens, Index, P.Rolls);
					RequireNoChildren(Node);
					return;
				}
				if (Word == TEXT("accept") || Word == TEXT("complete") || Word == TEXT("fail"))
				{
					BeginStep(Word == TEXT("accept") ? EDnaTestStepKind::QuestAccept : Word == TEXT("complete") ? EDnaTestStepKind::QuestComplete : EDnaTestStepKind::QuestFail);
					if (ExpectIdentifier(Tokens, Index, FText::Format(LOCTEXT("QuestStepName", "a quest name after '{0}'"), FText::FromString(Word)), P.Name))
					{
						ExpectEnd(Tokens, Index);
					}
					RequireNoChildren(Node);
					return;
				}
				if (Word == TEXT("emit"))
				{
					BeginStep(EDnaTestStepKind::Emit);
					if (ExpectIdentifier(Tokens, Index, LOCTEXT("TestEmitName", "an event name after 'emit'"), P.Name)
						&& ParseEmitArgs(Tokens, Index, P.EmitArgs))
					{
						if (Tokens[Index].IsWord(TEXT("hold")))
						{
							P.bHold = true;
							++Index;
						}
						ExpectEnd(Tokens, Index);
					}
					// No body is fine: `emit 闲置` alone raises it and expects nothing of what it opens.
					ClassifyChildren(Node, EContext::TestDialogue);
					return;
				}
				if (Word == TEXT("journal"))
				{
					// `journal 任务: 文本` -- the text is read as text, not tokens.
					Node.Kind = EDnaLineKind::TestStep;
					P.TestStep = EDnaTestStepKind::Journal;
					const int32 NameStart = SkipSpaces(Content, Word.Len());
					P.Name = FirstWord(Content.Mid(NameStart));
					const int32 Colon = SkipSpaces(Content, NameStart + P.Name.Len());
					if (P.Name.IsEmpty() || Colon >= Content.Len() || NormalizeDnaPunctuation(Content[Colon]) != TEXT(':'))
					{
						Fail(TEXT("DNS2007"), Node.Raw.Span, LOCTEXT("JournalShape", "Expected 'journal 任务: 文本'"));
						RequireNoChildren(Node);
						return;
					}
					ParseText(Node, SkipSpaces(Content, Colon + 1), Content.Len(), P.Text);
					ClassifyContinuations(Node);
					return;
				}
				const bool bProperty = Word == TEXT("depth") || Word == TEXT("always") || Word == TEXT("eventually") || Word == TEXT("softlock");
				if (bProperty && !bExplore)
				{
					Unexpected(Node, FText::Format(LOCTEXT("PropertyInTest", "inside a test; '{0}' belongs to an explore block"), FText::FromString(Word)));
					return;
				}
				if (Word == TEXT("depth"))
				{
					BeginStep(EDnaTestStepKind::Depth);
					if (!Tokens[Index].Is(EDnaTokenKind::Int) || Tokens[Index].Integer < 1 || Tokens[Index].Integer > 1000)
					{
						Fail(TEXT("DNS2007"), Tokens[Index].Span, LOCTEXT("DepthInt", "Expected a whole number from 1 to 1000 after 'depth'"));
					}
					else
					{
						P.Depth = static_cast<int32>(Tokens[Index].Integer);
						ExpectEnd(Tokens, Index + 1);
					}
					RequireNoChildren(Node);
					return;
				}
				if (Word == TEXT("always") || Word == TEXT("eventually"))
				{
					BeginStep(Word == TEXT("always") ? EDnaTestStepKind::Always : EDnaTestStepKind::Eventually);
					if (Tokens[Index].Is(EDnaTokenKind::EndOfInput))
					{
						Fail(TEXT("DNS2007"), Node.Raw.Span, FText::Format(LOCTEXT("PropertyCondition", "Expected a condition after '{0}'"), FText::FromString(Word)));
					}
					else
					{
						P.Condition = ExpectExpression(Tokens, Index);
						if (P.Condition.IsValid()) { ExpectEnd(Tokens, Index); }
					}
					RequireNoChildren(Node);
					return;
				}
				if (Word == TEXT("softlock"))
				{
					BeginStep(EDnaTestStepKind::Softlock);
					if (Tokens[Index].Is(EDnaTokenKind::EndOfInput))
					{
						Fail(TEXT("DNS2007"), Node.Raw.Span, LOCTEXT("SoftlockGoal", "Expected a condition or a quest after 'softlock'"));
					}
					while (!Tokens[Index].Is(EDnaTokenKind::EndOfInput))
					{
						const FDnaExprPtr Goal = ExpectExpression(Tokens, Index);
						if (!Goal.IsValid()) { break; }
						P.Goals.Add(Goal);
						if (Tokens[Index].Is(EDnaTokenKind::Comma))
						{
							++Index;
							continue;
						}
						ExpectEnd(Tokens, Index);
						break;
					}
					RequireNoChildren(Node);
					return;
				}
				Unexpected(Node, Content.StartsWith(TEXT("-"))
					? (bExplore
						? LOCTEXT("PickOutsideTalkExplore", "directly in an explore block; a choice to take belongs under 'talk' or 'start'")
						: LOCTEXT("PickOutsideTalk", "directly in a test; a choice to take belongs under 'talk' or 'start'"))
					: (bExplore
						? LOCTEXT("InExplore", "inside an explore block; expected seed, given, talk, start, emit, expect, set, wait, jump, save, rolls, accept, complete, fail, journal, depth, always, eventually or softlock")
						: LOCTEXT("InTest", "inside a test; expected seed, given, talk, start, emit, expect, set, wait, jump, save, rolls, accept, complete, fail or journal")));
			}

			/** `low`, `high` or a number in [0, 1), separated by commas. */
			void ParseRolls(const TArray<FDnaToken>& Tokens, int32 Index, TArray<float>& Out)
			{
				for (;;)
				{
					const FDnaToken& Token = Tokens[Index];
					const bool bNumber = Token.Is(EDnaTokenKind::Int) || Token.Is(EDnaTokenKind::Float);
					const double Number = Token.Is(EDnaTokenKind::Int) ? static_cast<double>(Token.Integer) : Token.Real;
					if (Token.IsWord(TEXT("low")))
					{
						Out.Add(::DreamNarrative::LowestDraw);
					}
					else if (Token.IsWord(TEXT("high")))
					{
						Out.Add(::DreamNarrative::HighestDraw);
					}
					else if (bNumber && Number >= 0.0 && Number < 1.0)
					{
						Out.Add(static_cast<float>(Number));
					}
					else
					{
						Fail(TEXT("DNS2007"), Token.Span, LOCTEXT("RollValue", "Expected low, high or a number from 0 up to (not including) 1"));
						return;
					}
					++Index;
					if (!Tokens[Index].Is(EDnaTokenKind::Comma))
					{
						ExpectEnd(Tokens, Index);
						return;
					}
					++Index;
				}
			}

			/** Under `given`: `fact = value`, or `chapter = 章节`, `visited(对话) = n`, `once #选项id`. */
			void ClassifyGivenEntry(FDnaLineNode& Node)
			{
				const FString& Content = Node.Raw.Content;
				const FString Word = FirstWord(Content);
				FDnaLinePayload& P = Node.Payload;
				if (Word == TEXT("once"))
				{
					Node.Kind = EDnaLineKind::TestStep;
					P.TestStep = EDnaTestStepKind::GivenOnce;
					const int32 Hash = SkipSpaces(Content, 4);
					if (Hash >= Content.Len() || NormalizeDnaPunctuation(Content[Hash]) != TEXT('#'))
					{
						Fail(TEXT("DNS2007"), SpanOf(Node, FMath::Min(Hash, Content.Len()), 1), LOCTEXT("OnceId", "Expected '#' and a choice id after 'once'"));
					}
					else
					{
						ParseLineIdReference(Node, Hash + 1, LOCTEXT("OnceChoiceId", "a choice id after '#'"));
					}
					RequireNoChildren(Node);
					return;
				}
				if (Word == TEXT("quest"))
				{
					// `quest 任务 = 状态 [剩余时长]`
					Node.Kind = EDnaLineKind::TestStep;
					P.TestStep = EDnaTestStepKind::GivenQuest;
					TArray<FDnaToken> Tokens;
					Tokenize(Node, 0, Tokens);
					int32 Index = 1;
					static const TCHAR* States[] = { TEXT("inactive"), TEXT("accepted"), TEXT("going"), TEXT("completed"), TEXT("failed"), TEXT("timedout") };
					if (ExpectIdentifier(Tokens, Index, LOCTEXT("GivenQuestName", "a quest name after 'quest'"), P.Name))
					{
						bool bState = false;
						if (Tokens[Index].Is(EDnaTokenKind::Assign))
						{
							for (const TCHAR* State : States) { bState |= Tokens[Index + 1].Text == State; }
						}
						if (!bState)
						{
							Fail(TEXT("DNS2007"), Tokens[Index].Span, LOCTEXT("GivenQuestState", "Expected '=' and inactive, accepted, going, completed, failed or timedout"));
						}
						else
						{
							P.Value = Tokens[Index + 1].Text;
							Index += 2;
							if (Tokens[Index].Is(EDnaTokenKind::Duration))
							{
								P.Seconds = Tokens[Index].Real;
								++Index;
							}
							ExpectEnd(Tokens, Index);
						}
					}
					RequireNoChildren(Node);
					return;
				}
				if (Word != TEXT("chapter") && Word != TEXT("visited"))
				{
					ClassifyImplication(Node);
					return;
				}
				Node.Kind = EDnaLineKind::TestStep;
				TArray<FDnaToken> Tokens;
				Tokenize(Node, 0, Tokens);
				int32 Index = 1;
				if (Word == TEXT("chapter"))
				{
					P.TestStep = EDnaTestStepKind::GivenChapter;
					if (!Tokens[Index].Is(EDnaTokenKind::Assign))
					{
						Fail(TEXT("DNS2007"), Tokens[Index].Span, LOCTEXT("GivenChapterAssign", "Expected '=' after 'chapter'"));
					}
					else if (ExpectIdentifier(Tokens, ++Index, LOCTEXT("GivenChapterName", "a chapter name"), P.Target))
					{
						ExpectEnd(Tokens, Index);
					}
				}
				else
				{
					P.TestStep = EDnaTestStepKind::GivenVisited;
					if (!Tokens[Index].Is(EDnaTokenKind::LeftParen))
					{
						Fail(TEXT("DNS2007"), Tokens[Index].Span, LOCTEXT("GivenVisitedParen", "Expected '(' and a dialogue name after 'visited'"));
					}
					else if (ExpectIdentifier(Tokens, ++Index, LOCTEXT("GivenVisitedName", "a dialogue name"), P.Target))
					{
						if (!Tokens[Index].Is(EDnaTokenKind::RightParen) || !Tokens[Index + 1].Is(EDnaTokenKind::Assign)
							|| !Tokens[Index + 2].Is(EDnaTokenKind::Int) || Tokens[Index + 2].Integer < 0)
						{
							Fail(TEXT("DNS2007"), Tokens[Index].Span, LOCTEXT("GivenVisitedCount", "Expected ') = ' and a whole number of visits"));
						}
						else
						{
							P.Count = static_cast<int32>(Tokens[Index + 2].Integer);
							ExpectEnd(Tokens, Index + 3);
						}
					}
				}
				RequireNoChildren(Node);
			}

			/**
			 * Under a test's `talk` / `start` / `emit`: a choice to take, a line that must (not) be shown, or
			 * a `channel` block for the lines another channel shows (not inside another one).
			 */
			void ClassifyTestExpectation(FDnaLineNode& Node, bool bInChannel)
			{
				const FString& Content = Node.Raw.Content;
				FDnaLinePayload& P = Node.Payload;
				if (WordAt(Content, 0, TEXT("channel")))
				{
					if (bInChannel)
					{
						Unexpected(Node, LOCTEXT("ChannelInChannel", "inside a channel block; channel blocks sit directly under the step"));
						return;
					}
					Node.Kind = EDnaLineKind::TestStep;
					P.TestStep = EDnaTestStepKind::ChannelLines;
					TArray<FDnaToken> Tokens;
					Tokenize(Node, 0, Tokens);
					int32 Index = 1;
					if (ExpectIdentifier(Tokens, Index, LOCTEXT("TestChannelName", "a channel name after 'channel'"), P.Name))
					{
						if (Tokens[Index].IsWord(TEXT("strict")))
						{
							P.bStrict = true;
							++Index;
						}
						ExpectEnd(Tokens, Index);
					}
					ClassifyChildren(Node, EContext::TestChannel);
					return;
				}
				Node.Kind = EDnaLineKind::TestStep;

				if (Content.StartsWith(TEXT("- ")) || Content == TEXT("-"))
				{
					P.TestStep = EDnaTestStepKind::Pick;
					const int32 Start = SkipSpaces(Content, 1);
					if (Start < Content.Len() && NormalizeDnaPunctuation(Content[Start]) == TEXT('#'))
					{
						ParseLineIdReference(Node, Start + 1, LOCTEXT("PickId", "a choice id after '#'"));
					}
					else
					{
						ParseText(Node, Start, Content.Len(), P.Text);
					}
					RequireNoChildren(Node);
					return;
				}

				int32 Start = 0;
				P.TestStep = EDnaTestStepKind::ExpectLine;
				if (WordAt(Content, 0, TEXT("absent")))
				{
					P.TestStep = EDnaTestStepKind::Absent;
					Start = SkipSpaces(Content, 6);
					if (Content.Mid(Start).StartsWith(TEXT("- ")) || Content.Mid(Start) == TEXT("-"))
					{
						// `absent - 文本`: a choice that must not be offered.
						P.bChoice = true;
						const int32 LabelStart = SkipSpaces(Content, Start + 1);
						if (LabelStart < Content.Len() && NormalizeDnaPunctuation(Content[LabelStart]) == TEXT('#'))
						{
							ParseLineIdReference(Node, LabelStart + 1, LOCTEXT("AbsentChoiceId", "a choice id after '#'"));
						}
						else
						{
							ParseText(Node, LabelStart, Content.Len(), P.Text);
						}
						RequireNoChildren(Node);
						return;
					}
				}

				if (Start < Content.Len() && NormalizeDnaPunctuation(Content[Start]) == TEXT('#'))
				{
					ParseLineIdReference(Node, Start + 1, LOCTEXT("LineId", "a line id after '#'"));
					RequireNoChildren(Node);
					return;
				}
				const FString Rest = Content.Mid(Start);
				if (Rest.StartsWith(TEXT("> ")) || Rest == TEXT(">"))
				{
					ParseText(Node, SkipSpaces(Content, Start + 1), Content.Len(), P.Text);
					ClassifyContinuations(Node);
					return;
				}
				FString Speaker;
				TArray<FString> Tags;
				int32 TextStart = 0;
				if (TryMatchSpeaker(Rest, Speaker, Tags, TextStart))
				{
					P.Name = Speaker;
					P.SpeakerTags = MoveTemp(Tags);
					ParseText(Node, Start + TextStart, Content.Len(), P.Text);
				}
				else
				{
					ParseText(Node, Start, Content.Len(), P.Text);
				}
				ClassifyContinuations(Node);
			}

			/**
			 * `#id` into Payload.Target. Read as characters, not tokens: generated ids are `<node>.<n>`
			 * and `<node>.c<n>`, and the tokenizer would read `.1` as a number.
			 */
			void ParseLineIdReference(FDnaLineNode& Node, int32 RelativeStart, const FText& What)
			{
				const FString& Content = Node.Raw.Content;
				int32 End = RelativeStart;
				while (End < Content.Len() && (IsDnaIdentifierChar(Content[End]) || Content[End] == TEXT('.')))
				{
					++End;
				}
				if (End == RelativeStart || !IsDnaIdentifierStart(Content[RelativeStart]))
				{
					Fail(TEXT("DNS2007"), SpanOf(Node, RelativeStart, FMath::Max(1, Content.Len() - RelativeStart)),
						FText::Format(LOCTEXT("ExpectedId", "Expected {0}"), What));
					return;
				}
				Node.Payload.Target = Content.Mid(RelativeStart, End - RelativeStart);
				const int32 After = SkipSpaces(Content, End);
				if (After < Content.Len())
				{
					Fail(TEXT("DNS2008"), SpanOf(Node, After, Content.Len() - After),
						FText::Format(LOCTEXT("AfterId", "Unexpected '{0}' after the id"), FText::FromString(Content.Mid(After))));
				}
			}
		};
	}

	// ---------------------------------------------------------------- declared names

	void CollectDnaDeclaredNamesFromScan(const FDnaScanResult& Scan, TSet<FString>& OutNames)
	{
		FString OpenGroup;
		for (const FDnaRawLine& Line : Scan.Lines)
		{
			if (Line.IsFloating())
			{
				continue;
			}
			if (Line.IndentLevel == 0)
			{
				OpenGroup.Reset();
				const FString Word = FirstWord(Line.Content);
				if (!IsDeclarationWord(Word))
				{
					continue;
				}
				const int32 NameStart = SkipSpaces(Line.Content, Word.Len());
				const FString Name = FirstWord(Line.Content.Mid(NameStart));
				if (Name.IsEmpty())
				{
					continue;
				}
				OutNames.Add(Name);
				if (Word == TEXT("fact"))
				{
					const int32 After = SkipSpaces(Line.Content, NameStart + Name.Len());
					if (After >= Line.Content.Len() || NormalizeDnaPunctuation(Line.Content[After]) != TEXT(':'))
					{
						OpenGroup = Name;
					}
				}
			}
			else if (!OpenGroup.IsEmpty())
			{
				const FString Child = FirstWord(Line.Content);
				if (!Child.IsEmpty())
				{
					OutNames.Add(OpenGroup + TEXT(".") + Child);
				}
			}
		}
	}

	void CollectDnaDeclaredNames(const FDnaSourceText& Source, TSet<FString>& OutNames)
	{
		FDnaScanResult Scan;
		FDnaDiagnosticSink Scratch;
		ScanDnaLines(Source, Scan, Scratch);
		CollectDnaDeclaredNamesFromScan(Scan, OutNames);
	}

	// ---------------------------------------------------------------- entry points

	bool ParseDnaDocument(const FDnaSourceText& Source, const FDnaParseOptions& Options, FDnaDocument& OutDocument, FDnaDiagnosticSink& Diagnostics)
	{
		FParser Parser(Source, Options, OutDocument, Diagnostics);
		return Parser.Run();
	}

	bool ParseDnaFile(const FString& FilePath, const FDnaParseOptions& Options, FDnaDocument& OutDocument, FDnaDiagnosticSink& Diagnostics)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *FilePath))
		{
			return Diagnostics.Error(TEXT("DNS9002"), FDnaSpan(),
				FText::Format(LOCTEXT("CannotRead", "Cannot read '{0}'"), FText::FromString(FilePath)));
		}
		const FDnaSourceText Source(FilePath, MoveTemp(Text));
		return ParseDnaDocument(Source, Options, OutDocument, Diagnostics);
	}
}

#undef LOCTEXT_NAMESPACE
