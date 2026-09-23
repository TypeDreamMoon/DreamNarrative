// Copyright DreamMoon. All Rights Reserved.

#include "Spec/DnaTestBlocks.h"
#include "DreamNarrativeDialogue.h"
#include "DreamNarrativeDialogueRunner.h"
#include "DreamNarrativeQuest.h"
#include "DreamNarrativeLedger.h"
#include "DreamNarrativeStory.h"
#include "Fixtures/DnaTestWorld.h"
#include "Lang/DnaSyntax.h"

namespace UE::DreamNarrative::Tests
{
	using namespace UE::DreamNarrative::Lang;

	namespace
	{
		FString DescribeShown(const FDnaLineView& Line)
		{
			return Line.bNarration
				? FString::Printf(TEXT("> %s"), *Line.Text.ToString())
				: FString::Printf(TEXT("%s: %s"), *Line.Speaker.ToString(), *Line.Text.ToString());
		}

		FString DescribeOffered(const UDreamNarrativeDialogueRunner& Runner)
		{
			TArray<FString> Labels;
			for (const FDnaChoiceView& Choice : Runner.GetChoices())
			{
				Labels.Add(FString::Printf(TEXT("\"%s\" (#%s)"), *Choice.Label.ToString(), *Choice.Id.ToString()));
			}
			return Labels.Num() > 0 ? FString::Join(Labels, TEXT(", ")) : TEXT("(none)");
		}

		class FBlockRunner
		{
		public:
			FBlockRunner(FDnaTestWorld& InWorld, const FString& InFile, TFunction<void(const FDnaLineNode&)> InBeforeStep)
				: World(InWorld), File(InFile), BeforeStep(MoveTemp(InBeforeStep))
			{
			}

			bool Run(const FDnaLineNode& TestNode)
			{
				TArray<FDnaLineNode*> Steps;
				TestNode.GetContentChildren(Steps);
				bool bOk = true;
				for (const FDnaLineNode* Step : Steps)
				{
					if (Step->Kind == EDnaLineKind::TestStep)
					{
						World.SetContext(Where(*Step));
						if (BeforeStep)
						{
							BeforeStep(*Step);
						}
						bOk &= RunStep(*Step);
					}
				}
				World.SetContext(FString());
				return bOk;
			}

		private:
			FDnaTestWorld& World;
			FString File;
			TFunction<void(const FDnaLineNode&)> BeforeStep;

			FString Where(const FDnaLineNode& Node) const
			{
				return FString::Printf(TEXT("%s(%d)"), *File, Node.LineNumber());
			}

			FString QuestStateText(FName Quest) const
			{
				return ::DreamNarrative::QuestStateMember(World.Story().GetQuestStateOf(Quest)).ToString();
			}

			/** `quest 任务 = 状态 [剩余时长]` under given: the state as it stands, its objectives undone, its timer as given. */
			bool GiveQuest(const FDnaLinePayload& E)
			{
				const FName Quest(*E.Name);
				const FDnaQuestData* Data = World.Story().FindQuest(Quest);
				EDnaQuestState State = EDnaQuestState::Inactive;
				if (!Data || !::DreamNarrative::TryParseQuestState(FName(*E.Value), State))
				{
					World.ReportError(FString::Printf(TEXT("given: quest %s = %s cannot be set"), *E.Name, *E.Value));
					return false;
				}
				UDreamNarrativeLedger& Ledger = World.Ledger();
				Ledger.SetFact(::DreamNarrative::QuestObjectivesFact(Quest), FDnaValue::FromInt(0), EDnaWriteSource::Narrative);
				Ledger.SetFact(::DreamNarrative::QuestStateFact(Quest), FDnaValue::FromTag(::DreamNarrative::QuestStateMember(State)), EDnaWriteSource::Narrative);
				const bool bActive = State == EDnaQuestState::Accepted || State == EDnaQuestState::Going;
				if (bActive && (E.Seconds > 0.0 || Data->Timeout > 0.f))
				{
					Ledger.StartTimer(::DreamNarrative::QuestStateFact(Quest), E.Seconds > 0.0 ? E.Seconds : Data->Timeout);
				}
				else
				{
					Ledger.CancelTimer(::DreamNarrative::QuestStateFact(Quest));
				}
				return true;
			}

			bool IsOnceChoice(FName Id) const
			{
				for (const TObjectPtr<UDreamNarrativeScript>& Script : World.GetScripts())
				{
					const UDnaDialogueSection* Dialogue = Script ? Script->FindSection<UDnaDialogueSection>() : nullptr;
					for (const FDnaDialogueNodeData& Node : Dialogue ? Dialogue->Nodes : TArray<FDnaDialogueNodeData>())
					{
						for (const FDnaChoiceGroupData& Group : Node.ChoiceGroups)
						{
							if (Group.Options.ContainsByPredicate([Id](const FDnaChoiceData& Option) { return Option.bOnce && Option.Id == Id; }))
							{
								return true;
							}
						}
					}
				}
				return false;
			}

			bool RunStep(const FDnaLineNode& Step)
			{
				const FDnaLinePayload& P = Step.Payload;
				switch (P.TestStep)
				{
				case EDnaTestStepKind::Seed:
					World.Ledger().SetRandomSeed(P.Seed);
					return true;

				case EDnaTestStepKind::Given:
				{
					// Facts first, then visits and once-choices, the chapter last: setting a chapter runs no
					// advance rule, so a test that starts with a given starts exactly there.
					bool bOk = true;
					TArray<FDnaLineNode*> Entries;
					Step.GetContentChildren(Entries);
					const FDnaLineNode* Chapter = nullptr;
					for (const FDnaLineNode* Entry : Entries)
					{
						World.SetContext(Where(*Entry));
						const FDnaLinePayload& E = Entry->Payload;
						if (Entry->Kind == EDnaLineKind::Implication)
						{
							FDnaValue Value;
							bOk &= World.EvaluateValueFor(E.Expr, E.Name, Value) && World.Given(FName(*E.Name), Value);
						}
						else if (Entry->Kind != EDnaLineKind::TestStep)
						{
							continue;
						}
						else if (E.TestStep == EDnaTestStepKind::GivenVisited)
						{
							World.Ledger().SetVisited(FName(*E.Target), E.Count);
						}
						else if (E.TestStep == EDnaTestStepKind::GivenOnce)
						{
							if (IsOnceChoice(FName(*E.Target)))
							{
								World.Ledger().MarkChosenOnce(FName(*E.Target));
							}
							else
							{
								World.ReportError(FString::Printf(TEXT("given: no dialogue has a once-choice #%s"), *E.Target));
								bOk = false;
							}
						}
						else if (E.TestStep == EDnaTestStepKind::GivenChapter)
						{
							Chapter = Entry;
						}
						else if (E.TestStep == EDnaTestStepKind::GivenQuest)
						{
							bOk &= GiveQuest(E);
						}
					}
					if (Chapter)
					{
						World.SetContext(Where(*Chapter));
						bOk &= World.Ledger().SetChapter(FName(*Chapter->Payload.Target));
					}
					World.SetContext(Where(Step));
					return World.FinishStep(TEXT("given"), bOk);
				}

				case EDnaTestStepKind::Talk:
				case EDnaTestStepKind::Start:
					return RunDialogue(Step);

				case EDnaTestStepKind::Expect:
				{
					FDnaValue Value;
					if (!World.EvaluateAst(P.Condition, Value))
					{
						return false;
					}
					if (!Value.AsBool())
					{
						const FString Written = Step.Raw.Content.Mid(6).TrimStartAndEnd();
						World.ReportError(FString::Printf(TEXT("expect `%s` is false. %s"), *Written, *World.DescribeFactsOf(*P.Condition)));
						return false;
					}
					return true;
				}

				case EDnaTestStepKind::Set:
				{
					FDnaValue Value;
					if (!World.EvaluateValueFor(P.Expr, P.Name, Value))
					{
						return false;
					}
					const FName Fact(*P.Name);
					if (P.AssignOp != EDnaTokenKind::Assign)
					{
						const FDnaValue Current = World.Ledger().GetFact(Fact);
						const double Delta = P.AssignOp == EDnaTokenKind::PlusAssign ? Value.AsNumber() : -Value.AsNumber();
						Value = Current.Type == EDnaFactType::Float
							? FDnaValue::FromFloat(static_cast<float>(Current.AsNumber() + Delta))
							: FDnaValue::FromInt(static_cast<int32>(Current.AsNumber() + Delta));
					}
					return World.SetExtern(Fact, Value);
				}

				case EDnaTestStepKind::Wait:
					World.Wait(P.Seconds);
					return true;

				case EDnaTestStepKind::Jump:
					return World.Jump(FName(*P.Target));

				case EDnaTestStepKind::Save:
					return World.CheckSaveRoundTrip(TEXT("save"));

				case EDnaTestStepKind::Rolls:
					World.ForceRolls(P.Rolls);
					return true;

				case EDnaTestStepKind::QuestAccept:
				case EDnaTestStepKind::QuestComplete:
				case EDnaTestStepKind::QuestFail:
				{
					const EDnaQuestRequest What = P.TestStep == EDnaTestStepKind::QuestAccept ? EDnaQuestRequest::Accept
						: P.TestStep == EDnaTestStepKind::QuestComplete ? EDnaQuestRequest::Complete : EDnaQuestRequest::Fail;
					const FString Label = FString::Printf(TEXT("%s %s"), LexToString(P.TestStep), *P.Name);
					const bool bOk = World.Story().RequestQuest(What, FName(*P.Name), EDnaWriteSource::Game);
					if (!bOk)
					{
						World.ReportError(FString::Printf(TEXT("%s: the quest refused; it is %s"), *Label, *QuestStateText(FName(*P.Name))));
					}
					return World.FinishStep(Label, bOk);
				}

				case EDnaTestStepKind::Journal:
				{
					FString Expected;
					if (!ExpectedText(Step, Expected))
					{
						return false;
					}
					const FString Shown = World.Story().GetQuestJournal(FName(*P.Name)).ToString();
					if (Shown != Expected)
					{
						World.ReportError(FString::Printf(TEXT("journal %s reads \"%s\", not \"%s\" (the quest is %s)"), *P.Name, *Shown, *Expected, *QuestStateText(FName(*P.Name))));
						return false;
					}
					return true;
				}

				default:
					return true;
				}
			}

			// ---------------------------------------------------------------- dialogue

			/** The text an expectation stands for, with its interpolations evaluated now. */
			bool ExpectedText(const FDnaLineNode& Node, FString& Out)
			{
				Out.Reset();
				auto Append = [this, &Out](const FDnaText& Text)
				{
					for (const FDnaTextPart& Part : Text.Parts)
					{
						if (!Part.IsExpr())
						{
							Out += Part.Literal;
							continue;
						}
						FDnaValue Value;
						if (!World.EvaluateAst(Part.Expr, Value))
						{
							return false;
						}
						// Formatted the way the runner formats a line's arguments.
						switch (Value.Type)
						{
						case EDnaFactType::Int: Out += FText::AsNumber(Value.Int).ToString(); break;
						case EDnaFactType::Float: Out += FText::AsNumber(Value.Float).ToString(); break;
						default: Out += Value.ToString(); break;
						}
					}
					return true;
				};
				if (!Append(Node.Payload.Text))
				{
					return false;
				}
				for (const FDnaLineNodePtr& Child : Node.Children)
				{
					if (Child.IsValid() && Child->Kind == EDnaLineKind::Continuation)
					{
						Out += TEXT("\n");
						if (!Append(Child->Payload.Text))
						{
							return false;
						}
					}
				}
				return true;
			}

			FString DescribeExpected(const FDnaLineNode& Node)
			{
				const FDnaLinePayload& P = Node.Payload;
				if (!P.Target.IsEmpty())
				{
					return TEXT("#") + P.Target;
				}
				FString Text;
				if (!ExpectedText(Node, Text))
				{
					Text = P.Text.Raw;
				}
				if (P.TestStep == EDnaTestStepKind::Pick)
				{
					return Text;
				}
				return P.Name.IsEmpty() ? TEXT("> ") + Text : P.Name + TEXT(": ") + Text;
			}

			bool Matches(const FDnaLineNode& Node, const FDnaLineView& Line)
			{
				const FDnaLinePayload& P = Node.Payload;
				if (!P.Target.IsEmpty())
				{
					return Line.Id == FName(*P.Target);
				}
				if (P.Name.IsEmpty() ? !Line.bNarration : Line.Speaker != FName(*P.Name))
				{
					return false;
				}
				if (P.SpeakerTags.Num() > 0)
				{
					if (P.SpeakerTags.Num() != Line.Tags.Num())
					{
						return false;
					}
					for (int32 Index = 0; Index < P.SpeakerTags.Num(); ++Index)
					{
						if (Line.Tags[Index] != FName(*P.SpeakerTags[Index]))
						{
							return false;
						}
					}
				}
				FString Expected;
				return ExpectedText(Node, Expected) && Expected == Line.Text.ToString();
			}

			bool MatchesChoice(const FDnaLineNode& Node, const FDnaChoiceView& Choice)
			{
				if (!Node.Payload.Target.IsEmpty())
				{
					return Choice.Id == FName(*Node.Payload.Target);
				}
				FString Expected;
				return ExpectedText(Node, Expected) && Expected == Choice.Label.ToString();
			}

			bool RunDialogue(const FDnaLineNode& Step)
			{
				const FDnaLinePayload& P = Step.Payload;
				const bool bTalk = P.TestStep == EDnaTestStepKind::Talk;
				const FString What = FString::Printf(TEXT("%s %s"), bTalk ? TEXT("talk") : TEXT("start"), *P.Name);
				UDreamNarrativeDialogueRunner* Runner = bTalk ? World.Story().TalkTo(FName(*P.Name)) : World.Story().StartDialogue(FName(*P.Name));
				if (!Runner)
				{
					World.ReportError(bTalk
						? FString::Printf(TEXT("%s: no entry of %s applies right now"), *What, *P.Name)
						: FString::Printf(TEXT("%s: there is no dialogue called %s"), *What, *P.Name));
					return World.FinishStep(What, false);
				}

				// In source order: the lines to see and the choices to take. Absent lines apply throughout.
				TArray<const FDnaLineNode*> Items;
				TArray<const FDnaLineNode*> Absents;
				TArray<FDnaLineNode*> Children;
				Step.GetContentChildren(Children);
				for (const FDnaLineNode* Child : Children)
				{
					if (Child->Kind != EDnaLineKind::TestStep) { continue; }
					(Child->Payload.TestStep == EDnaTestStepKind::Absent ? Absents : Items).Add(Child);
				}

				bool bOk = true;
				int32 Cursor = 0;
				auto IsExpectedLine = [&Items](int32 Index) { return Items.IsValidIndex(Index) && Items[Index]->Payload.TestStep == EDnaTestStepKind::ExpectLine; };
				for (int32 Guard = 0; Guard < 10000 && Runner->GetState() != EDnaRunnerState::Ended; ++Guard)
				{
					switch (Runner->GetState())
					{
					case EDnaRunnerState::ShowingLine:
					{
						const FDnaLineView& Line = Runner->GetCurrentLine();
						for (const FDnaLineNode* Absent : Absents)
						{
							if (!Absent->Payload.bChoice && Matches(*Absent, Line))
							{
								World.SetContext(Where(*Absent));
								World.ReportError(FString::Printf(TEXT("%s showed \"%s\", which the test says must not appear"), *What, *DescribeShown(Line)));
								bOk = false;
							}
						}
						if (IsExpectedLine(Cursor) && Matches(*Items[Cursor], Line))
						{
							++Cursor;
						}
						else if (P.bStrict)
						{
							World.SetContext(IsExpectedLine(Cursor) ? Where(*Items[Cursor]) : Where(Step));
							World.ReportError(IsExpectedLine(Cursor)
								? FString::Printf(TEXT("%s: expected \"%s\", but the dialogue showed \"%s\""), *What, *DescribeExpected(*Items[Cursor]), *DescribeShown(Line))
								: FString::Printf(TEXT("%s showed \"%s\", which this strict talk does not list"), *What, *DescribeShown(Line)));
							bOk = false;
							if (IsExpectedLine(Cursor))
							{
								++Cursor;
							}
						}
						Runner->Advance();
						break;
					}
					case EDnaRunnerState::AwaitingChoice:
					{
						for (const FDnaLineNode* Absent : Absents)
						{
							if (!Absent->Payload.bChoice) { continue; }
							for (const FDnaChoiceView& Choice : Runner->GetChoices())
							{
								if (MatchesChoice(*Absent, Choice))
								{
									World.SetContext(Where(*Absent));
									World.ReportError(FString::Printf(TEXT("%s offered \"%s\", which the test says must not be offered"), *What, *Choice.Label.ToString()));
									bOk = false;
								}
							}
						}
						// Whatever the test expected before this choice had to be shown by now.
						for (; IsExpectedLine(Cursor); ++Cursor)
						{
							World.SetContext(Where(*Items[Cursor]));
							World.ReportError(FString::Printf(TEXT("%s: expected \"%s\" before the choice, but it was not shown"), *What, *DescribeExpected(*Items[Cursor])));
							bOk = false;
						}
						if (!Items.IsValidIndex(Cursor))
						{
							World.SetContext(Where(Step));
							World.ReportError(FString::Printf(TEXT("%s: choices are on offer but the test takes none here. On offer: %s"), *What, *DescribeOffered(*Runner)));
							Runner->Abort();
							bOk = false;
							break;
						}
						const FDnaLineNode& PickNode = *Items[Cursor++];
						World.SetContext(Where(PickNode));
						int32 PickIndex = INDEX_NONE;
						for (const FDnaChoiceView& Choice : Runner->GetChoices())
						{
							if (MatchesChoice(PickNode, Choice))
							{
								PickIndex = Choice.Index;
								break;
							}
						}
						if (PickIndex == INDEX_NONE)
						{
							World.ReportError(FString::Printf(TEXT("%s: \"%s\" is not on offer. On offer: %s"), *What,
								PickNode.Payload.Target.IsEmpty() ? *PickNode.Payload.Text.Raw : *(TEXT("#") + PickNode.Payload.Target), *DescribeOffered(*Runner)));
							Runner->Abort();
							bOk = false;
							break;
						}
						Runner->Choose(PickIndex);
						break;
					}
					case EDnaRunnerState::Waiting:
						if (!World.Clock().AdvanceToNext())
						{
							World.ReportError(FString::Printf(TEXT("%s is waiting but nothing is on the clock"), *What));
							Runner->Abort();
							bOk = false;
						}
						break;
					case EDnaRunnerState::PlayingStage:
						Runner->NotifyStageFinished();
						break;
					default:
						World.ReportError(FString::Printf(TEXT("%s stopped in an unexpected state"), *What));
						Runner->Abort();
						bOk = false;
						break;
					}
				}
				if (Runner->GetState() != EDnaRunnerState::Ended)
				{
					World.SetContext(Where(Step));
					World.ReportError(FString::Printf(TEXT("%s did not end"), *What));
					Runner->Abort();
					bOk = false;
				}
				for (; Cursor < Items.Num(); ++Cursor)
				{
					World.SetContext(Where(*Items[Cursor]));
					World.ReportError(IsExpectedLine(Cursor)
						? FString::Printf(TEXT("%s: expected \"%s\", but it was not shown"), *What, *DescribeExpected(*Items[Cursor]))
						: FString::Printf(TEXT("%s ended before the choice \"%s\" was offered"), *What, *DescribeExpected(*Items[Cursor])));
					bOk = false;
				}
				World.RecordDialogue(*Runner);
				World.SetContext(Where(Step));
				return World.FinishStep(What, bOk);
			}
		};
	}

	bool RunDnaTestBlock(FDnaTestWorld& World, const FDnaLineNode& TestNode, const FString& FileLabel, TFunction<void(const FDnaLineNode&)> BeforeStep)
	{
		FBlockRunner Runner(World, FileLabel, MoveTemp(BeforeStep));
		return Runner.Run(TestNode);
	}
}
