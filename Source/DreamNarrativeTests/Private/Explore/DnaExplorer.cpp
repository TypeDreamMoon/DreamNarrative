// Copyright DreamMoon. All Rights Reserved.

#include "Explore/DnaExplorer.h"
#include "Algo/Unique.h"
#include "DreamNarrativeDialogue.h"
#include "DreamNarrativeDialogueRunner.h"
#include "DreamNarrativeEvent.h"
#include "DreamNarrativeExpression.h"
#include "DreamNarrativeLedger.h"
#include "DreamNarrativeQuest.h"
#include "DreamNarrativeScript.h"
#include "DreamNarrativeStory.h"
#include "DreamNarrativeTrace.h"
#include "Fixtures/DnaLogCapture.h"
#include "Fixtures/DnaTestWorld.h"
#include "HAL/PlatformTime.h"
#include "Misc/Crc.h"
#include "UObject/UObjectGlobals.h"

namespace UE::DreamNarrative::Tests::Explore
{
	namespace
	{
		/** Ways through one move before the explorer stops trying more (the state then counts as not fully explored). */
		constexpr int32 MaxRunsPerMove = 4096;
		/** Lines, choices and waits in one dialogue before it counts as one that does not stop. */
		constexpr int32 MaxYieldsPerRun = 2000;
		/** The save round trip is checked in the first states, then in every SaveSampleEvery-th. */
		constexpr int32 SaveCheckFirst = 64;
		constexpr int32 SaveSampleEvery = 16;
		/** Unreachable items listed per kind before the rest are only counted. */
		constexpr int32 MaxListed = 12;
		/** Ways of raising one event mixed from its parameters' key values; past this each parameter is varied alone. */
		constexpr int32 MaxEventMixes = 64;
		/** Every dialogue a run starts is a new object: the ones earlier runs left behind are let go every so many runs. */
		constexpr int32 CollectEvery = 65536;

		// ---------------------------------------------------------------- small pieces

		int32 FloatBits(float Value)
		{
			int32 Bits = 0;
			FMemory::Memcpy(&Bits, &Value, sizeof(Bits));
			return Bits;
		}

		float BitsFloat(int32 Bits)
		{
			float Value = 0.f;
			FMemory::Memcpy(&Value, &Bits, sizeof(Value));
			return Value;
		}

		/** A run of ints as a map key. */
		struct FIntKey
		{
			TArray<int32> Values;

			bool operator==(const FIntKey& Other) const { return Values == Other.Values; }
			friend uint32 GetTypeHash(const FIntKey& Key) { return FCrc::MemCrc32(Key.Values.GetData(), Key.Values.Num() * sizeof(int32)); }
		};

		bool IsComparison(EDnaExprOperator Op)
		{
			return Op == EDnaExprOperator::Eq || Op == EDnaExprOperator::Ne || Op == EDnaExprOperator::Lt
				|| Op == EDnaExprOperator::Le || Op == EDnaExprOperator::Gt || Op == EDnaExprOperator::Ge;
		}

		/** `c < x` is `x > c`. */
		EDnaExprOperator Mirror(EDnaExprOperator Op)
		{
			switch (Op)
			{
			case EDnaExprOperator::Lt: return EDnaExprOperator::Gt;
			case EDnaExprOperator::Le: return EDnaExprOperator::Ge;
			case EDnaExprOperator::Gt: return EDnaExprOperator::Lt;
			case EDnaExprOperator::Ge: return EDnaExprOperator::Le;
			default: return Op;
			}
		}

		/** Copies the subtree at Index into Sub, returning where it landed. */
		int32 CopySubtree(const FDnaCompiledExpr& Expr, int32 Index, FDnaCompiledExpr& Sub)
		{
			if (!Expr.Nodes.IsValidIndex(Index))
			{
				return INDEX_NONE;
			}
			const int32 Copied = Sub.Nodes.Add(Expr.Nodes[Index]);
			TArray<int32> Children;
			for (const int32 Child : Expr.Nodes[Index].Children)
			{
				Children.Add(CopySubtree(Expr, Child, Sub));
			}
			Sub.Nodes[Copied].Children = MoveTemp(Children);
			return Copied;
		}

		/** The value of a subtree when it reads nothing from the world. Only the subtree is copied: a constant
		 *  is decided over the whole node table, and the rest of the expression reads the world. */
		bool ConstantOf(const FDnaCompiledExpr& Expr, int32 Index, FDnaValue& Out)
		{
			FDnaCompiledExpr Sub;
			Sub.Root = CopySubtree(Expr, Index, Sub);
			return ::DreamNarrative::EvaluateConstant(Sub, Out);
		}

		bool ReadsFact(const FDnaCompiledExpr& Expr, FName Fact)
		{
			return Expr.Nodes.ContainsByPredicate([Fact](const FDnaExprNode& Node) { return Node.Kind == EDnaExprNodeKind::Fact && Node.Name == Fact; });
		}

		// ---------------------------------------------------------------- decisions

		/** A place where a run branched: a choice among Count, or a draw (low or high). */
		struct FPoint
		{
			uint8 Count = 0;
			uint8 Taken = 0;
			bool bDraw = false;
		};

		/** Hands out the decisions of one run: the forced ones first, then the first alternative of every new point. */
		struct FDecisions
		{
			TArray<uint8> Forced;
			TArray<FPoint> Points;
			bool bDiverged = false;

			uint8 Take(int32 Count, bool bDraw)
			{
				const uint8 Clamped = static_cast<uint8>(FMath::Clamp(Count, 1, 255));
				uint8 Taken = 0;
				if (Points.Num() < Forced.Num())
				{
					Taken = Forced[Points.Num()];
					if (Taken >= Clamped)
					{
						bDiverged = true;
						Taken = 0;
					}
				}
				Points.Add({ Clamped, Taken, bDraw });
				return Taken;
			}

			TArray<uint8> TakenSoFar() const
			{
				TArray<uint8> Out;
				Out.Reserve(Points.Num());
				for (const FPoint& Point : Points) { Out.Add(Point.Taken); }
				return Out;
			}
		};

		/** Every draw is a decision: low or high. Outside a run it is always low. */
		class FDrawSource : public IDnaRandomSource
		{
		public:
			FDecisions* Decisions = nullptr;

			virtual float NextUnit(FRandomStream& Stream) override
			{
				if (!Decisions)
				{
					return ::DreamNarrative::LowestDraw;
				}
				return Decisions->Take(2, /*bDraw=*/true) == 0 ? ::DreamNarrative::LowestDraw : ::DreamNarrative::HighestDraw;
			}
		};

		/** Hears a dialogue running away, and, while describing a path, what each draw came out as. */
		class FExploreTrace : public IDnaTrace
		{
		public:
			bool bRanAway = false;
			TArray<FString>* DrawNotes = nullptr;

			virtual void OnExpression(const FDnaExprNode& Node, const FDnaValue* Left, const FDnaValue* Right, const FDnaValue& Result) override
			{
				if (!DrawNotes)
				{
					return;
				}
				if (Node.Kind == EDnaExprNodeKind::Chance && Left)
				{
					// Six significant digits: the probability is a float, and 30% should not read as 30.000001%.
					DrawNotes->Add(FString::Printf(TEXT("chance(%g%%) %s"), Left->AsNumber() * 100.0, Result.AsBool() ? TEXT("成立") : TEXT("不成立")));
				}
				else if (Node.Kind == EDnaExprNodeKind::Random && Left && Right)
				{
					DrawNotes->Add(FString::Printf(TEXT("random(%s, %s) 得 %s"), *Left->ToString(), *Right->ToString(), *Result.ToString()));
				}
			}

			virtual void OnEvent(const TCHAR* Event) override
			{
				if (FCString::Strcmp(Event, TEXT("dialogue.runaway")) == 0)
				{
					bRanAway = true;
				}
			}
		};

		// ---------------------------------------------------------------- how the story uses a number

		struct FUsage
		{
			/** Some condition or value reads it (text that only shows it does not count). */
			bool bRead = false;
			/** Some read needs the exact value: arithmetic, a comparison with another fact. */
			bool bExact = false;
			/** Written with += / -=: it can grow, or shrink, without bound. */
			bool bUp = false;
			bool bDown = false;
			/** `x op c`, with x on the left. */
			TArray<TPair<EDnaExprOperator, double>> Compares;
			/** `x % k` for a constant k. */
			TArray<int32> Moduli;
		};

		/** How a slot folds into the state key. */
		struct FFold
		{
			/** Nothing reads it: every value is the same state. */
			bool bIgnore = false;
			bool bExact = true;
			bool bHasUpper = false;
			bool bHasLower = false;
			double Upper = 0.0;
			double Lower = 0.0;
		};

		FFold MakeFold(const FUsage* Usage, bool bNumeric)
		{
			FFold Fold;
			if (!Usage || !Usage->bRead)
			{
				Fold.bIgnore = true;
				return Fold;
			}
			// Exact reads, or a number that can move both ways, or one that only takes the values it is
			// given (finitely many): kept as it is.
			if (!bNumeric || Usage->bExact || Usage->Compares.Num() == 0 || Usage->bUp == Usage->bDown)
			{
				return Fold;
			}
			// Past the farthest boundary any comparison draws, every value answers every comparison the
			// same way; moving one way only, it stays past it.
			double Upper = -DBL_MAX;
			double Lower = DBL_MAX;
			for (const TPair<EDnaExprOperator, double>& Compare : Usage->Compares)
			{
				const double C = Compare.Value;
				switch (Compare.Key)
				{
				case EDnaExprOperator::Ge:
				case EDnaExprOperator::Lt:
					Upper = FMath::Max(Upper, C);
					Lower = FMath::Min(Lower, C - 1.0);
					break;
				case EDnaExprOperator::Gt:
				case EDnaExprOperator::Le:
					Upper = FMath::Max(Upper, C + 1.0);
					Lower = FMath::Min(Lower, C);
					break;
				default:
					Upper = FMath::Max(Upper, C + 1.0);
					Lower = FMath::Min(Lower, C - 1.0);
					break;
				}
			}
			Fold.bExact = false;
			if (Usage->bUp)
			{
				Fold.bHasUpper = true;
				Fold.Upper = Upper;
			}
			else
			{
				Fold.bHasLower = true;
				Fold.Lower = Lower;
			}
			return Fold;
		}

		// ---------------------------------------------------------------- moves and runs

		enum class EMoveKind : uint8
		{
			Talk,
			Start,
			Set,
			/** Time runs until the next timer (a quest's timeout) goes off. */
			Wait,
			/** The game raises an event some dialogue opens on. */
			Emit,
			/** A new playthrough: back to where the exploration started, the profile kept. */
			NewGame,
		};

		struct FMove
		{
			EMoveKind Kind = EMoveKind::Talk;
			FName Target;
			FDnaValue Value;
			/** For Emit: the event as raised. */
			FDnaEventPayload Event;
			/** For Set: the fact's slot and the value as the state encodes it. */
			int32 Slot = INDEX_NONE;
			int32 Code = 0;
		};

		enum class ERunEnd : uint8
		{
			/** Back between moves: a new state. */
			Ended,
			/** A talk with no entry that applies. */
			NotApplicable,
			/** Reached a choice already explored another way. */
			Cut,
			/** The runner gave up on a node that never yields. */
			RanAway,
			/** Lines and waits without end. */
			Endless,
			/** Waiting with nothing on the clock. */
			Stuck,
		};

		struct FRunOutcome
		{
			ERunEnd End = ERunEnd::Ended;
			TArray<FPoint> Points;
			TArray<FString> Logged;
			bool bDiverged = false;
			/** The node the dialogue was in when it stopped. */
			FName LastNode;
		};

		/** What a replay saw, for writing the path down. */
		struct FRecording
		{
			TArray<TArray<FDnaChoiceView>> Choices;
			/** The channel each of Choices was offered on. */
			TArray<FName> ChoiceChannels;
			TArray<FString> DrawNotes;
			/** For a wait: how long it was. */
			double WaitSeconds = 0.0;
		};

		/** The choice points of one move from one state, and which of them can still get to the end. */
		struct FDialogueGraph
		{
			TMap<FIntKey, int32> Index;
			TArray<TArray<uint8>> Owner;
			TArray<TArray<int32>> Next;
			TArray<bool> bEnds;
			TArray<FName> Node;
			TArray<FString> Labels;

			FDialogueGraph()
			{
				// Node 0 is the start of the dialogue.
				Add(FIntKey(), {}, NAME_None, FString());
			}

			int32 Add(FIntKey&& Key, TArray<uint8>&& Prefix, FName InNode, FString&& InLabels)
			{
				const int32 Id = Owner.Num();
				if (Id > 0)
				{
					Index.Add(MoveTemp(Key), Id);
				}
				Owner.Add(MoveTemp(Prefix));
				Next.AddDefaulted();
				bEnds.Add(false);
				Node.Add(InNode);
				Labels.Add(MoveTemp(InLabels));
				return Id;
			}
		};

		struct FState
		{
			TArray<int32> Values;
			int32 Parent = INDEX_NONE;
			int32 Move = INDEX_NONE;
			TArray<uint8> Decisions;
			int32 Depth = 0;
			bool bExpanded = false;
			/** Some move from it was not followed all the way (a limit): it may lead further than seen. */
			bool bPartial = false;
			TArray<int32> Next;
		};

		/** A path to write down once the exploration is over, for a finding. */
		struct FPendingRepro
		{
			int32 Finding = INDEX_NONE;
			int32 State = INDEX_NONE;
			int32 Move = INDEX_NONE;
			TArray<uint8> Decisions;
			/** Only this many decision points of the move count (the rest came after what matters). */
			int32 MaxPoints = INDEX_NONE;
		};
	}

	// ======================================================================== the explorer

	class FDnaExplorer::FImpl
	{
	public:
		FImpl(FDnaTestWorld& InWorld, const FDnaExploreOptions& InOptions)
			: World(InWorld), Options(InOptions)
		{
		}

		TArray<FDnaExploreFinding> Findings;
		FDnaExploreStats Stats;

		void Run()
		{
			const double StartTime = FPlatformTime::Seconds();
			Deadline = StartTime + Options.MaxSeconds;

			CollectProgram();
			AnalyseUsage();
			BuildSlots();
			BuildMoves();
			GoalHolds.SetNum(Options.Goals.Num());
			GoalMet.Init(false, Options.Goals.Num());
			GoalReported.Init(false, Options.Goals.Num());

			IDnaTrace* const SavedTrace = Ledger().GetTrace();
			IDnaRandomSource* const SavedSource = Ledger().GetRandomSource();
			Ledger().SetTrace(&Trace);
			Ledger().SetRandomSource(&Draws);
			{
				FDnaLogCapture Capture;
				Log = &Capture;

				TArray<int32> Values;
				Encode(Values);
				AddState(MoveTemp(Values), INDEX_NONE, INDEX_NONE, {});

				for (int32 Next = 0; Next < States.Num() && !bStopped; ++Next)
				{
					if (States[Next].Depth >= Options.MaxDepth)
					{
						NoteLimit(FString::Printf(TEXT("depth %d"), Options.MaxDepth));
						continue;
					}
					Expand(Next);
				}

				Stats.States = States.Num();
				Stats.Seconds = FPlatformTime::Seconds() - StartTime;
				Stats.bExhausted = !States.ContainsByPredicate([](const FState& State) { return !State.bExpanded || State.bPartial; });

				// Writing a path down replays it, so it happens while everything is still in place.
				CheckGoals();
				WriteDownPaths();
				Restore(States[0].Values);
				Log = nullptr;
			}
			Story().StopAllDialogue();
			Ledger().SetRandomSource(SavedSource);
			Ledger().SetTrace(SavedTrace);

			CheckUnreachable();
			if (!Stats.bExhausted)
			{
				FDnaExploreFinding& Finding = Findings.AddDefaulted_GetRef();
				Finding.Code = TEXT("DNS9201");
				Finding.Message = FString::Printf(TEXT("The exploration did not finish: it stopped at %s after %d states (%d moves deep, %.1f s), so what it did not reach is unknown. "
					"Raise `depth`, or look for a number that keeps growing and is compared with nothing that would stop it."),
					Stats.LimitHit.IsEmpty() ? TEXT("a limit") : *Stats.LimitHit, Stats.States, Stats.Depth, Stats.Seconds);
			}
		}

	private:
		FDnaTestWorld& World;
		FDnaExploreOptions Options;

		// -- the program
		TArray<FName> FactNames;
		TArray<FDnaFactDecl> FactDecls;
		TArray<FName> NodeNames;
		TArray<FName> OnceIds;
		TArray<FName> ChapterNames;
		/** The timers a state can have running: the timeouts of quests that have one. */
		TArray<FName> TimerNames;
		TArray<FName> Talkers;
		TArray<FName> Roots;
		/** Events some dialogue opens on. */
		TArray<FName> HeardEvents;
		TArray<FName> ChannelNames;
		TArray<FName> AllLines;
		TArray<FName> AllOptions;
		TMap<FName, int32> OptionIndex;
		TMap<FName, int32> NodeIndex;

		// -- the state layout
		TMap<FName, FUsage> FactUse;
		TMap<FName, FUsage> VisitUse;
		/** Keyed `事件.参数`. */
		TMap<FName, FUsage> ParamUse;
		TArray<FFold> Folds;
		int32 VisitBase = 0;
		int32 OnceBase = 0;
		int32 ChapterSlot = 0;
		int32 TimerBase = 0;
		TArray<FMove> Moves;

		// -- the exploration
		TArray<FState> States;
		TMap<FIntKey, int32> StateIndex;
		TArray<TBitArray<>> GoalHolds;
		TBitArray<> GoalMet;
		TBitArray<> GoalReported;
		TSet<FString> Reported;
		TArray<FPendingRepro> Pending;
		TSet<FName> SeenLines;
		TSet<FName> SeenOptions;
		TSet<FName> SeenNodes;
		TSet<FName> SeenChapters;
		bool bStopped = false;
		double Deadline = 0.0;

		// -- the machinery
		FExploreTrace Trace;
		FDrawSource Draws;
		FDnaLogCapture* Log = nullptr;
		int32 RunsSinceCollect = 0;

		UDreamNarrativeLedger& Ledger() const { return World.Ledger(); }
		UDreamNarrativeStory& Story() const { return World.Story(); }

		// ================================================================ setup

		void CollectProgram()
		{
			TArray<FName> Facts;
			Ledger().GetFactNames(Facts);
			Facts.Sort([](const FName& A, const FName& B) { return A.ToString() < B.ToString(); });
			for (const FName& Fact : Facts)
			{
				if (const FDnaFactDecl* Decl = Ledger().FindFactDecl(Fact))
				{
					FactNames.Add(Fact);
					FactDecls.Add(*Decl);
				}
			}
			Ledger().GetChapterNames(ChapterNames);
			TArray<FName> Quests;
			Story().GetQuestNames(Quests);
			Quests.Sort([](const FName& A, const FName& B) { return A.ToString() < B.ToString(); });
			for (const FName& Quest : Quests)
			{
				const FDnaQuestData* Data = Story().FindQuest(Quest);
				if (Data && Data->Timeout > 0.f)
				{
					TimerNames.Add(::DreamNarrative::QuestStateFact(Quest));
				}
			}

			TSet<FName> Targets;
			TSet<FName> TalkerSet;
			TSet<FName> EventSet;
			for (const TObjectPtr<UDreamNarrativeScript>& Script : World.GetScripts())
			{
				const UDnaDialogueSection* Dialogue = Script ? Script->FindSection<UDnaDialogueSection>() : nullptr;
				if (!Dialogue)
				{
					continue;
				}
				for (const FDnaDialogueNodeData& Node : Dialogue->Nodes)
				{
					NodeIndex.Add(Node.Name, NodeNames.Add(Node.Name));
					if (!Node.ForCharacter.IsNone())
					{
						TalkerSet.Add(Node.ForCharacter);
					}
					if (!Node.OnEvent.IsNone())
					{
						EventSet.Add(Node.OnEvent);
					}
					for (const FDnaLineData& Line : Node.Lines)
					{
						AllLines.Add(Line.Id);
					}
					for (const FDnaChoiceGroupData& Group : Node.ChoiceGroups)
					{
						for (const FDnaChoiceData& Option : Group.Options)
						{
							OptionIndex.Add(Option.Id, AllOptions.Add(Option.Id));
							if (Option.bOnce)
							{
								OnceIds.Add(Option.Id);
							}
						}
					}
					for (const FDnaInstruction& Instruction : Node.Code)
					{
						if (Instruction.Op == EDnaOpCode::GoTo)
						{
							Targets.Add(Instruction.Name);
						}
					}
				}
			}
			Talkers = TalkerSet.Array();
			Talkers.Sort([](const FName& A, const FName& B) { return A.ToString() < B.ToString(); });
			HeardEvents = EventSet.Array();
			HeardEvents.Sort([](const FName& A, const FName& B) { return A.ToString() < B.ToString(); });
			Story().GetChannelNames(ChannelNames);
			// A dialogue with no `for` and no `on` that no other dialogue goes to can only be started by the game.
			for (const TObjectPtr<UDreamNarrativeScript>& Script : World.GetScripts())
			{
				const UDnaDialogueSection* Dialogue = Script ? Script->FindSection<UDnaDialogueSection>() : nullptr;
				for (const FDnaDialogueNodeData& Node : Dialogue ? Dialogue->Nodes : TArray<FDnaDialogueNodeData>())
				{
					if (Node.ForCharacter.IsNone() && Node.OnEvent.IsNone() && !Targets.Contains(Node.Name))
					{
						Roots.Add(Node.Name);
					}
				}
			}
		}

		enum class ERole : uint8
		{
			Condition,
			Value,
			Display,
		};

		void AnalyseUsage()
		{
			for (const TObjectPtr<UDreamNarrativeScript>& Script : World.GetScripts())
			{
				if (!Script)
				{
					continue;
				}
				for (const FDnaChapterDecl& Chapter : Script->Chapters)
				{
					for (const FDnaAdvanceRule& Rule : Chapter.Advance)
					{
						Scan(Script->GetExpression(Rule.ConditionExpr), ERole::Condition);
					}
				}
				const UDnaDialogueSection* Dialogue = Script->FindSection<UDnaDialogueSection>();
				if (!Dialogue)
				{
					continue;
				}
				for (const FDnaDialogueNodeData& Node : Dialogue->Nodes)
				{
					// A `once` entry asks whether it was ever left: `visited > 0`.
					if (Node.bOnce)
					{
						FUsage& Usage = VisitUse.FindOrAdd(Node.Name);
						Usage.bRead = true;
						Usage.Compares.Add({ EDnaExprOperator::Gt, 0.0 });
					}
					Scan(Script->GetExpression(Node.Condition), ERole::Condition);
					for (const FDnaLineData& Line : Node.Lines)
					{
						Scan(Script->GetExpression(Line.Condition), ERole::Condition);
						for (const int32 Arg : Line.TextArgs) { Scan(Script->GetExpression(Arg), ERole::Display); }
					}
					for (const FDnaChoiceGroupData& Group : Node.ChoiceGroups)
					{
						for (const FDnaChoiceData& Option : Group.Options)
						{
							Scan(Script->GetExpression(Option.Condition), ERole::Condition);
							for (const int32 Arg : Option.LabelArgs) { Scan(Script->GetExpression(Arg), ERole::Display); }
						}
					}
					for (const FDnaInstruction& Instruction : Node.Code)
					{
						if (Instruction.Op == EDnaOpCode::JumpIfFalse)
						{
							Scan(Script->GetExpression(Instruction.Expr), ERole::Condition);
						}
						else if (Instruction.Op == EDnaOpCode::Set)
						{
							const FDnaCompiledExpr* Value = Script->GetExpression(Instruction.Expr);
							Scan(Value, ERole::Value);
							NoteWrite(Instruction.Name, Instruction.Assign, Value);
						}
					}
				}
			}
			for (const TObjectPtr<UDreamNarrativeScript>& Script : World.GetScripts())
			{
				const UDnaQuestSection* Quests = Script ? Script->FindSection<UDnaQuestSection>() : nullptr;
				for (const FDnaQuestData& Quest : Quests ? Quests->Quests : TArray<FDnaQuestData>())
				{
					Scan(Script->GetExpression(Quest.Available), ERole::Condition);
					Scan(Script->GetExpression(Quest.FailIf), ERole::Condition);
					for (const FDnaObjectiveData& Objective : Quest.Objectives)
					{
						Scan(Script->GetExpression(Objective.Condition), ERole::Condition);
					}
					for (const FDnaQuestHandlerData* Handler : { &Quest.OnAccept, &Quest.OnComplete, &Quest.OnFail, &Quest.OnTimeout })
					{
						for (const FDnaInstruction& Instruction : Handler->Code)
						{
							if (Instruction.Op == EDnaOpCode::Set)
							{
								const FDnaCompiledExpr* Value = Script->GetExpression(Instruction.Expr);
								Scan(Value, ERole::Value);
								NoteWrite(Instruction.Name, Instruction.Assign, Value);
							}
						}
					}
				}
			}
			for (const FDnaExploreGoal& Goal : Options.Goals)
			{
				Scan(World.GetPooledExpression(Goal.Expr), ERole::Condition);
			}
			// A visit count only ever goes up.
			for (TPair<FName, FUsage>& Pair : VisitUse)
			{
				Pair.Value.bUp = true;
			}
		}

		void Scan(const FDnaCompiledExpr* Expr, ERole Role)
		{
			if (Expr && Expr->IsValid() && Role != ERole::Display)
			{
				ScanNode(*Expr, Expr->Root, INDEX_NONE, Role);
			}
		}

		void ScanNode(const FDnaCompiledExpr& Expr, int32 Index, int32 Parent, ERole Role)
		{
			if (!Expr.Nodes.IsValidIndex(Index))
			{
				return;
			}
			const FDnaExprNode& Node = Expr.Nodes[Index];
			if (Node.Kind == EDnaExprNodeKind::Fact)
			{
				NoteRead(FactUse.FindOrAdd(Node.Name), Expr, Index, Parent, Role);
			}
			else if (Node.Kind == EDnaExprNodeKind::Visited)
			{
				NoteRead(VisitUse.FindOrAdd(Node.Name), Expr, Index, Parent, Role);
			}
			else if (Node.Kind == EDnaExprNodeKind::EventParam)
			{
				NoteRead(ParamUse.FindOrAdd(ParamKey(Node.Event, Node.Name)), Expr, Index, Parent, Role);
			}
			for (const int32 Child : Node.Children)
			{
				ScanNode(Expr, Child, Index, Role);
			}
		}

		void NoteRead(FUsage& Usage, const FDnaCompiledExpr& Expr, int32 Index, int32 Parent, ERole Role)
		{
			Usage.bRead = true;
			if (Parent == INDEX_NONE)
			{
				// The whole condition is the number: it asks `x != 0`. As a value it is used as it is.
				if (Role == ERole::Condition) { Usage.Compares.Add({ EDnaExprOperator::Ne, 0.0 }); }
				else { Usage.bExact = true; }
				return;
			}
			const FDnaExprNode& Up = Expr.Nodes[Parent];
			if (Up.Kind == EDnaExprNodeKind::Binary && IsComparison(Up.Operator) && Up.Children.Num() == 2)
			{
				const bool bLeft = Up.Children[0] == Index;
				FDnaValue Constant;
				if (ConstantOf(Expr, Up.Children[bLeft ? 1 : 0], Constant) && Constant.IsNumeric())
				{
					Usage.Compares.Add({ bLeft ? Up.Operator : Mirror(Up.Operator), Constant.AsNumber() });
					return;
				}
				Usage.bExact = true;
				return;
			}
			if ((Up.Kind == EDnaExprNodeKind::Unary && Up.Operator == EDnaExprOperator::Not)
				|| (Up.Kind == EDnaExprNodeKind::Binary && (Up.Operator == EDnaExprOperator::And || Up.Operator == EDnaExprOperator::Or)))
			{
				Usage.Compares.Add({ EDnaExprOperator::Ne, 0.0 });
				return;
			}
			if (Up.Kind == EDnaExprNodeKind::In && Up.Children.Num() > 0 && Up.Children[0] == Index)
			{
				for (int32 Member = 1; Member < Up.Children.Num(); ++Member)
				{
					FDnaValue Constant;
					if (ConstantOf(Expr, Up.Children[Member], Constant) && Constant.IsNumeric())
					{
						Usage.Compares.Add({ EDnaExprOperator::Eq, Constant.AsNumber() });
					}
					else
					{
						Usage.bExact = true;
					}
				}
				return;
			}
			if (Up.Kind == EDnaExprNodeKind::Binary && Up.Operator == EDnaExprOperator::Mod && Up.Children.Num() == 2 && Up.Children[0] == Index)
			{
				FDnaValue Constant;
				if (ConstantOf(Expr, Up.Children[1], Constant) && Constant.Type == EDnaFactType::Int && Constant.Int >= 2 && Constant.Int <= 16)
				{
					Usage.Moduli.AddUnique(Constant.Int);
				}
			}
			Usage.bExact = true;
		}

		static FName ParamKey(FName Event, FName Param)
		{
			return FName(*FString::Printf(TEXT("%s.%s"), *Event.ToString(), *Param.ToString()));
		}

		void NoteWrite(FName Fact, EDnaAssignOp Assign, const FDnaCompiledExpr* Value)
		{
			FUsage& Usage = FactUse.FindOrAdd(Fact);
			if (Assign == EDnaAssignOp::Set)
			{
				// A value made from the fact itself can walk anywhere; any other value is one it is given.
				if (Value && ReadsFact(*Value, Fact))
				{
					Usage.bUp = Usage.bDown = true;
				}
				return;
			}
			FDnaValue Delta;
			if (Value && ::DreamNarrative::EvaluateConstant(*Value, Delta) && Delta.IsNumeric())
			{
				const double Signed = Assign == EDnaAssignOp::Add ? Delta.AsNumber() : -Delta.AsNumber();
				Usage.bUp |= Signed > 0.0;
				Usage.bDown |= Signed < 0.0;
				return;
			}
			Usage.bUp = Usage.bDown = true;
		}

		void BuildSlots()
		{
			for (int32 Index = 0; Index < FactNames.Num(); ++Index)
			{
				// A quest's state and done objectives are read by the quest machinery itself: always as they are.
				if (FactNames[Index].ToString().StartsWith(TEXT("@")))
				{
					Folds.Add(FFold());
					continue;
				}
				const bool bNumeric = FactDecls[Index].Type == EDnaFactType::Int || FactDecls[Index].Type == EDnaFactType::Float;
				Folds.Add(MakeFold(FactUse.Find(FactNames[Index]), bNumeric));
			}
			VisitBase = Folds.Num();
			for (const FName& Node : NodeNames)
			{
				Folds.Add(MakeFold(VisitUse.Find(Node), /*bNumeric=*/true));
			}
			OnceBase = Folds.Num();
			for (int32 Index = 0; Index < OnceIds.Num(); ++Index)
			{
				Folds.Add(FFold());
			}
			ChapterSlot = Folds.Num();
			Folds.Add(FFold());
			TimerBase = Folds.Num();
			for (int32 Index = 0; Index < TimerNames.Num(); ++Index)
			{
				Folds.Add(FFold());
			}
		}

		void BuildMoves()
		{
			for (const FName& Character : Talkers)
			{
				FMove& Move = Moves.AddDefaulted_GetRef();
				Move.Kind = EMoveKind::Talk;
				Move.Target = Character;
			}
			for (const FName& Root : Roots)
			{
				FMove& Move = Moves.AddDefaulted_GetRef();
				Move.Kind = EMoveKind::Start;
				Move.Target = Root;
			}
			if (TimerNames.Num() > 0)
			{
				Moves.AddDefaulted_GetRef().Kind = EMoveKind::Wait;
			}
			for (int32 Slot = 0; Slot < FactNames.Num(); ++Slot)
			{
				const FDnaFactDecl& Decl = FactDecls[Slot];
				const FUsage* Usage = FactUse.Find(FactNames[Slot]);
				if (Decl.Owner != EDnaFactOwner::Extern || !Usage || !Usage->bRead)
				{
					continue;
				}
				for (const FDnaValue& Value : KeyValues(Decl, *Usage))
				{
					FMove& Move = Moves.AddDefaulted_GetRef();
					Move.Kind = EMoveKind::Set;
					Move.Target = FactNames[Slot];
					Move.Value = Value;
					Move.Slot = Slot;
					Move.Code = EncodeValue(Slot, Value);
				}
			}
			// A new playthrough means something only when something outlives it.
			if (FactDecls.ContainsByPredicate([](const FDnaFactDecl& Decl) { return Decl.bProfile; }))
			{
				Moves.AddDefaulted_GetRef().Kind = EMoveKind::NewGame;
			}
			for (const FName& Event : HeardEvents)
			{
				for (FDnaEventPayload& Payload : KeyPayloads(Event))
				{
					FMove& Move = Moves.AddDefaulted_GetRef();
					Move.Kind = EMoveKind::Emit;
					Move.Target = Event;
					Move.Event = MoveTemp(Payload);
				}
			}
		}

		/**
		 * The ways worth raising an event: every mix of the key values of the parameters some dialogue
		 * reads (the others keep their defaults), or, when that is too many, each such parameter varied
		 * on its own.
		 */
		TArray<FDnaEventPayload> KeyPayloads(FName Event) const
		{
			FDnaEventPayload Plain;
			Plain.Event = Event;
			TArray<TPair<FName, TArray<FDnaValue>>> Varied;
			const FDnaEventDecl* Decl = Ledger().FindEventDecl(Event);
			for (const FDnaFactDecl& Param : Decl ? Decl->Params : TArray<FDnaFactDecl>())
			{
				const FUsage* Usage = ParamUse.Find(ParamKey(Event, Param.Name));
				TArray<FDnaValue> Values = Usage && Usage->bRead ? KeyValues(Param, *Usage) : TArray<FDnaValue>();
				if (Values.Num() > 0)
				{
					Varied.Add({ Param.Name, MoveTemp(Values) });
				}
			}
			int64 Mixes = 1;
			for (const TPair<FName, TArray<FDnaValue>>& Param : Varied)
			{
				Mixes = FMath::Min<int64>(Mixes * Param.Value.Num(), MaxEventMixes + 1);
			}
			TArray<FDnaEventPayload> Out = { Plain };
			if (Mixes <= MaxEventMixes)
			{
				for (const TPair<FName, TArray<FDnaValue>>& Param : Varied)
				{
					TArray<FDnaEventPayload> Next;
					for (const FDnaEventPayload& Partial : Out)
					{
						for (const FDnaValue& Value : Param.Value)
						{
							Next.Add_GetRef(Partial).Set(Param.Key, Value);
						}
					}
					Out = MoveTemp(Next);
				}
				return Out;
			}
			for (const TPair<FName, TArray<FDnaValue>>& Param : Varied)
			{
				for (const FDnaValue& Value : Param.Value)
				{
					Out.Add_GetRef(Plain).Set(Param.Key, Value);
				}
			}
			return Out;
		}

		/** The values of an extern worth writing: one per stretch its comparisons tell apart, and so on. */
		TArray<FDnaValue> KeyValues(const FDnaFactDecl& Decl, const FUsage& Usage) const
		{
			TArray<FDnaValue> Out;
			switch (Decl.Type)
			{
			case EDnaFactType::Bool:
				Out = { FDnaValue::FromBool(false), FDnaValue::FromBool(true) };
				return Out;
			case EDnaFactType::Tag:
				for (const FName& Member : Decl.Domain) { Out.Add(FDnaValue::FromTag(Member)); }
				return Out;
			case EDnaFactType::Int:
			{
				TArray<int64> Starts;
				for (const TPair<EDnaExprOperator, double>& Compare : Usage.Compares)
				{
					const double C = Compare.Value;
					switch (Compare.Key)
					{
					case EDnaExprOperator::Ge:
					case EDnaExprOperator::Lt:
						Starts.Add(static_cast<int64>(FMath::CeilToDouble(C)));
						break;
					case EDnaExprOperator::Gt:
					case EDnaExprOperator::Le:
						Starts.Add(static_cast<int64>(FMath::FloorToDouble(C)) + 1);
						break;
					default:
						if (C == FMath::FloorToDouble(C))
						{
							Starts.Add(static_cast<int64>(C));
							Starts.Add(static_cast<int64>(C) + 1);
						}
						break;
					}
				}
				Starts.Sort();
				Starts.SetNum(Algo::Unique(Starts));
				int32 Span = 1;
				for (const int32 Modulus : Usage.Moduli) { Span = FMath::Max(Span, Modulus); }

				TArray<int64> Values;
				if (Starts.Num() > 0)
				{
					// Below the first start, reaching down; then each stretch from its start, reaching up.
					for (int32 Step = 0; Step < Span; ++Step) { Values.Add(Starts[0] - 1 - Step); }
					for (int32 Index = 0; Index < Starts.Num(); ++Index)
					{
						const int64 End = Starts.IsValidIndex(Index + 1) ? Starts[Index + 1] - 1 : MAX_int64;
						for (int32 Step = 0; Step < Span && Starts[Index] + Step <= End; ++Step) { Values.Add(Starts[Index] + Step); }
					}
				}
				else if (Span > 1)
				{
					for (int32 Step = 0; Step < Span; ++Step) { Values.Add(Step); }
				}
				else
				{
					Values.Add(Decl.Default.Int);
					Values.Add(static_cast<int64>(Decl.Default.Int) + 1);
				}
				if (Decl.bHasRange)
				{
					for (int64& Value : Values) { Value = FMath::Clamp<int64>(Value, static_cast<int64>(Decl.RangeMin), static_cast<int64>(Decl.RangeMax)); }
					Values.Add(static_cast<int64>(Decl.RangeMin));
					Values.Add(static_cast<int64>(Decl.RangeMax));
				}
				Values.Sort();
				Values.SetNum(Algo::Unique(Values));
				for (const int64 Value : Values) { Out.Add(FDnaValue::FromInt(static_cast<int32>(FMath::Clamp<int64>(Value, MIN_int32, MAX_int32)))); }
				return Out;
			}
			case EDnaFactType::Float:
			{
				TArray<double> Cuts;
				for (const TPair<EDnaExprOperator, double>& Compare : Usage.Compares) { Cuts.Add(Compare.Value); }
				Cuts.Sort();
				Cuts.SetNum(Algo::Unique(Cuts));
				TArray<double> Values;
				if (Cuts.Num() > 0)
				{
					Values.Add(Cuts[0] - 1.0);
					for (int32 Index = 0; Index < Cuts.Num(); ++Index)
					{
						Values.Add(Cuts[Index]);
						Values.Add(Cuts.IsValidIndex(Index + 1) ? (Cuts[Index] + Cuts[Index + 1]) * 0.5 : Cuts[Index] + 1.0);
					}
				}
				else
				{
					Values.Add(Decl.Default.Float);
					Values.Add(Decl.Default.Float + 1.0);
				}
				if (Decl.bHasRange)
				{
					for (double& Value : Values) { Value = FMath::Clamp(Value, Decl.RangeMin, Decl.RangeMax); }
					Values.Add(Decl.RangeMin);
					Values.Add(Decl.RangeMax);
				}
				Values.Sort();
				Values.SetNum(Algo::Unique(Values));
				for (const double Value : Values) { Out.Add(FDnaValue::FromFloat(static_cast<float>(Value))); }
				return Out;
			}
			}
			return Out;
		}

		// ================================================================ states

		int32 EncodeValue(int32 Slot, const FDnaValue& Value) const
		{
			const FDnaFactDecl& Decl = FactDecls[Slot];
			switch (Decl.Type)
			{
			case EDnaFactType::Bool: return Value.AsBool() ? 1 : 0;
			case EDnaFactType::Int: return Value.Int;
			case EDnaFactType::Float: return FloatBits(Value.Float);
			case EDnaFactType::Tag: return Decl.Domain.IndexOfByKey(Value.Tag) + 1;
			}
			return 0;
		}

		FDnaValue DecodeValue(int32 Slot, int32 Code) const
		{
			const FDnaFactDecl& Decl = FactDecls[Slot];
			switch (Decl.Type)
			{
			case EDnaFactType::Bool: return FDnaValue::FromBool(Code != 0);
			case EDnaFactType::Int: return FDnaValue::FromInt(Code);
			case EDnaFactType::Float: return FDnaValue::FromFloat(BitsFloat(Code));
			case EDnaFactType::Tag: return FDnaValue::FromTag(Decl.Domain.IsValidIndex(Code - 1) ? Decl.Domain[Code - 1] : (Decl.Domain.Num() > 0 ? Decl.Domain[0] : NAME_None));
			}
			return FDnaValue();
		}

		void Encode(TArray<int32>& Out) const
		{
			Out.Reset(Folds.Num());
			for (int32 Slot = 0; Slot < FactNames.Num(); ++Slot)
			{
				Out.Add(EncodeValue(Slot, Ledger().GetFact(FactNames[Slot])));
			}
			for (const FName& Node : NodeNames)
			{
				Out.Add(Ledger().GetVisited(Node));
			}
			for (const FName& Id : OnceIds)
			{
				Out.Add(Ledger().WasChosenOnce(Id) ? 1 : 0);
			}
			Out.Add(ChapterNames.IndexOfByKey(Ledger().GetChapter()) + 1);
			for (const FName& Timer : TimerNames)
			{
				Out.Add(FloatBits(Ledger().IsTimerRunning(Timer) ? static_cast<float>(Ledger().GetTimerRemaining(Timer)) : -1.f));
			}
		}

		void Fold(const TArray<int32>& Concrete, FIntKey& Out) const
		{
			Out.Values = Concrete;
			// A timer counts as running or not; how long it has left does not tell states apart.
			for (int32 Slot = TimerBase; Slot < Out.Values.Num(); ++Slot)
			{
				Out.Values[Slot] = BitsFloat(Out.Values[Slot]) >= 0.f ? 1 : 0;
			}
			for (int32 Slot = 0; Slot < ChapterSlot; ++Slot)
			{
				const FFold& Rule = Folds[Slot];
				int32& Code = Out.Values[Slot];
				if (Rule.bIgnore)
				{
					Code = 0;
				}
				else if (!Rule.bExact)
				{
					if (Slot < VisitBase && FactDecls[Slot].Type == EDnaFactType::Float)
					{
						float Value = BitsFloat(Code);
						if (Rule.bHasUpper && Value >= Rule.Upper) { Value = static_cast<float>(Rule.Upper); }
						if (Rule.bHasLower && Value <= Rule.Lower) { Value = static_cast<float>(Rule.Lower); }
						Code = FloatBits(Value);
					}
					else
					{
						if (Rule.bHasUpper && Code >= FMath::CeilToDouble(Rule.Upper)) { Code = static_cast<int32>(FMath::CeilToDouble(Rule.Upper)); }
						if (Rule.bHasLower && Code <= FMath::FloorToDouble(Rule.Lower)) { Code = static_cast<int32>(FMath::FloorToDouble(Rule.Lower)); }
					}
				}
			}
		}

		void Restore(const TArray<int32>& Values)
		{
			FDnaLedgerSnapshot Snapshot;
			for (int32 Slot = 0; Slot < FactNames.Num(); ++Slot)
			{
				FDnaFactSnapshotEntry& Entry = Snapshot.Facts.AddDefaulted_GetRef();
				Entry.Fact = FactNames[Slot];
				Entry.Value = DecodeValue(Slot, Values[Slot]);
			}
			for (int32 Index = 0; Index < NodeNames.Num(); ++Index)
			{
				if (Values[VisitBase + Index] > 0)
				{
					Snapshot.Visited.Add(NodeNames[Index], Values[VisitBase + Index]);
				}
			}
			for (int32 Index = 0; Index < OnceIds.Num(); ++Index)
			{
				if (Values[OnceBase + Index] != 0)
				{
					Snapshot.ChosenOnce.Add(OnceIds[Index]);
				}
			}
			const int32 Chapter = Values[ChapterSlot] - 1;
			Snapshot.Chapter = ChapterNames.IsValidIndex(Chapter) ? ChapterNames[Chapter] : NAME_None;
			for (int32 Index = 0; Index < TimerNames.Num(); ++Index)
			{
				const float Remaining = BitsFloat(Values[TimerBase + Index]);
				if (Remaining >= 0.f)
				{
					Snapshot.Timers.Add(TimerNames[Index], Remaining);
				}
			}
			Ledger().ApplySnapshot(Snapshot);
			// A save leaves the profile out: it goes back on its own.
			FDnaProfileSnapshot Profile;
			for (int32 Slot = 0; Slot < FactNames.Num(); ++Slot)
			{
				if (FactDecls[Slot].bProfile)
				{
					FDnaFactSnapshotEntry& Entry = Profile.Facts.AddDefaulted_GetRef();
					Entry.Fact = FactNames[Slot];
					Entry.Value = DecodeValue(Slot, Values[Slot]);
				}
			}
			if (Profile.Facts.Num() > 0)
			{
				Ledger().ApplyProfileSnapshot(Profile);
			}
		}

		/** Adds the state the ledger is in, unless an equal one is known. Returns its index, INDEX_NONE when the state limit is hit. */
		int32 AddState(TArray<int32>&& Values, int32 Parent, int32 Move, TArray<uint8>&& Decisions)
		{
			FIntKey Key;
			Fold(Values, Key);
			if (const int32* Found = StateIndex.Find(Key))
			{
				return *Found;
			}
			if (States.Num() >= Options.MaxStates)
			{
				NoteLimit(FString::Printf(TEXT("%d states"), Options.MaxStates));
				return INDEX_NONE;
			}
			const int32 Index = States.AddDefaulted();
			FState& State = States[Index];
			State.Values = MoveTemp(Values);
			State.Parent = Parent;
			State.Move = Move;
			State.Decisions = MoveTemp(Decisions);
			State.Depth = Parent == INDEX_NONE ? 0 : States[Parent].Depth + 1;
			Stats.Depth = FMath::Max(Stats.Depth, State.Depth);
			StateIndex.Add(MoveTemp(Key), Index);
			OnNewState(Index);
			return Index;
		}

		/** The ledger is in the new state: check what holds there. */
		void OnNewState(int32 Index)
		{
			SeenChapters.Add(Ledger().GetChapter());
			for (int32 Goal = 0; Goal < Options.Goals.Num(); ++Goal)
			{
				const FDnaCompiledExpr* Expr = World.GetPooledExpression(Options.Goals[Goal].Expr);
				const bool bHolds = Expr && Ledger().EvaluateCondition(*Expr);
				GoalHolds[Goal].Add(bHolds);
				switch (Options.Goals[Goal].Kind)
				{
				case EDnaGoalKind::Always:
					if (!bHolds && !GoalReported[Goal])
					{
						GoalReported[Goal] = true;
						FDnaExploreFinding& Finding = AddFinding(TEXT("DNS5107"), FString::Printf(
							TEXT("%s: `always %s` does not hold in a state %d moves from the start. %s"),
							*Options.Goals[Goal].Where, *Options.Goals[Goal].Source, States[Index].Depth, *DescribeState()));
						Finding.ReproTail = TEXT("expect ") + Options.Goals[Goal].Source;
						Finding.ReproSuffix = TEXT("反例");
						Pending.Add({ Findings.Num() - 1, Index });
					}
					break;
				case EDnaGoalKind::Eventually:
				case EDnaGoalKind::Softlock:
					GoalMet[Goal] = GoalMet[Goal] || bHolds;
					break;
				}
			}
			if (Index < SaveCheckFirst || Index % SaveSampleEvery == 0)
			{
				const TArray<FString> Differences = World.SaveRoundTripDifferences();
				if (Differences.Num() > 0 && !Reported.Contains(TEXT("save")))
				{
					Reported.Add(TEXT("save"));
					FDnaExploreFinding& Finding = AddFinding(TEXT("DNS5104"), FString::Printf(
						TEXT("Saving and loading does not give the same ledger in a state %d moves from the start:\n    %s"),
						States[Index].Depth, *FString::Join(Differences, TEXT("\n    "))));
					Finding.ReproTail = TEXT("save");
					Finding.ReproSuffix = TEXT("存读档");
					Pending.Add({ Findings.Num() - 1, Index });
				}
			}
		}

		FString DescribeState() const
		{
			TArray<FString> Parts;
			for (int32 Slot = 0; Slot < FactNames.Num(); ++Slot)
			{
				if (!Folds[Slot].bIgnore)
				{
					Parts.Add(FString::Printf(TEXT("%s = %s"), *FactNames[Slot].ToString(), *Ledger().GetFact(FactNames[Slot]).ToString()));
				}
			}
			Parts.Add(FString::Printf(TEXT("chapter = %s"), *Ledger().GetChapter().ToString()));
			return TEXT("There: ") + FString::Join(Parts, TEXT(", "));
		}

		void NoteLimit(const FString& What)
		{
			if (Stats.LimitHit.IsEmpty())
			{
				Stats.LimitHit = What;
			}
		}

		FDnaExploreFinding& AddFinding(const TCHAR* Code, const FString& Message, bool bWarning = false)
		{
			FDnaExploreFinding& Finding = Findings.AddDefaulted_GetRef();
			Finding.Code = Code;
			Finding.Message = Message;
			Finding.bWarning = bWarning;
			return Finding;
		}

		// ================================================================ exploring

		void Expand(int32 From)
		{
			for (int32 Move = 0; Move < Moves.Num(); ++Move)
			{
				if (FPlatformTime::Seconds() > Deadline)
				{
					NoteLimit(FString::Printf(TEXT("%g s"), Options.MaxSeconds));
					bStopped = true;
					return;
				}
				const FMove& Candidate = Moves[Move];
				if (Candidate.Kind == EMoveKind::Set && States[From].Values[Candidate.Slot] == Candidate.Code)
				{
					continue;
				}
				if (Candidate.Kind == EMoveKind::Wait && !AnyTimerRunning(States[From].Values))
				{
					continue;
				}
				ExpandMove(From, Move);
			}
			States[From].bExpanded = true;
		}

		/** Every way through one move: replays it with the decisions forced, depth first over the decisions. */
		void ExpandMove(int32 From, int32 Move)
		{
			FDialogueGraph Graph;
			TArray<uint8> Forced;
			for (int32 Run = 0;; ++Run)
			{
				if (Run >= MaxRunsPerMove)
				{
					States[From].bPartial = true;
					NoteLimit(FString::Printf(TEXT("%d ways through one move"), MaxRunsPerMove));
					break;
				}
				FRunOutcome Outcome = RunOnce(From, Move, Forced, &Graph, nullptr);
				++Stats.Runs;
				Handle(From, Move, Outcome);

				int32 Point = Outcome.Points.Num() - 1;
				while (Point >= 0 && Outcome.Points[Point].Taken + 1 >= Outcome.Points[Point].Count)
				{
					--Point;
				}
				if (Point < 0)
				{
					break;
				}
				Forced.SetNum(Point + 1);
				for (int32 Index = 0; Index < Point; ++Index)
				{
					Forced[Index] = Outcome.Points[Index].Taken;
				}
				Forced[Point] = Outcome.Points[Point].Taken + 1;
			}
			CheckTraps(From, Move, Graph);
		}

		FRunOutcome RunOnce(int32 From, int32 MoveIndex, const TArray<uint8>& Forced, FDialogueGraph* Graph, FRecording* Recording)
		{
			Restore(States[From].Values);
			// Restoring ended every dialogue the last run left: nothing holds them any more.
			if (++RunsSinceCollect >= CollectEvery)
			{
				RunsSinceCollect = 0;
				CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
			}
			FDecisions Decisions;
			Decisions.Forced = Forced;
			Draws.Decisions = &Decisions;
			Trace.bRanAway = false;
			if (Log) { Log->Take(); }

			FRunOutcome Out;
			const FMove& Move = Moves[MoveIndex];
			switch (Move.Kind)
			{
			case EMoveKind::Talk:
			{
				const FName Entry = Story().SelectEntry(Move.Target);
				if (Entry.IsNone() || !Story().StartDialogue(Entry)) { Out.End = ERunEnd::NotApplicable; }
				break;
			}
			case EMoveKind::Start:
				if (!Story().StartDialogue(Move.Target)) { Out.End = ERunEnd::NotApplicable; }
				break;
			case EMoveKind::Set:
				Ledger().SetFact(Move.Target, Move.Value, EDnaWriteSource::Game);
				break;
			case EMoveKind::Wait:
				if (Recording)
				{
					Recording->WaitSeconds = World.Clock().GetTimeToNext();
				}
				if (!World.Clock().AdvanceToNext())
				{
					Out.End = ERunEnd::NotApplicable;
				}
				break;
			case EMoveKind::Emit:
				Ledger().RaiseEvent(Move.Event);
				break;
			case EMoveKind::NewGame:
			{
				TArray<int32> Fresh = States[0].Values;
				for (int32 Slot = 0; Slot < FactNames.Num(); ++Slot)
				{
					if (FactDecls[Slot].bProfile)
					{
						Fresh[Slot] = States[From].Values[Slot];
					}
				}
				Restore(Fresh);
				break;
			}
			}
			// A write or a timer can open dialogue too (a quest's `emit`), so every move is talked out.
			if (Out.End != ERunEnd::NotApplicable)
			{
				Drive(Out, Decisions, Graph, Recording);
			}
			NoteVisits(States[From].Values);
			Draws.Decisions = nullptr;
			Out.Points = MoveTemp(Decisions.Points);
			Out.bDiverged = Decisions.bDiverged;
			if (Log) { Out.Logged = Log->Take(); }
			if (Trace.bRanAway)
			{
				Out.End = ERunEnd::RanAway;
			}
			return Out;
		}

		bool AnyTimerRunning(const TArray<int32>& Values) const
		{
			for (int32 Slot = TimerBase; Slot < Values.Num(); ++Slot)
			{
				if (BitsFloat(Values[Slot]) >= 0.f)
				{
					return true;
				}
			}
			return false;
		}

		/**
		 * Lets every channel say all it has, the way a player who never waits would: lines move on at
		 * once, each choice takes the run's next decision, waits run on the clock once nothing else is
		 * left to show. The choice points of a dialogue talking alone go into the move's graph.
		 */
		void Drive(FRunOutcome& Out, FDecisions& Decisions, FDialogueGraph* Graph, FRecording* Recording)
		{
			int32 Previous = 0;
			TArray<const UDreamNarrativeDialogueRunner*> Driven;
			for (int32 Yield = 0;; ++Yield)
			{
				UDreamNarrativeDialogueRunner* Runner = NextRunnerToDrive(Story());
				if (!Runner)
				{
					Out.End = ERunEnd::Ended;
					if (Graph) { Graph->bEnds[Previous] = true; }
					break;
				}
				Driven.AddUnique(Runner);
				Out.LastNode = Runner->GetNodeName();
				const EDnaRunnerState State = Runner->GetState();
				if (Yield >= MaxYieldsPerRun)
				{
					Out.End = ERunEnd::Endless;
					if (Graph) { Graph->bEnds[Previous] = true; }
					Stop();
					break;
				}
				if (State == EDnaRunnerState::ShowingLine)
				{
					SeenLines.Add(Runner->GetCurrentLine().Id);
					Runner->Advance();
				}
				else if (State == EDnaRunnerState::AwaitingChoice)
				{
					const TArray<FDnaChoiceView>& Choices = Runner->GetChoices();
					for (const FDnaChoiceView& Choice : Choices)
					{
						SeenOptions.Add(Choice.Id);
					}
					// With another dialogue under way, where this one stands is not all there is of the run.
					if (Graph && TalksAlone(*Runner))
					{
						FIntKey Key;
						Key.Values.Add(NodeIndex.FindRef(Runner->GetNodeName()));
						for (const FDnaChoiceView& Choice : Choices) { Key.Values.Add(OptionIndex.FindRef(Choice.Id)); }
						Key.Values.Add(-1);
						TArray<int32> Values;
						Encode(Values);
						FIntKey Folded;
						Fold(Values, Folded);
						Key.Values.Append(Folded.Values);

						TArray<uint8> Prefix = Decisions.TakenSoFar();
						const int32* Found = Graph->Index.Find(Key);
						const int32 Point = Found ? *Found : Graph->Add(MoveTemp(Key), CopyTemp(Prefix), Runner->GetNodeName(), DescribeChoices(Choices));
						Graph->Next[Previous].AddUnique(Point);
						if (Found && Graph->Owner[Point] != Prefix)
						{
							// Reached before along another way: everything from here was, or will be, explored from there.
							Out.End = ERunEnd::Cut;
							Stop();
							break;
						}
						Previous = Point;
					}
					if (Recording)
					{
						Recording->Choices.Add(Choices);
						Recording->ChoiceChannels.Add(Runner->GetChannel());
					}
					Runner->Choose(Decisions.Take(Choices.Num(), /*bDraw=*/false));
				}
				else if (State == EDnaRunnerState::Waiting)
				{
					if (!World.Clock().AdvanceToNext())
					{
						Out.End = ERunEnd::Stuck;
						Stop();
						break;
					}
				}
				else if (State == EDnaRunnerState::PlayingStage)
				{
					Runner->NotifyStageFinished();
				}
				else
				{
					Out.End = ERunEnd::Stuck;
					Stop();
					break;
				}
			}
			for (const UDreamNarrativeDialogueRunner* Runner : Driven)
			{
				NoteEnteredNodes(*Runner);
			}
		}

		/** Nothing but Runner talks, or waits its turn. */
		bool TalksAlone(const UDreamNarrativeDialogueRunner& Runner) const
		{
			for (const FName& Channel : ChannelNames)
			{
				const UDreamNarrativeDialogueRunner* Other = Story().GetRunner(Channel);
				if ((Other && Other != &Runner) || Story().GetQueueLength(Channel) > 0)
				{
					return false;
				}
			}
			return true;
		}

		/** Ends the dialogues the explorer leaves; nothing they do from here is a decision. */
		void Stop()
		{
			Draws.Decisions = nullptr;
			Story().StopAllDialogue();
		}

		void NoteEnteredNodes(const UDreamNarrativeDialogueRunner& Runner)
		{
			for (const FString& Entry : Runner.GetTranscript())
			{
				if (Entry.StartsWith(TEXT("[enter ")) && Entry.EndsWith(TEXT("]")))
				{
					SeenNodes.Add(FName(*Entry.Mid(7, Entry.Len() - 8)));
				}
			}
		}

		/** A dialogue that started and ended inside another's step was never driven, but it was left. */
		void NoteVisits(const TArray<int32>& Before)
		{
			for (int32 Index = 0; Index < NodeNames.Num(); ++Index)
			{
				if (Ledger().GetVisited(NodeNames[Index]) > Before[VisitBase + Index])
				{
					SeenNodes.Add(NodeNames[Index]);
				}
			}
		}

		static FString DescribeChoices(const TArray<FDnaChoiceView>& Choices)
		{
			TArray<FString> Labels;
			for (const FDnaChoiceView& Choice : Choices) { Labels.Add(TEXT("\"") + Choice.Label.ToString() + TEXT("\"")); }
			return FString::Join(Labels, TEXT(", "));
		}

		FString DescribeMove(int32 MoveIndex) const
		{
			const FMove& Move = Moves[MoveIndex];
			switch (Move.Kind)
			{
			case EMoveKind::Talk: return FString::Printf(TEXT("talk %s"), *Move.Target.ToString());
			case EMoveKind::Start: return FString::Printf(TEXT("start %s"), *Move.Target.ToString());
			case EMoveKind::Set: return FString::Printf(TEXT("set %s = %s"), *Move.Target.ToString(), *Move.Value.ToString());
			case EMoveKind::Wait: return TEXT("wait for the next timer");
			case EMoveKind::Emit: return TEXT("emit ") + Move.Event.ToString();
			case EMoveKind::NewGame: return TEXT("newgame");
			}
			return FString();
		}

		void Handle(int32 From, int32 Move, FRunOutcome& Outcome)
		{
			auto Report = [&](const TCHAR* Code, const FString& Key, const FString& Message, const TCHAR* Suffix)
			{
				if (Reported.Contains(Key))
				{
					return;
				}
				Reported.Add(Key);
				FDnaExploreFinding& Finding = AddFinding(Code, Message);
				Finding.ReproSuffix = Suffix;
				Pending.Add({ Findings.Num() - 1, From, Move, Outcome.bDiverged ? TArray<uint8>() : TakenIn(Outcome) });
			};

			// A runaway logs an error of its own; the runaway says more.
			if (Outcome.Logged.Num() > 0 && Outcome.End != ERunEnd::RanAway)
			{
				Report(TEXT("DNS5101"), TEXT("log:") + Outcome.Logged[0], FString::Printf(TEXT("`%s`, %d moves from the start, logged:\n    %s"),
					*DescribeMove(Move), States[From].Depth + 1, *FString::Join(Outcome.Logged, TEXT("\n    "))), TEXT("报错"));
			}
			if (Outcome.bDiverged)
			{
				Report(TEXT("DNS5101"), TEXT("diverged:") + DescribeMove(Move), FString::Printf(
					TEXT("`%s` went another way when replayed with the same choices and draws: something the story runs on is not the same twice."), *DescribeMove(Move)), TEXT("报错"));
			}
			switch (Outcome.End)
			{
			case ERunEnd::RanAway:
				Report(TEXT("DNS5102"), TEXT("stop:") + Outcome.LastNode.ToString(), FString::Printf(
					TEXT("`%s` does not stop: dialogue %s keeps jumping without showing a line or offering a choice, and the runner gave up on it."),
					*DescribeMove(Move), *Outcome.LastNode.ToString()), TEXT("停不下来"));
				AddTransition(From, Move, Outcome);
				break;
			case ERunEnd::Endless:
				Report(TEXT("DNS5102"), TEXT("stop:") + Outcome.LastNode.ToString(), FString::Printf(
					TEXT("`%s` does not stop: after %d lines and waits it is still going, in dialogue %s, without a choice that leads out."),
					*DescribeMove(Move), MaxYieldsPerRun, *Outcome.LastNode.ToString()), TEXT("停不下来"));
				break;
			case ERunEnd::Stuck:
				Report(TEXT("DNS5101"), TEXT("stuck:") + Outcome.LastNode.ToString(), FString::Printf(
					TEXT("`%s` stopped in dialogue %s waiting for something nothing will finish."), *DescribeMove(Move), *Outcome.LastNode.ToString()), TEXT("报错"));
				break;
			case ERunEnd::Ended:
				AddTransition(From, Move, Outcome);
				break;
			default:
				break;
			}
		}

		/** The decisions a run took, to replay it. */
		static TArray<uint8> TakenIn(const FRunOutcome& Outcome)
		{
			TArray<uint8> Out;
			for (const FPoint& Point : Outcome.Points) { Out.Add(Point.Taken); }
			return Out;
		}

		void AddTransition(int32 From, int32 Move, const FRunOutcome& Outcome)
		{
			TArray<int32> Values;
			Encode(Values);
			const int32 To = AddState(MoveTemp(Values), From, Move, TakenIn(Outcome));
			if (To == INDEX_NONE)
			{
				States[From].bPartial = true;
				return;
			}
			++Stats.Transitions;
			if (To != From)
			{
				States[From].Next.AddUnique(To);
			}
		}

		void CheckTraps(int32 From, int32 Move, const FDialogueGraph& Graph)
		{
			const int32 Count = Graph.Owner.Num();
			TBitArray<> CanEnd(false, Count);
			TArray<TArray<int32>> Previous;
			Previous.SetNum(Count);
			TArray<int32> Work;
			for (int32 Point = 0; Point < Count; ++Point)
			{
				for (const int32 Next : Graph.Next[Point]) { Previous[Next].Add(Point); }
				if (Graph.bEnds[Point])
				{
					CanEnd[Point] = true;
					Work.Add(Point);
				}
			}
			while (Work.Num() > 0)
			{
				const int32 Point = Work.Pop(EAllowShrinking::No);
				for (const int32 Earlier : Previous[Point])
				{
					if (!CanEnd[Earlier])
					{
						CanEnd[Earlier] = true;
						Work.Add(Earlier);
					}
				}
			}
			for (int32 Point = 1; Point < Count; ++Point)
			{
				if (CanEnd[Point])
				{
					continue;
				}
				const FString Key = FString::Printf(TEXT("trap:%s:%s"), *Graph.Node[Point].ToString(), *Graph.Labels[Point]);
				if (!Reported.Contains(Key))
				{
					Reported.Add(Key);
					FDnaExploreFinding& Finding = AddFinding(TEXT("DNS5103"), FString::Printf(
						TEXT("`%s` cannot be left: in dialogue %s, from the choice between %s, no way on reaches the end of the dialogue."),
						*DescribeMove(Move), *Graph.Node[Point].ToString(), *Graph.Labels[Point]));
					Finding.ReproSuffix = TEXT("走不出去");
					Pending.Add({ Findings.Num() - 1, From, Move, Graph.Owner[Point], Graph.Owner[Point].Num() });
				}
				break;
			}
		}

		// ================================================================ afterwards

		void CheckGoals()
		{
			for (int32 Goal = 0; Goal < Options.Goals.Num(); ++Goal)
			{
				const FDnaExploreGoal& Spec = Options.Goals[Goal];
				if (Spec.Kind == EDnaGoalKind::Always)
				{
					continue;
				}
				if (!GoalMet[Goal])
				{
					AddFinding(TEXT("DNS5108"), Stats.bExhausted
						? FString::Printf(TEXT("%s: `%s` holds in none of the %d states reachable from the start; every one of them was explored."), *Spec.Where, *Spec.Source, Stats.States)
						: FString::Printf(TEXT("%s: `%s` holds in none of the %d states explored, and the exploration did not finish."), *Spec.Where, *Spec.Source, Stats.States));
					continue;
				}
				if (Spec.Kind == EDnaGoalKind::Softlock)
				{
					CheckSoftlock(Goal);
				}
			}
		}

		TBitArray<> ReachBackwards(const TArray<TArray<int32>>& Previous, TFunctionRef<bool(int32)> IsSeed) const
		{
			TBitArray<> Reached(false, States.Num());
			TArray<int32> Work;
			for (int32 State = 0; State < States.Num(); ++State)
			{
				if (IsSeed(State))
				{
					Reached[State] = true;
					Work.Add(State);
				}
			}
			while (Work.Num() > 0)
			{
				const int32 State = Work.Pop(EAllowShrinking::No);
				for (const int32 Earlier : Previous[State])
				{
					if (!Reached[Earlier])
					{
						Reached[Earlier] = true;
						Work.Add(Earlier);
					}
				}
			}
			return Reached;
		}

		void CheckSoftlock(int32 Goal)
		{
			const FDnaExploreGoal& Spec = Options.Goals[Goal];
			TArray<TArray<int32>> Previous;
			Previous.SetNum(States.Num());
			for (int32 State = 0; State < States.Num(); ++State)
			{
				for (const int32 Next : States[State].Next) { Previous[Next].Add(State); }
			}
			const TBitArray<>& Holds = GoalHolds[Goal];
			const TBitArray<> CanReach = ReachBackwards(Previous, [&Holds](int32 State) { return Holds[State]; });
			const TBitArray<> MayGoFurther = ReachBackwards(Previous, [this](int32 State) { return !States[State].bExpanded || States[State].bPartial; });

			int32 FirstStuck = INDEX_NONE;
			int32 FirstUnsure = INDEX_NONE;
			int32 Stuck = 0;
			int32 Unsure = 0;
			for (int32 State = 0; State < States.Num(); ++State)
			{
				if (CanReach[State])
				{
					continue;
				}
				if (MayGoFurther[State])
				{
					FirstUnsure = FirstUnsure == INDEX_NONE ? State : FirstUnsure;
					++Unsure;
				}
				else
				{
					FirstStuck = FirstStuck == INDEX_NONE ? State : FirstStuck;
					++Stuck;
				}
			}
			if (FirstStuck != INDEX_NONE)
			{
				Restore(States[FirstStuck].Values);
				FDnaExploreFinding& Finding = AddFinding(TEXT("DNS5105"), FString::Printf(
					TEXT("%s: softlock. `%s` can be reached from the start, but not from %d reachable state(s); the nearest is %d moves from the start, and everything after it was explored. %s"),
					*Spec.Where, *Spec.Source, Stuck, States[FirstStuck].Depth, *DescribeState()));
				Finding.ReproTail = TEXT("eventually ") + Spec.Source;
				Finding.bReproIsExplore = true;
				Finding.ReproSuffix = TEXT("软锁");
				WriteDownPath(Finding, FirstStuck, INDEX_NONE, {}, INDEX_NONE);
			}
			if (FirstUnsure != INDEX_NONE)
			{
				Restore(States[FirstUnsure].Values);
				FDnaExploreFinding& Finding = AddFinding(TEXT("DNS5106"), FString::Printf(
					TEXT("%s: possible softlock. `%s` was not reached from %d state(s) whose way on was not explored to the end; the nearest is %d moves from the start. %s"),
					*Spec.Where, *Spec.Source, Unsure, States[FirstUnsure].Depth, *DescribeState()), /*bWarning=*/true);
				Finding.ReproTail = TEXT("eventually ") + Spec.Source;
				Finding.bReproIsExplore = true;
				Finding.ReproSuffix = TEXT("疑似软锁");
				WriteDownPath(Finding, FirstUnsure, INDEX_NONE, {}, INDEX_NONE);
			}
			Restore(States[0].Values);
		}

		void CheckUnreachable()
		{
			const FString Scope = Stats.bExhausted ? TEXT("anywhere reachable") : TEXT("anywhere the exploration got to");
			auto List = [this, &Scope](const TArray<FName>& All, const TSet<FName>& Seen, const TCHAR* What)
			{
				TArray<FString> Missing;
				for (const FName& Name : All)
				{
					if (!Name.IsNone() && !Seen.Contains(Name))
					{
						Missing.AddUnique(Name.ToString());
					}
				}
				if (Missing.Num() == 0)
				{
					return;
				}
				const int32 Total = Missing.Num();
				if (Total > MaxListed)
				{
					Missing.SetNum(MaxListed);
					Missing.Add(FString::Printf(TEXT("and %d more"), Total - MaxListed));
				}
				AddFinding(TEXT("DNS5110"), FString::Printf(TEXT("%d %s never came up %s: %s"), Total, What, *Scope, *FString::Join(Missing, TEXT(", "))), /*bWarning=*/true);
			};
			List(NodeNames, SeenNodes, TEXT("dialogue(s)"));
			List(AllLines, SeenLines, TEXT("line(s)"));
			List(AllOptions, SeenOptions, TEXT("choice(s)"));
			List(ChapterNames, SeenChapters, TEXT("chapter(s)"));
		}

		// ================================================================ writing paths down

		void WriteDownPaths()
		{
			for (const FPendingRepro& Repro : Pending)
			{
				WriteDownPath(Findings[Repro.Finding], Repro.State, Repro.Move, Repro.Decisions, Repro.MaxPoints);
			}
			Pending.Reset();
		}

		/** The moves from the start to State, and then Move with Decisions when given, as step lines. */
		void WriteDownPath(FDnaExploreFinding& Finding, int32 State, int32 Move, const TArray<uint8>& Decisions, int32 MaxPoints)
		{
			TArray<int32> Chain;
			for (int32 Cursor = State; Cursor != INDEX_NONE && States[Cursor].Parent != INDEX_NONE; Cursor = States[Cursor].Parent)
			{
				Chain.Insert(Cursor, 0);
			}
			for (const int32 Step : Chain)
			{
				WriteDownMove(Finding.ReproSteps, States[Step].Parent, States[Step].Move, States[Step].Decisions, INDEX_NONE);
			}
			if (Move != INDEX_NONE)
			{
				WriteDownMove(Finding.ReproSteps, State, Move, Decisions, MaxPoints);
			}
		}

		void WriteDownMove(TArray<FString>& Lines, int32 From, int32 MoveIndex, const TArray<uint8>& Decisions, int32 MaxPoints)
		{
			FRecording Recording;
			Trace.DrawNotes = &Recording.DrawNotes;
			const FRunOutcome Outcome = RunOnce(From, MoveIndex, Decisions, nullptr, &Recording);
			Trace.DrawNotes = nullptr;
			const int32 Limit = MaxPoints == INDEX_NONE ? Outcome.Points.Num() : FMath::Min(MaxPoints, Outcome.Points.Num());

			TArray<FString> Rolls;
			TArray<FString> Notes;
			TArray<FString> Picks;
			// Picks on other channels go under a `channel` block of the step, in the order the channels first chose.
			TArray<TPair<FName, TArray<FString>>> ChannelPicks;
			int32 Draw = 0;
			int32 Choice = 0;
			for (int32 Point = 0; Point < Limit; ++Point)
			{
				const FPoint& Taken = Outcome.Points[Point];
				if (Taken.bDraw)
				{
					Rolls.Add(Taken.Taken == 0 ? TEXT("low") : TEXT("high"));
					if (Recording.DrawNotes.IsValidIndex(Draw)) { Notes.Add(Recording.DrawNotes[Draw]); }
					++Draw;
				}
				else if (Recording.Choices.IsValidIndex(Choice))
				{
					const FName Channel = Recording.ChoiceChannels[Choice];
					const FString Pick = PickText(Recording.Choices[Choice], Taken.Taken);
					if (Channel == ::DreamNarrative::MainChannelName())
					{
						Picks.Add(TEXT("  - ") + Pick);
					}
					else
					{
						TPair<FName, TArray<FString>>* Block = ChannelPicks.FindByPredicate([Channel](const TPair<FName, TArray<FString>>& Each) { return Each.Key == Channel; });
						(Block ? *Block : ChannelPicks.Emplace_GetRef(Channel, TArray<FString>())).Value.Add(TEXT("    - ") + Pick);
					}
					++Choice;
				}
			}
			if (Rolls.Num() > 0)
			{
				Lines.Add(TEXT("rolls ") + FString::Join(Rolls, TEXT(", ")) + (Notes.Num() > 0 ? TEXT("    // ") + FString::Join(Notes, TEXT(";")) : FString()));
			}
			Lines.Add(Moves[MoveIndex].Kind == EMoveKind::Wait ? FString::Printf(TEXT("wait %gs"), Recording.WaitSeconds) : DescribeMove(MoveIndex));
			Lines.Append(Picks);
			for (const TPair<FName, TArray<FString>>& Block : ChannelPicks)
			{
				Lines.Add(TEXT("  channel ") + Block.Key.ToString());
				Lines.Append(Block.Value);
			}
		}

		/** The label when it names the choice on its own and reads back as the same text; the id otherwise. */
		static FString PickText(const TArray<FDnaChoiceView>& Offered, int32 Taken)
		{
			if (!Offered.IsValidIndex(Taken))
			{
				return TEXT("?");
			}
			const FString Label = Offered[Taken].Label.ToString();
			const bool bUnique = Offered.FilterByPredicate([&Label](const FDnaChoiceView& Choice) { return Choice.Label.ToString() == Label; }).Num() == 1;
			const bool bPlain = !Label.IsEmpty() && Label == Label.TrimStartAndEnd() && !Label.Contains(TEXT("{")) && !Label.Contains(TEXT("}"))
				&& !Label.Contains(TEXT("//")) && !Label.Contains(TEXT("\n")) && !Label.StartsWith(TEXT("#")) && !Label.StartsWith(TEXT("＃"));
			return bUnique && bPlain ? Label : TEXT("#") + Offered[Taken].Id.ToString();
		}
	};

	// ======================================================================== the facade

	FDnaExplorer::FDnaExplorer(FDnaTestWorld& World, const FDnaExploreOptions& Options)
		: Impl(MakeUnique<FImpl>(World, Options))
	{
	}

	FDnaExplorer::~FDnaExplorer() = default;

	void FDnaExplorer::Run()
	{
		Impl->Run();
	}

	const TArray<FDnaExploreFinding>& FDnaExplorer::GetFindings() const
	{
		return Impl->Findings;
	}

	const FDnaExploreStats& FDnaExplorer::GetStats() const
	{
		return Impl->Stats;
	}
}
