// Copyright DreamMoon. All Rights Reserved.

#include "Lang/DnaValidator.h"
#include "Lang/DnaLexer.h"
#include "Lang/DnaStageCompiler.h"

#define LOCTEXT_NAMESPACE "DreamNarrativeLang"

namespace UE::DreamNarrative::Lang
{
	FDnaTypeInfo FDnaTypeInfo::OfFact(const FDnaFactInfo& Fact)
	{
		FDnaTypeInfo Type;
		switch (Fact.Type)
		{
		case EDnaFactType::Bool: Type.Kind = EKind::Bool; break;
		case EDnaFactType::Int: Type.Kind = EKind::Int; break;
		case EDnaFactType::Float: Type.Kind = EKind::Float; break;
		case EDnaFactType::Tag: Type.Kind = EKind::Tag; Type.TagFact = &Fact; break;
		}
		return Type;
	}

	const TCHAR* FDnaTypeInfo::Describe() const
	{
		switch (Kind)
		{
		case EKind::Unknown: return TEXT("unknown");
		case EKind::Bool: return TEXT("bool");
		case EKind::Int: return TEXT("int");
		case EKind::Float: return TEXT("float");
		case EKind::Tag: return TEXT("tag");
		case EKind::Chapter: return TEXT("chapter");
		}
		return TEXT("?");
	}

	namespace
	{
		using EKind = FDnaTypeInfo::EKind;

		class FValidator
		{
		public:
			FValidator(const FDnaProgram& InProgram, FDnaDiagnosticSink& InDiagnostics)
				: Program(InProgram), Diagnostics(InDiagnostics)
			{
			}

			bool Run()
			{
				for (const TSharedPtr<FDnaDocument>& Document : Program.Documents)
				{
					if (!Document.IsValid()) { continue; }
					CurrentDocument = Document.Get();
					Diagnostics.SetFilePath(Document->Path);
					for (const FDnaLineNodePtr& Root : Document->Roots)
					{
						if (Root.IsValid() && !Root->IsFloating())
						{
							CheckRoot(*Root);
						}
					}
				}
				return bOk;
			}

			bool RunOnExpression(const FDnaExprPtr& Expr, bool bAsCondition)
			{
				if (!Expr.IsValid())
				{
					return false;
				}
				if (bAsCondition)
				{
					CheckCondition(Expr);
				}
				else
				{
					CheckExpr(*Expr);
				}
				return bOk;
			}

		private:
			const FDnaProgram& Program;
			FDnaDiagnosticSink& Diagnostics;
			const FDnaDocument* CurrentDocument = nullptr;
			/** The event whose parameters may be read here: that of the dialogue being checked, when it opens `on` one. */
			const FDnaEventInfo* CurrentEvent = nullptr;
			bool bOk = true;

			/** Where a line's modifier tail starts, for diagnostics about the modifiers. */
			FDnaSpan TailSpan(const FDnaModifiers& Mods) const
			{
				if (CurrentDocument && Mods.TailOffset != INDEX_NONE)
				{
					return CurrentDocument->Source.MakeSpan(Mods.TailOffset, 1);
				}
				return FDnaSpan();
			}

			bool Fail(const TCHAR* Code, const FDnaSpan& Span, const FText& Message)
			{
				bOk = false;
				return Diagnostics.Error(Code, Span, Message);
			}

			// ---------------------------------------------------------------- roots

			void CheckRoot(FDnaLineNode& Root)
			{
				const FDnaLinePayload& P = Root.Payload;
				switch (Root.Kind)
				{
				case EDnaLineKind::Fact:
					CheckFactDefault(Root, P.Name);
					break;
				case EDnaLineKind::FactGroup:
				{
					TArray<FDnaLineNode*> Entries;
					Root.GetContentChildren(Entries);
					for (FDnaLineNode* Entry : Entries)
					{
						if (Entry->Kind == EDnaLineKind::Fact)
						{
							CheckFactDefault(*Entry, P.Name + TEXT(".") + Entry->Payload.Name);
						}
					}
					break;
				}
				case EDnaLineKind::Chapter:
					CheckChapter(Root);
					break;
				case EDnaLineKind::Quest:
					CheckQuest(Root);
					break;
				case EDnaLineKind::Dialogue:
					CheckDialogue(Root);
					break;
				case EDnaLineKind::Stage:
				{
					FDnaStageData Stage;
					bOk &= CompileDnaStage(Root, Stage, Diagnostics);
					break;
				}
				case EDnaLineKind::Channel:
					CheckChannel(Root);
					break;
				case EDnaLineKind::Event:
					CheckEvent(Root);
					break;
				case EDnaLineKind::Archive:
					CheckArchive(Root);
					break;
				case EDnaLineKind::Test:
				case EDnaLineKind::Explore:
					CheckTest(Root);
					break;
				default:
					break;
				}
			}

			/** `set fact = / += / -= value`, in a dialogue or in a test. */
			void CheckSetValue(const FDnaFactInfo& Fact, EDnaTokenKind AssignOp, const FDnaExprPtr& Expr, const FDnaSpan& LineSpan)
			{
				if (AssignOp == EDnaTokenKind::Assign)
				{
					CheckAssignment(Fact, Expr, LineSpan);
					return;
				}
				if (Fact.Type != EDnaFactType::Int && Fact.Type != EDnaFactType::Float)
				{
					Fail(TEXT("DNS4001"), LineSpan,
						FText::Format(LOCTEXT("CompoundOnNonNumber", "'{0}' is a {1} fact; '+=' and '-=' only apply to int and float facts"),
							FText::FromString(Fact.Name), FText::FromString(::DreamNarrative::LexToString(Fact.Type))));
				}
				const FDnaTypeInfo Value = CheckExpr(*Expr);
				if (!Value.IsUnknown() && !Value.IsNumeric())
				{
					Fail(TEXT("DNS4001"), Expr->Span,
						FText::Format(LOCTEXT("CompoundValue", "Expected a number here, found {0}"), FText::FromString(Value.Describe())));
				}
			}

			// ---------------------------------------------------------------- test

			void CheckTest(FDnaLineNode& Root)
			{
				TArray<FDnaLineNode*> Steps;
				Root.GetContentChildren(Steps);
				for (FDnaLineNode* Step : Steps)
				{
					if (Step->Kind != EDnaLineKind::TestStep) { continue; }
					const FDnaLinePayload& P = Step->Payload;
					switch (P.TestStep)
					{
					case EDnaTestStepKind::Given:
					{
						TArray<FDnaLineNode*> Entries;
						Step->GetContentChildren(Entries);
						for (const FDnaLineNode* Entry : Entries)
						{
							if (Entry->Kind == EDnaLineKind::TestStep && Entry->Payload.TestStep == EDnaTestStepKind::GivenChapter)
							{
								RequireChapter(Entry->Payload.Target, Entry->Raw.Span);
								continue;
							}
							if (Entry->Kind == EDnaLineKind::TestStep && Entry->Payload.TestStep == EDnaTestStepKind::GivenVisited)
							{
								RequireDialogue(Entry->Payload.Target, Entry->Raw.Span);
								continue;
							}
							if (Entry->Kind == EDnaLineKind::TestStep && Entry->Payload.TestStep == EDnaTestStepKind::GivenQuest)
							{
								RequireQuest(Entry->Payload.Name, Entry->Raw.Span);
								continue;
							}
							if (Entry->Kind != EDnaLineKind::Implication) { continue; }
							if (const FDnaFactInfo* Fact = RequireFact(Entry->Payload.Name, Entry->Raw.Span))
							{
								CheckAssignment(*Fact, Entry->Payload.Expr, Entry->Raw.Span);
							}
						}
						break;
					}
					case EDnaTestStepKind::Talk:
						RequireCharacter(P.Name, Step->Raw.Span, /*bIsSpeaker=*/false);
						CheckTestExpectations(*Step);
						break;
					case EDnaTestStepKind::Start:
						RequireDialogue(P.Name, Step->Raw.Span);
						CheckTestExpectations(*Step);
						break;
					case EDnaTestStepKind::Emit:
						CheckEmit(P.Name, P.EmitArgs, Step->Raw.Span);
						CheckTestExpectations(*Step);
						break;
					case EDnaTestStepKind::Expect:
						if (P.Condition.IsValid()) { CheckCondition(P.Condition); }
						break;
					case EDnaTestStepKind::Set:
					{
						const FDnaFactInfo* Fact = RequireFact(P.Name, Step->Raw.Span);
						if (!Fact || !P.Expr.IsValid()) { break; }
						if (!Fact->bExtern)
						{
							Fail(TEXT("DNS9101"), Step->Raw.Span,
								FText::Format(LOCTEXT("TestSetsNarrativeFact", "'{0}' belongs to the narrative, and 'set' in a test writes as the game does; put the starting value under 'given'"),
									FText::FromString(Fact->Name)));
						}
						CheckSetValue(*Fact, P.AssignOp, P.Expr, Step->Raw.Span);
						break;
					}
					case EDnaTestStepKind::Jump:
					case EDnaTestStepKind::Select:
						RequireChapter(P.Target, Step->Raw.Span);
						break;
					case EDnaTestStepKind::Archive:
						if (!Program.IsArchive(P.Name))
						{
							Fail(TEXT("DNS3014"), Step->Raw.Span, FText::Format(LOCTEXT("UnknownArchive", "No archive called '{0}' exists"), FText::FromString(P.Name)));
						}
						CheckText(P.Text);
						break;
					case EDnaTestStepKind::Always:
					case EDnaTestStepKind::Eventually:
						if (P.Condition.IsValid()) { CheckCondition(P.Condition); }
						break;
					case EDnaTestStepKind::QuestAccept:
					case EDnaTestStepKind::QuestComplete:
					case EDnaTestStepKind::QuestFail:
						RequireQuest(P.Name, Step->Raw.Span);
						break;
					case EDnaTestStepKind::Journal:
						RequireQuest(P.Name, Step->Raw.Span);
						CheckText(P.Text);
						break;
					case EDnaTestStepKind::Softlock:
						for (const FDnaExprPtr& Goal : P.Goals)
						{
							// A bare quest name stands for the quest's completion.
							if (Goal.IsValid() && !IsBareQuest(*Goal)) { CheckCondition(Goal); }
						}
						break;
					default:
						break;
					}
				}
			}

			bool IsBareQuest(const FDnaExpr& Expr) const
			{
				return Expr.Kind == EDnaExprKind::Name && Expr.Path.Num() == 1 && Program.IsQuest(Expr.Path[0]);
			}

			/** The lines under a test's talk / start / emit (and its channel blocks): speakers must be characters, interpolations must check. */
			void CheckTestExpectations(FDnaLineNode& Step)
			{
				TArray<FDnaLineNode*> Lines;
				Step.GetContentChildren(Lines);
				for (FDnaLineNode* Line : Lines)
				{
					if (Line->Kind != EDnaLineKind::TestStep) { continue; }
					const FDnaLinePayload& P = Line->Payload;
					if (P.TestStep == EDnaTestStepKind::ChannelLines)
					{
						RequireChannel(P.Name, Line->Raw.Span);
						CheckTestExpectations(*Line);
						continue;
					}
					const bool bLine = P.TestStep == EDnaTestStepKind::ExpectLine || P.TestStep == EDnaTestStepKind::Absent;
					if (bLine && !P.Name.IsEmpty())
					{
						RequireCharacter(P.Name, Line->Raw.Span, /*bIsSpeaker=*/true);
					}
					CheckText(P.Text);
					for (const FDnaLineNodePtr& Child : Line->Children)
					{
						if (Child.IsValid() && Child->Kind == EDnaLineKind::Continuation)
						{
							CheckText(Child->Payload.Text);
						}
					}
				}
			}

			void CheckFactDefault(const FDnaLineNode& Node, const FString& FullName)
			{
				const FDnaFactInfo* Fact = Program.FindFact(FullName);
				if (Fact && ((Fact->bSession && Fact->bProfile) || (Fact->bProgress && (Fact->bSession || Fact->bProfile))))
				{
					Fail(TEXT("DNS2023"), Node.Raw.Span, FText::Format(LOCTEXT("ScopeConflict",
						"'{0}' is kept in one place: the save slot (the default, which `progress` refines), the session or the profile"), FText::FromString(FullName)));
				}
				if (Fact && !Fact->GameplayTag.IsEmpty())
				{
					// The ability system writes it, as the game.
					if (!Fact->bExtern)
					{
						Fail(TEXT("DNS2021"), Node.Raw.Span, FText::Format(LOCTEXT("GameplayFactExtern",
							"'{0}' is fed from a gameplay tag, so the game writes it; declare it extern"), FText::FromString(FullName)));
					}
					if (Fact->Type != EDnaFactType::Bool && Fact->Type != EDnaFactType::Int)
					{
						Fail(TEXT("DNS4007"), Node.Raw.Span, FText::Format(LOCTEXT("GameplayFactType",
							"A gameplay tag feeds a bool (whether it is there) or an int (how many there are), not a {0}"), FText::FromString(::DreamNarrative::LexToString(Fact->Type))));
					}
				}
				if (!Fact || !Fact->DefaultExpr.IsValid())
				{
					return;
				}
				CheckAssignment(*Fact, Fact->DefaultExpr, Node.Raw.Span);
				if (Fact->Type == EDnaFactType::Tag && Fact->Domain.Num() == 0)
				{
					Fail(TEXT("DNS4002"), Node.Raw.Span,
						FText::Format(LOCTEXT("TagNoDomain", "Tag fact '{0}' declares no members; add `in (...)`"), FText::FromString(FullName)));
				}
			}

			void CheckChapter(FDnaLineNode& Root)
			{
				const FDnaLinePayload& P = Root.Payload;
				if (!P.Target.IsEmpty())
				{
					RequireChapter(P.Target, Root.Raw.Span);
				}
				TArray<FDnaLineNode*> Lines;
				Root.GetContentChildren(Lines);
				for (FDnaLineNode* Line : Lines)
				{
					if (Line->Kind == EDnaLineKind::Implied)
					{
						TArray<FDnaLineNode*> Implications;
						Line->GetContentChildren(Implications);
						for (const FDnaLineNode* Implication : Implications)
						{
							if (Implication->Kind != EDnaLineKind::Implication) { continue; }
							if (const FDnaFactInfo* Fact = RequireFact(Implication->Payload.Name, Implication->Raw.Span))
							{
								CheckAssignment(*Fact, Implication->Payload.Expr, Implication->Raw.Span);
							}
						}
					}
					else if (Line->Kind == EDnaLineKind::Advance)
					{
						RequireChapter(Line->Payload.Target, Line->Raw.Span);
						if (Line->Payload.Condition.IsValid()) { CheckCondition(Line->Payload.Condition); }
					}
				}
			}

			void CheckQuest(FDnaLineNode& Root)
			{
				TArray<FDnaLineNode*> Lines;
				Root.GetContentChildren(Lines);
				int32 Objectives = 0;
				for (FDnaLineNode* Line : Lines)
				{
					const FDnaLinePayload& P = Line->Payload;
					switch (Line->Kind)
					{
					case EDnaLineKind::Available:
					case EDnaLineKind::FailIf:
					case EDnaLineKind::Objective:
						if (P.Condition.IsValid()) { CheckCondition(P.Condition); }
						if (Line->Kind == EDnaLineKind::Objective)
						{
							CheckTexts(*Line);
							// The done objectives are kept as the bits of one int.
							if (++Objectives == 32)
							{
								Fail(TEXT("DNS2014"), Line->Raw.Span, LOCTEXT("TooManyObjectives", "A quest can have at most 31 objectives; split it into subquests"));
							}
						}
						break;
					case EDnaLineKind::Journal:
						CheckTexts(*Line);
						break;
					case EDnaLineKind::On:
					{
						CheckCommandBlock(*Line);
						TArray<FDnaLineNode*> Commands;
						Line->GetContentChildren(Commands);
						for (const FDnaLineNode* Command : Commands)
						{
							if (Command->Kind == EDnaLineKind::Command && Command->Payload.Command == EDnaCommandKind::Wait)
							{
								Fail(TEXT("DNS2013"), Command->Raw.Span, LOCTEXT("WaitInOn", "An 'on' block runs all at once; 'wait' belongs in a dialogue"));
							}
						}
						break;
					}
					case EDnaLineKind::Subquest:
						RequireQuest(P.Name, Line->Raw.Span);
						break;
					case EDnaLineKind::Property:
					{
						CheckText(P.Text);
						if (P.Name == TEXT("priority") && P.Value != TEXT("main") && P.Value != TEXT("side") && P.Value != TEXT("hidden"))
						{
							Fail(TEXT("DNS2011"), Line->Raw.Span, FText::Format(LOCTEXT("QuestPriority", "'{0}' is not a priority; expected main, side or hidden"), FText::FromString(P.Value)));
						}
						double Seconds = 0.0;
						if (P.Name == TEXT("timeout") && (!TryParseDnaDuration(P.Value, Seconds) || Seconds <= 0.0))
						{
							Fail(TEXT("DNS2012"), Line->Raw.Span, FText::Format(LOCTEXT("QuestTimeout", "'{0}' is not a duration; write a timeout such as 20m, 90s or 1.5h"), FText::FromString(P.Value)));
						}
						break;
					}
					default:
						break;
					}
				}
			}

			// ---------------------------------------------------------------- dialogue

			void CheckDialogue(FDnaLineNode& Root)
			{
				const FDnaLinePayload& P = Root.Payload;
				if (!P.ForCharacter.IsEmpty())
				{
					RequireCharacter(P.ForCharacter, Root.Raw.Span, /*bIsSpeaker=*/false);
				}
				if (!P.OnEvent.IsEmpty())
				{
					RequireEvent(P.OnEvent, Root.Raw.Span);
				}
				if (!P.Channel.IsEmpty())
				{
					RequireChannel(P.Channel, Root.Raw.Span);
				}
				// Only the dialogue an event opens reads the event's parameters: in its condition and its own lines.
				TGuardValue<const FDnaEventInfo*> Scope(CurrentEvent, P.OnEvent.IsEmpty() ? nullptr : Program.FindEvent(P.OnEvent));
				if (P.Condition.IsValid())
				{
					CheckCondition(P.Condition);
				}
				CheckDialogueBlock(Root);

				const FDnaChannelInfo* Channel = Program.FindChannel(P.Channel);
				if (Channel && Channel->bAutoAdvance)
				{
					if (const FDnaLineNode* Choice = FindChoice(Root))
					{
						Fail(TEXT("DNS2018"), Choice->Raw.Span, FText::Format(
							LOCTEXT("ChoiceOnAutoChannel", "Channel '{0}' moves on by itself, so a dialogue on it cannot offer choices; run this one on a channel with `advance: player`"),
							FText::FromString(Channel->Name)));
					}
				}
			}

			static const FDnaLineNode* FindChoice(const FDnaLineNode& Block)
			{
				for (const FDnaLineNodePtr& Child : Block.Children)
				{
					if (!Child.IsValid()) { continue; }
					if (Child->Kind == EDnaLineKind::Choice) { return Child.Get(); }
					if (const FDnaLineNode* Nested = FindChoice(*Child)) { return Nested; }
				}
				return nullptr;
			}

			// ---------------------------------------------------------------- channels and events

			void CheckChannel(FDnaLineNode& Root)
			{
				const FString& Name = Root.Payload.Name;
				const bool bMain = Name == TEXT("main");
				TArray<FDnaLineNode*> Lines;
				Root.GetContentChildren(Lines);
				for (FDnaLineNode* Line : Lines)
				{
					if (Line->Kind != EDnaLineKind::Property) { continue; }
					const FString& Key = Line->Payload.Name;
					const FString Value = Line->Payload.Value.TrimStartAndEnd();
					if (Key == TEXT("input") || Key == TEXT("advance") || Key == TEXT("busy"))
					{
						if (bMain)
						{
							Fail(TEXT("DNS2016"), Line->Raw.Span, LOCTEXT("MainFixed",
								"main always locks input, waits for the player and lets a new dialogue cut in; a `channel main` block sets only pauses and stops"));
							continue;
						}
						const TCHAR* Expected = Key == TEXT("input") ? TEXT("lock or free") : Key == TEXT("advance") ? TEXT("player or auto") : TEXT("queue, interrupt or drop");
						const bool bKnown = Key == TEXT("input") ? (Value == TEXT("lock") || Value == TEXT("free"))
							: Key == TEXT("advance") ? (Value == TEXT("player") || Value == TEXT("auto"))
							: (Value == TEXT("queue") || Value == TEXT("interrupt") || Value == TEXT("drop"));
						if (!bKnown)
						{
							Fail(TEXT("DNS2015"), Line->Raw.Span, FText::Format(LOCTEXT("ChannelValue", "'{0}' is not a value of {1}; expected {2}"),
								FText::FromString(Value), FText::FromString(Key), FText::FromString(Expected)));
						}
						continue;
					}
					if (Key == TEXT("stage"))
					{
						if (Value != TEXT("pause") && Value != TEXT("continue"))
						{
							Fail(TEXT("DNS2015"), Line->Raw.Span, FText::Format(LOCTEXT("ChannelStage", "'{0}' is not a value of stage; expected pause or continue"), FText::FromString(Value)));
						}
						continue;
					}
					if (Key == TEXT("pauses") || Key == TEXT("stops"))
					{
						const TArray<FString> Names = SplitDnaNameList(Value);
						if (Names.Num() == 0)
						{
							Fail(TEXT("DNS2015"), Line->Raw.Span, FText::Format(LOCTEXT("ChannelList", "{0} takes channel names separated by commas"), FText::FromString(Key)));
						}
						for (const FString& Other : Names)
						{
							if (Other == TEXT("main"))
							{
								Fail(TEXT("DNS2015"), Line->Raw.Span, LOCTEXT("MainHeld", "No channel pauses or stops main: the player's conversation is never cut by another channel"));
							}
							else if (Other == Name)
							{
								Fail(TEXT("DNS2015"), Line->Raw.Span, LOCTEXT("SelfHeld", "A channel cannot pause or stop itself; `busy` says what it does with what arrives while it talks"));
							}
							else
							{
								RequireChannel(Other, Line->Raw.Span);
							}
						}
						continue;
					}
					Fail(TEXT("DNS2015"), Line->Raw.Span, FText::Format(LOCTEXT("ChannelKey", "'{0}' is not a channel property; a channel has input, advance, busy, pauses, stops and stage"), FText::FromString(Key)));
				}
			}

			void CheckEvent(FDnaLineNode& Root)
			{
				const FDnaEventInfo* Event = Program.FindEvent(Root.Payload.Name);
				if (!Event || Event->Node != &Root)
				{
					return;
				}
				for (const FDnaFactInfo& Param : Event->Params)
				{
					if (Param.DefaultExpr.IsValid())
					{
						CheckAssignment(Param, Param.DefaultExpr, Param.Node->Raw.Span);
					}
					if (Param.Source != EDnaGameplaySource::None)
					{
						if (Event->GameplayTag.IsEmpty())
						{
							Fail(TEXT("DNS2022"), Param.Node->Raw.Span, FText::Format(LOCTEXT("SourceWithoutGameplay",
								"'{0}' takes its value from a gameplay event, but event '{1}' names none; add `gameplay 标签` to its header"),
								FText::FromString(Param.Name), FText::FromString(Event->Name)));
						}
						const bool bFits = Param.Source == EDnaGameplaySource::Magnitude
							? (Param.Type == EDnaFactType::Int || Param.Type == EDnaFactType::Float)
							: (Param.Type == EDnaFactType::Bool || Param.Type == EDnaFactType::Tag);
						if (!bFits)
						{
							Fail(TEXT("DNS4007"), Param.Node->Raw.Span, FText::Format(LOCTEXT("SourceType",
								"A {0} parameter cannot take its value from {1}: magnitude feeds an int or a float, tags feed a bool or a tag"),
								FText::FromString(::DreamNarrative::LexToString(Param.Type)),
								FText::FromString(Param.Source == EDnaGameplaySource::Magnitude ? TEXT("magnitude") : TEXT("tags"))));
						}
					}
					if (Param.Type == EDnaFactType::Tag && Param.Domain.Num() == 0)
					{
						Fail(TEXT("DNS4002"), Param.Node->Raw.Span,
							FText::Format(LOCTEXT("TagParamNoDomain", "Tag parameter '{0}' declares no members; add `in (...)`"), FText::FromString(Param.Name)));
					}
				}
			}

			/** `emit 事件(参数 = 值, ...)`, in a dialogue, an `on` block or a test. */
			void CheckEmit(const FString& Name, const TArray<FDnaEmitArg>& Args, const FDnaSpan& Span)
			{
				const FDnaEventInfo* Event = Program.FindEvent(Name);
				if (!Event)
				{
					// An event only the game listens to needs no declaration -- until it carries values.
					if (Args.Num() > 0)
					{
						Fail(TEXT("DNS3011"), Span, FText::Format(LOCTEXT("UndeclaredEventArgs",
							"Event '{0}' is not declared; declare it with its parameters to send values with it"), FText::FromString(Name)));
					}
					return;
				}
				for (const FDnaEmitArg& Arg : Args)
				{
					const FDnaFactInfo* Param = Event->FindParam(Arg.Name);
					if (!Param)
					{
						Fail(TEXT("DNS3013"), Arg.Span, FText::Format(LOCTEXT("UnknownParamArg", "Event '{0}' has no parameter '{1}'"),
							FText::FromString(Name), FText::FromString(Arg.Name)));
						if (Arg.Value.IsValid()) { CheckExpr(*Arg.Value); }
						continue;
					}
					CheckAssignment(*Param, Arg.Value, Arg.Span);
				}
			}

			void CheckDialogueBlock(FDnaLineNode& Block)
			{
				TArray<FDnaLineNode*> Lines;
				Block.GetContentChildren(Lines);

				// else must follow an if; choice groups are runs of consecutive choices
				EDnaLineKind Previous = EDnaLineKind::Unknown;
				int32 GroupStart = INDEX_NONE;
				auto CloseGroup = [&](int32 EndExclusive)
				{
					if (GroupStart == INDEX_NONE) { return; }
					bool bHasFallback = false;
					for (int32 Index = GroupStart; Index < EndExclusive; ++Index)
					{
						const FDnaModifiers& Mods = Lines[Index]->Payload.Modifiers;
						if (!Mods.Condition.IsValid() && !Mods.bOnce)
						{
							bHasFallback = true;
							break;
						}
					}
					if (!bHasFallback)
					{
						Fail(TEXT("DNS5008"), Lines[GroupStart]->Raw.Span,
							LOCTEXT("ChoiceNoFallback", "Every option in this group is conditional or `once`; add one plain option so the group cannot run dry"));
					}
					GroupStart = INDEX_NONE;
				};

				for (int32 Index = 0; Index < Lines.Num(); ++Index)
				{
					FDnaLineNode& Line = *Lines[Index];
					if (Line.Kind == EDnaLineKind::Choice)
					{
						if (GroupStart == INDEX_NONE) { GroupStart = Index; }
					}
					else
					{
						CloseGroup(Index);
					}

					if ((Line.Kind == EDnaLineKind::Else || Line.Kind == EDnaLineKind::ElseIf)
						&& !(Previous == EDnaLineKind::If || Previous == EDnaLineKind::ElseIf))
					{
						Fail(TEXT("DNS2010"), Line.Raw.Span, LOCTEXT("ElseWithoutIf", "'else' has no 'if' directly before it"));
					}
					Previous = Line.Kind;
					CheckDialogueLine(Line);
				}
				CloseGroup(Lines.Num());
			}

			void CheckDialogueLine(FDnaLineNode& Line)
			{
				const FDnaLinePayload& P = Line.Payload;
				switch (Line.Kind)
				{
				case EDnaLineKind::Speech:
					RequireCharacter(P.Name, Line.Raw.Span, /*bIsSpeaker=*/true);
					CheckTextLine(Line);
					break;
				case EDnaLineKind::Narration:
					CheckTextLine(Line);
					break;
				case EDnaLineKind::Choice:
					CheckText(P.Text);
					CheckModifiers(P.Modifiers);
					CheckDialogueBlock(Line);
					break;
				case EDnaLineKind::If:
				case EDnaLineKind::ElseIf:
					if (P.Condition.IsValid()) { CheckCondition(P.Condition); }
					CheckDialogueBlock(Line);
					break;
				case EDnaLineKind::Else:
					CheckDialogueBlock(Line);
					break;
				case EDnaLineKind::Jump:
					if (!P.bEnd) { RequireDialogue(P.Target, Line.Raw.Span); }
					break;
				case EDnaLineKind::Command:
					CheckCommand(Line);
					break;
				default:
					break;
				}
			}

			void CheckTextLine(FDnaLineNode& Line)
			{
				CheckText(Line.Payload.Text);
				CheckModifiers(Line.Payload.Modifiers);
				for (const FDnaLineNodePtr& Child : Line.Children)
				{
					if (Child.IsValid() && Child->Kind == EDnaLineKind::Continuation)
					{
						CheckText(Child->Payload.Text);
					}
				}
			}

			void CheckArchive(FDnaLineNode& Root)
			{
				TArray<FDnaLineNode*> Lines;
				Root.GetContentChildren(Lines);
				int32 Entries = 0;
				for (FDnaLineNode* Line : Lines)
				{
					if (Line->Kind == EDnaLineKind::Property)
					{
						CheckText(Line->Payload.Text);
						continue;
					}
					if (Line->Kind != EDnaLineKind::ArchiveEntry)
					{
						continue;
					}
					if (Line->Payload.Condition.IsValid()) { CheckCondition(Line->Payload.Condition); }
					CheckContinuations(*Line);
					// What is unlocked is kept as the bits of one int.
					if (++Entries == 32)
					{
						Fail(TEXT("DNS2024"), Line->Raw.Span, LOCTEXT("TooManyEntries", "An archive can have at most 31 entries; split it in two"));
					}
				}
			}

			void CheckContinuations(const FDnaLineNode& Node)
			{
				for (const FDnaLineNodePtr& Child : Node.Children)
				{
					if (Child.IsValid() && Child->Kind == EDnaLineKind::Continuation)
					{
						CheckText(Child->Payload.Text);
						CheckContinuations(*Child);
					}
				}
			}

			void CheckTexts(FDnaLineNode& Block)
			{
				TArray<FDnaLineNode*> Lines;
				Block.GetContentChildren(Lines);
				for (const FDnaLineNode* Line : Lines)
				{
					if (Line->Kind == EDnaLineKind::Property) { CheckText(Line->Payload.Text); }
				}
			}

			void CheckText(const FDnaText& Text)
			{
				for (const FDnaTextPart& Part : Text.Parts)
				{
					if (Part.IsExpr())
					{
						CheckExpr(*Part.Expr);
					}
				}
			}

			void CheckModifiers(const FDnaModifiers& Mods)
			{
				if (Mods.Condition.IsValid())
				{
					CheckCondition(Mods.Condition);
				}
				if (!Mods.Stage.IsEmpty())
				{
					RequireStage(Mods.Stage, TailSpan(Mods));
				}
			}

			void CheckCommandBlock(FDnaLineNode& Block)
			{
				TArray<FDnaLineNode*> Lines;
				Block.GetContentChildren(Lines);
				for (FDnaLineNode* Line : Lines)
				{
					if (Line->Kind == EDnaLineKind::Command) { CheckCommand(*Line); }
				}
			}

			void CheckCommand(FDnaLineNode& Line)
			{
				const FDnaLinePayload& P = Line.Payload;
				switch (P.Command)
				{
				case EDnaCommandKind::Set:
				{
					const FDnaFactInfo* Fact = RequireFact(P.Name, Line.Raw.Span);
					if (Fact && P.Expr.IsValid())
					{
						CheckSetValue(*Fact, P.AssignOp, P.Expr, Line.Raw.Span);
					}
					break;
				}
				case EDnaCommandKind::Accept:
				case EDnaCommandKind::Complete:
				case EDnaCommandKind::Fail:
					RequireQuest(P.Name, Line.Raw.Span);
					break;
				case EDnaCommandKind::Advance:
					RequireChapter(P.Name, Line.Raw.Span);
					break;
				case EDnaCommandKind::Play:
					if (RequireStage(P.Name, Line.Raw.Span) && Program.Stages[P.Name].bLineRelative)
					{
						Fail(TEXT("DNS7010"), Line.Raw.Span, FText::Format(LOCTEXT("PlayLineStage",
							"Stage '{0}' follows a line (`base: line`); attach it to one with `@{0}` instead of playing it"), FText::FromString(P.Name)));
					}
					break;
				case EDnaCommandKind::Emit:
					CheckEmit(P.Name, P.EmitArgs, Line.Raw.Span);
					break;
				default:
					break;
				}
			}

			// ---------------------------------------------------------------- references

			const FDnaFactInfo* RequireFact(const FString& Name, const FDnaSpan& Span)
			{
				if (const FDnaFactInfo* Fact = Program.FindFact(Name))
				{
					return Fact;
				}
				Fail(TEXT("DNS3001"), Span,
					FText::Format(LOCTEXT("UnknownFact", "No fact called '{0}' is declared"), FText::FromString(Name)));
				return nullptr;
			}

			void RequireCharacter(const FString& Name, const FDnaSpan& Span, bool bIsSpeaker)
			{
				if (Program.IsCharacter(Name)) { return; }
				if (bIsSpeaker)
				{
					Fail(TEXT("DNS3002"), Span,
						FText::Format(LOCTEXT("UnknownSpeaker", "'{0}' is not a declared character. Declare it with `character {0}`, or start the line with `> ` if it is narration"),
							FText::FromString(Name)));
				}
				else
				{
					Fail(TEXT("DNS3002"), Span,
						FText::Format(LOCTEXT("UnknownCharacter", "'{0}' is not a declared character"), FText::FromString(Name)));
				}
			}

			void RequireDialogue(const FString& Name, const FDnaSpan& Span)
			{
				if (!Program.IsDialogue(Name))
				{
					Fail(TEXT("DNS3003"), Span,
						FText::Format(LOCTEXT("UnknownDialogue", "No dialogue called '{0}' exists"), FText::FromString(Name)));
				}
			}

			void RequireQuest(const FString& Name, const FDnaSpan& Span)
			{
				if (!Program.IsQuest(Name))
				{
					Fail(TEXT("DNS3005"), Span,
						FText::Format(LOCTEXT("UnknownQuest", "No quest called '{0}' exists"), FText::FromString(Name)));
				}
			}

			bool RequireStage(const FString& Name, const FDnaSpan& Span)
			{
				if (!Program.IsStage(Name))
				{
					return Fail(TEXT("DNS3006"), Span,
						FText::Format(LOCTEXT("UnknownStage", "No stage called '{0}' exists"), FText::FromString(Name)));
				}
				return true;
			}

			void RequireChapter(const FString& Name, const FDnaSpan& Span)
			{
				if (!Program.IsChapter(Name))
				{
					Fail(TEXT("DNS3009"), Span,
						FText::Format(LOCTEXT("UnknownChapter", "No chapter called '{0}' exists"), FText::FromString(Name)));
				}
			}

			void RequireChannel(const FString& Name, const FDnaSpan& Span)
			{
				if (!Program.Channels.Contains(Name))
				{
					Fail(TEXT("DNS3010"), Span,
						FText::Format(LOCTEXT("UnknownChannel", "No channel called '{0}' is declared"), FText::FromString(Name)));
				}
			}

			void RequireEvent(const FString& Name, const FDnaSpan& Span)
			{
				if (!Program.FindEvent(Name))
				{
					Fail(TEXT("DNS3011"), Span,
						FText::Format(LOCTEXT("UnknownEvent", "No event called '{0}' is declared"), FText::FromString(Name)));
				}
			}

			/** The fact a name reads, or the parameter of the event in scope it names; null for anything else. */
			const FDnaFactInfo* FindValueInfo(const FDnaExpr& Expr) const
			{
				if (Expr.Kind != EDnaExprKind::Name)
				{
					return nullptr;
				}
				if (const FDnaFactInfo* Fact = Program.FindFact(Expr.Name))
				{
					return Fact;
				}
				if (CurrentEvent && Expr.Path.Num() == 2 && Expr.Path[0] == CurrentEvent->Name)
				{
					return CurrentEvent->FindParam(Expr.Path[1]);
				}
				return nullptr;
			}

			// ---------------------------------------------------------------- types

			/** A condition may be bool or numeric (non-zero is true). */
			void CheckCondition(const FDnaExprPtr& Expr)
			{
				const FDnaTypeInfo Type = CheckExpr(*Expr);
				if (!Type.IsUnknown() && Type.Kind != EKind::Bool && !Type.IsNumeric())
				{
					Fail(TEXT("DNS4001"), Expr->Span,
						FText::Format(LOCTEXT("ConditionType", "A condition must be true/false or a number, not a {0}"), FText::FromString(Type.Describe())));
				}
			}

			/** `fact = expr`: the value must fit the fact's type, set and range. */
			void CheckAssignment(const FDnaFactInfo& Fact, const FDnaExprPtr& Expr, const FDnaSpan& Span)
			{
				if (!Expr.IsValid()) { return; }
				switch (Fact.Type)
				{
				case EDnaFactType::Tag:
				{
					const FDnaTypeInfo Value = CheckTagOperand(*Expr, Fact);
					if (!Value.IsUnknown() && Value.Kind != EKind::Tag)
					{
						Fail(TEXT("DNS4001"), Expr->Span,
							FText::Format(LOCTEXT("AssignTag", "'{0}' holds one of ({1}); a {2} cannot be assigned to it"),
								FText::FromString(Fact.Name), FText::FromString(FString::Join(Fact.Domain, TEXT(", "))), FText::FromString(Value.Describe())));
					}
					break;
				}
				case EDnaFactType::Bool:
				{
					const FDnaTypeInfo Value = CheckExpr(*Expr);
					if (!Value.IsUnknown() && Value.Kind != EKind::Bool)
					{
						Fail(TEXT("DNS4001"), Expr->Span,
							FText::Format(LOCTEXT("AssignBool", "'{0}' is a bool fact; a {1} cannot be assigned to it"),
								FText::FromString(Fact.Name), FText::FromString(Value.Describe())));
					}
					break;
				}
				case EDnaFactType::Int:
				case EDnaFactType::Float:
				{
					const FDnaTypeInfo Value = CheckExpr(*Expr);
					if (!Value.IsUnknown() && !Value.IsNumeric())
					{
						Fail(TEXT("DNS4001"), Expr->Span,
							FText::Format(LOCTEXT("AssignNumber", "'{0}' is a {1} fact; a {2} cannot be assigned to it"),
								FText::FromString(Fact.Name), FText::FromString(::DreamNarrative::LexToString(Fact.Type)), FText::FromString(Value.Describe())));
					}
					else if (Fact.bHasRange && Expr->Kind == EDnaExprKind::Literal)
					{
						const double Number = Expr->Literal.AsNumber();
						if (Number < Fact.RangeMin || Number > Fact.RangeMax)
						{
							Fail(TEXT("DNS4003"), Expr->Span,
								FText::Format(LOCTEXT("OutOfRange", "{0} is outside the declared range {1}..{2} of '{3}'"),
									FText::FromString(Expr->Literal.ToString()), Fact.RangeMin, Fact.RangeMax, FText::FromString(Fact.Name)));
						}
					}
					break;
				}
				}
			}

			/** An operand that may be a bare member of Fact's set. */
			FDnaTypeInfo CheckTagOperand(const FDnaExpr& Expr, const FDnaFactInfo& Fact)
			{
				if (Expr.Kind == EDnaExprKind::Name && Expr.Path.Num() == 1 && !Program.FindFact(Expr.Name) && !Program.IsQuest(Expr.Path[0]))
				{
					if (!Fact.HasMember(Expr.Name))
					{
						Fail(TEXT("DNS4002"), Expr.Span,
							FText::Format(LOCTEXT("NotAMember", "'{0}' is not one of the values of '{1}' ({2})"),
								FText::FromString(Expr.Name), FText::FromString(Fact.Name), FText::FromString(FString::Join(Fact.Domain, TEXT(", ")))));
						return FDnaTypeInfo::Of(EKind::Unknown);
					}
					FDnaTypeInfo Type = FDnaTypeInfo::Of(EKind::Tag);
					Type.TagFact = &Fact;
					return Type;
				}
				return CheckExpr(Expr);
			}

			/** An operand compared with the current chapter must be a chapter name. */
			FDnaTypeInfo CheckChapterOperand(const FDnaExpr& Expr)
			{
				if (Expr.Kind == EDnaExprKind::Name && Expr.Path.Num() == 1 && !Program.FindFact(Expr.Name))
				{
					RequireChapter(Expr.Name, Expr.Span);
					return FDnaTypeInfo::Of(EKind::Chapter);
				}
				return CheckExpr(Expr);
			}

			FDnaTypeInfo CheckExpr(const FDnaExpr& Expr)
			{
				switch (Expr.Kind)
				{
				case EDnaExprKind::Literal:
					switch (Expr.Literal.Type)
					{
					case EDnaFactType::Bool: return FDnaTypeInfo::Of(EKind::Bool);
					case EDnaFactType::Int: return FDnaTypeInfo::Of(EKind::Int);
					case EDnaFactType::Float: return FDnaTypeInfo::Of(EKind::Float);
					default: return FDnaTypeInfo::Of(EKind::Unknown);
					}
				case EDnaExprKind::Chapter:
					return FDnaTypeInfo::Of(EKind::Chapter);
				case EDnaExprKind::Name:
					return CheckName(Expr);
				case EDnaExprKind::Call:
					return CheckCall(Expr);
				case EDnaExprKind::Unary:
				{
					const FDnaTypeInfo Operand = CheckExpr(*Expr.Args[0]);
					if (Expr.Op == EDnaExprOp::Not)
					{
						if (!Operand.IsUnknown() && Operand.Kind != EKind::Bool && !Operand.IsNumeric())
						{
							Fail(TEXT("DNS4001"), Expr.Span,
								FText::Format(LOCTEXT("NotOperand", "'not' needs true/false or a number, not a {0}"), FText::FromString(Operand.Describe())));
						}
						return FDnaTypeInfo::Of(EKind::Bool);
					}
					if (!Operand.IsUnknown() && !Operand.IsNumeric())
					{
						Fail(TEXT("DNS4001"), Expr.Span,
							FText::Format(LOCTEXT("NegOperand", "'-' needs a number, not a {0}"), FText::FromString(Operand.Describe())));
						return FDnaTypeInfo::Of(EKind::Unknown);
					}
					return Operand;
				}
				case EDnaExprKind::Binary:
					return CheckBinary(Expr);
				case EDnaExprKind::In:
				{
					const FDnaTypeInfo Subject = CheckExpr(*Expr.Args[0]);
					if (Subject.Kind == EKind::Tag && Subject.TagFact)
					{
						for (int32 Index = 1; Index < Expr.Args.Num(); ++Index)
						{
							CheckTagOperand(*Expr.Args[Index], *Subject.TagFact);
						}
					}
					else if (Subject.Kind == EKind::Chapter)
					{
						for (int32 Index = 1; Index < Expr.Args.Num(); ++Index)
						{
							CheckChapterOperand(*Expr.Args[Index]);
						}
					}
					else if (!Subject.IsUnknown())
					{
						Fail(TEXT("DNS4001"), Expr.Span,
							FText::Format(LOCTEXT("InSubject", "'in' applies to a tag fact or the chapter, not a {0}"), FText::FromString(Subject.Describe())));
					}
					return FDnaTypeInfo::Of(EKind::Bool);
				}
				}
				return FDnaTypeInfo::Of(EKind::Unknown);
			}

			FDnaTypeInfo CheckName(const FDnaExpr& Expr)
			{
				// event parameter
				if (Expr.Path.Num() == 2)
				{
					if (const FDnaEventInfo* Event = Program.FindEvent(Expr.Path[0]))
					{
						if (CurrentEvent != Event)
						{
							Fail(TEXT("DNS3012"), Expr.Span, FText::Format(LOCTEXT("ParamOutOfScope",
								"'{0}' is a parameter of event '{1}'; only the dialogue that opens on {1} reads it"),
								FText::FromString(Expr.Name), FText::FromString(Expr.Path[0])));
							return FDnaTypeInfo::Of(EKind::Unknown);
						}
						if (const FDnaFactInfo* Param = Event->FindParam(Expr.Path[1]))
						{
							return FDnaTypeInfo::OfFact(*Param);
						}
						Fail(TEXT("DNS3013"), Expr.Span, FText::Format(LOCTEXT("UnknownParam", "Event '{0}' has no parameter '{1}'"),
							FText::FromString(Expr.Path[0]), FText::FromString(Expr.Path[1])));
						return FDnaTypeInfo::Of(EKind::Unknown);
					}
				}
				// quest state
				if (Expr.Path.Num() == 2 && Program.IsQuest(Expr.Path[0]))
				{
					const FString& Property = Expr.Path[1];
					if (Property == TEXT("available") || Property == TEXT("active") || Property == TEXT("completed") || Property == TEXT("failed"))
					{
						return FDnaTypeInfo::Of(EKind::Bool);
					}
					Fail(TEXT("DNS4005"), Expr.Span,
						FText::Format(LOCTEXT("QuestProperty", "'{0}' is not a quest state; use available, active, completed or failed"), FText::FromString(Property)));
					return FDnaTypeInfo::Of(EKind::Unknown);
				}
				if (const FDnaFactInfo* Fact = Program.FindFact(Expr.Name))
				{
					return FDnaTypeInfo::OfFact(*Fact);
				}
				Fail(TEXT("DNS3001"), Expr.Span,
					FText::Format(LOCTEXT("UnknownName", "No fact called '{0}' is declared"), FText::FromString(Expr.Name)));
				return FDnaTypeInfo::Of(EKind::Unknown);
			}

			FDnaTypeInfo CheckCall(const FDnaExpr& Expr)
			{
				if (Expr.Name == TEXT("visited"))
				{
					if (Expr.Args.Num() != 1 || Expr.Args[0]->Kind != EDnaExprKind::Name)
					{
						Fail(TEXT("DNS4005"), Expr.Span, LOCTEXT("VisitedArgs", "'visited' takes exactly one dialogue name"));
					}
					else
					{
						RequireDialogue(Expr.Args[0]->Name, Expr.Args[0]->Span);
					}
					return FDnaTypeInfo::Of(EKind::Int);
				}
				if (Expr.Name == TEXT("random"))
				{
					if (Expr.Args.Num() != 2)
					{
						Fail(TEXT("DNS4005"), Expr.Span, LOCTEXT("RandomArgs", "'random' takes two numbers: random(min, max)"));
						return FDnaTypeInfo::Of(EKind::Int);
					}
					bool bFloat = false;
					for (const FDnaExprPtr& Arg : Expr.Args)
					{
						const FDnaTypeInfo Type = CheckExpr(*Arg);
						if (!Type.IsUnknown() && !Type.IsNumeric())
						{
							Fail(TEXT("DNS4005"), Arg->Span, LOCTEXT("RandomArgType", "'random' arguments must be numbers"));
						}
						bFloat |= (Type.Kind == EKind::Float);
					}
					return FDnaTypeInfo::Of(bFloat ? EKind::Float : EKind::Int);
				}
				if (Expr.Name == TEXT("chance"))
				{
					if (Expr.Args.Num() != 1)
					{
						Fail(TEXT("DNS4005"), Expr.Span, LOCTEXT("ChanceArgs", "'chance' takes one probability: chance(30%)"));
					}
					else
					{
						const FDnaTypeInfo Type = CheckExpr(*Expr.Args[0]);
						if (!Type.IsUnknown() && !Type.IsNumeric())
						{
							Fail(TEXT("DNS4005"), Expr.Args[0]->Span, LOCTEXT("ChanceArgType", "'chance' needs a number or a percentage"));
						}
					}
					return FDnaTypeInfo::Of(EKind::Bool);
				}
				Fail(TEXT("DNS4005"), Expr.Span,
					FText::Format(LOCTEXT("UnknownFunction", "'{0}' is not a built-in; the built-ins are visited, random and chance"), FText::FromString(Expr.Name)));
				for (const FDnaExprPtr& Arg : Expr.Args) { CheckExpr(*Arg); }
				return FDnaTypeInfo::Of(EKind::Unknown);
			}

			FDnaTypeInfo CheckBinary(const FDnaExpr& Expr)
			{
				const FDnaExpr& Left = *Expr.Args[0];
				const FDnaExpr& Right = *Expr.Args[1];

				switch (Expr.Op)
				{
				case EDnaExprOp::And:
				case EDnaExprOp::Or:
				{
					for (const FDnaExpr* Side : { &Left, &Right })
					{
						const FDnaTypeInfo Type = CheckExpr(*Side);
						if (!Type.IsUnknown() && Type.Kind != EKind::Bool && !Type.IsNumeric())
						{
							Fail(TEXT("DNS4001"), Side->Span,
								FText::Format(LOCTEXT("LogicOperand", "'{0}' needs true/false or numbers, not a {1}"),
									FText::FromString(LexToString(Expr.Op)), FText::FromString(Type.Describe())));
						}
					}
					return FDnaTypeInfo::Of(EKind::Bool);
				}
				case EDnaExprOp::Eq:
				case EDnaExprOp::Ne:
				{
					// Tag and chapter comparisons resolve the other side as a member name.
					if (IsTagFactRef(Left)) { CheckTagOperand(Right, *FindValueInfo(Left)); CheckExpr(Left); return FDnaTypeInfo::Of(EKind::Bool); }
					if (IsTagFactRef(Right)) { CheckTagOperand(Left, *FindValueInfo(Right)); CheckExpr(Right); return FDnaTypeInfo::Of(EKind::Bool); }
					if (Left.Kind == EDnaExprKind::Chapter) { CheckChapterOperand(Right); return FDnaTypeInfo::Of(EKind::Bool); }
					if (Right.Kind == EDnaExprKind::Chapter) { CheckChapterOperand(Left); return FDnaTypeInfo::Of(EKind::Bool); }

					const FDnaTypeInfo L = CheckExpr(Left);
					const FDnaTypeInfo R = CheckExpr(Right);
					if (!L.IsUnknown() && !R.IsUnknown())
					{
						const bool bCompatible = (L.IsNumeric() && R.IsNumeric()) || L.Kind == R.Kind;
						if (!bCompatible)
						{
							Fail(TEXT("DNS4001"), Expr.Span,
								FText::Format(LOCTEXT("EqMismatch", "Cannot compare a {0} with a {1}"),
									FText::FromString(L.Describe()), FText::FromString(R.Describe())));
						}
					}
					return FDnaTypeInfo::Of(EKind::Bool);
				}
				case EDnaExprOp::Lt:
				case EDnaExprOp::Le:
				case EDnaExprOp::Gt:
				case EDnaExprOp::Ge:
				{
					const FDnaTypeInfo L = CheckExpr(Left);
					const FDnaTypeInfo R = CheckExpr(Right);
					for (const FDnaTypeInfo* Side : { &L, &R })
					{
						if (!Side->IsUnknown() && !Side->IsNumeric())
						{
							Fail(TEXT("DNS4004"), Expr.Span,
								FText::Format(LOCTEXT("OrderMismatch", "'{0}' orders numbers; a {1} has no order"),
									FText::FromString(LexToString(Expr.Op)), FText::FromString(Side->Describe())));
							break;
						}
					}
					return FDnaTypeInfo::Of(EKind::Bool);
				}
				case EDnaExprOp::Add:
				case EDnaExprOp::Sub:
				case EDnaExprOp::Mul:
				case EDnaExprOp::Div:
				case EDnaExprOp::Mod:
				{
					const FDnaTypeInfo L = CheckExpr(Left);
					const FDnaTypeInfo R = CheckExpr(Right);
					bool bFloat = false;
					for (const FDnaTypeInfo* Side : { &L, &R })
					{
						if (Side->IsUnknown()) { continue; }
						if (!Side->IsNumeric())
						{
							Fail(TEXT("DNS4001"), Expr.Span,
								FText::Format(LOCTEXT("ArithMismatch", "'{0}' needs numbers, not a {1}"),
									FText::FromString(LexToString(Expr.Op)), FText::FromString(Side->Describe())));
							return FDnaTypeInfo::Of(EKind::Unknown);
						}
						bFloat |= (Side->Kind == EKind::Float);
					}
					if (Expr.Op == EDnaExprOp::Mod && bFloat)
					{
						Fail(TEXT("DNS4001"), Expr.Span, LOCTEXT("ModFloat", "'%' needs whole numbers on both sides"));
					}
					return FDnaTypeInfo::Of(bFloat ? EKind::Float : EKind::Int);
				}
				default:
					return FDnaTypeInfo::Of(EKind::Unknown);
				}
			}

			bool IsTagFactRef(const FDnaExpr& Expr) const
			{
				const FDnaFactInfo* Fact = FindValueInfo(Expr);
				return Fact && Fact->Type == EDnaFactType::Tag;
			}
		};
	}

	bool ValidateDnaProgram(const FDnaProgram& Program, FDnaDiagnosticSink& Diagnostics)
	{
		FValidator Validator(Program, Diagnostics);
		return Validator.Run();
	}

	bool ValidateDnaExpression(const FDnaProgram& Program, const FDnaExprPtr& Expr, bool bAsCondition, FDnaDiagnosticSink& Diagnostics)
	{
		FValidator Validator(Program, Diagnostics);
		return Validator.RunOnExpression(Expr, bAsCondition);
	}
}

#undef LOCTEXT_NAMESPACE
