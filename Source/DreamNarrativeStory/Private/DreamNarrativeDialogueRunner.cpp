// Copyright DreamMoon. All Rights Reserved.

#include "DreamNarrativeDialogueRunner.h"
#include "DreamNarrativeLedger.h"
#include "DreamNarrativeStory.h"
#include "DreamNarrativeStoryModule.h"

bool UDreamNarrativeDialogueRunner::Start(UDreamNarrativeStory* InStory, UDreamNarrativeLedger* InLedger, const UDnaDialogueSection* InSection, FName InNode)
{
	Story = InStory;
	Ledger = InLedger;
	Section = const_cast<UDnaDialogueSection*>(InSection);
	Transcript.Reset();
	if (!Ledger || !Section || !EnterNode(InNode))
	{
		State = EDnaRunnerState::Ended;
		return false;
	}
	Run();
	return true;
}

bool UDreamNarrativeDialogueRunner::EnterNode(FName InNode)
{
	const UDnaDialogueSection* Owner = nullptr;
	const FDnaDialogueNodeData* Found = Story ? Story->FindNode(InNode, &Owner) : (Section ? Section->FindNode(InNode) : nullptr);
	if (!Found)
	{
		UE_LOG(LogDreamNarrativeStory, Error, TEXT("Dialogue '%s' does not exist"), *InNode.ToString());
		return false;
	}
	if (Owner)
	{
		Section = const_cast<UDnaDialogueSection*>(Owner);
	}
	Node = Found;
	NodeName = InNode;
	PC = 0;
	Transcript.Add(FString::Printf(TEXT("[enter %s]"), *InNode.ToString()));
	return true;
}

const FDnaCompiledExpr* UDreamNarrativeDialogueRunner::Expr(int32 Index) const
{
	const UDreamNarrativeScript* Script = Section ? Section->GetScript() : nullptr;
	return Script ? Script->GetExpression(Index) : nullptr;
}

bool UDreamNarrativeDialogueRunner::Condition(int32 Index) const
{
	if (Index == INDEX_NONE)
	{
		return true;
	}
	const FDnaCompiledExpr* Compiled = Expr(Index);
	return Compiled && Ledger && Ledger->EvaluateCondition(*Compiled);
}

FText UDreamNarrativeDialogueRunner::FormatText(const FText& Pattern, const TArray<int32>& Args) const
{
	if (Args.Num() == 0)
	{
		return Pattern;
	}
	FFormatOrderedArguments Ordered;
	for (const int32 Index : Args)
	{
		const FDnaCompiledExpr* Compiled = Expr(Index);
		const FDnaValue Value = (Compiled && Ledger) ? Ledger->Evaluate(*Compiled) : FDnaValue();
		switch (Value.Type)
		{
		case EDnaFactType::Int: Ordered.Add(FText::AsNumber(Value.Int)); break;
		case EDnaFactType::Float: Ordered.Add(FText::AsNumber(Value.Float)); break;
		default: Ordered.Add(FText::FromString(Value.ToString())); break;
		}
	}
	return FText::Format(Pattern, Ordered);
}

FText UDreamNarrativeDialogueRunner::DisplayNameOf(FName Character) const
{
	if (const FDnaCharacterDecl* Decl = Ledger ? Ledger->FindCharacterDecl(Character) : nullptr)
	{
		if (!Decl->Display.IsEmpty())
		{
			return Decl->Display;
		}
	}
	return FText::FromName(Character);
}

void UDreamNarrativeDialogueRunner::Run()
{
	if (!Node || !Ledger)
	{
		Finish();
		return;
	}
	StepBudget = 10000;

	while (PC >= 0 && PC < Node->Code.Num())
	{
		if (--StepBudget <= 0)
		{
			UE_LOG(LogDreamNarrativeStory, Error, TEXT("Dialogue '%s' ran %d steps without yielding; aborting"), *NodeName.ToString(), 10000);
			Finish();
			return;
		}

		const FDnaInstruction& Ins = Node->Code[PC];
		switch (Ins.Op)
		{
		case EDnaOpCode::Line:
		{
			const FDnaLineData& Line = Node->Lines[Ins.Index];
			++PC;
			if (!Condition(Line.Condition))
			{
				continue;
			}
			CurrentLine = FDnaLineView();
			CurrentLine.Id = Line.Id;
			CurrentLine.Speaker = Line.Speaker;
			CurrentLine.SpeakerDisplay = Line.Speaker.IsNone() ? FText() : DisplayNameOf(Line.Speaker);
			CurrentLine.Tags = Line.Tags;
			CurrentLine.Text = FormatText(Line.Text, Line.TextArgs);
			CurrentLine.Duration = Line.Duration;
			CurrentLine.Stage = Line.Stage;
			CurrentLine.bNarration = Line.Speaker.IsNone();
			Transcript.Add(Line.Speaker.IsNone()
				? FString::Printf(TEXT("> %s"), *CurrentLine.Text.ToString())
				: FString::Printf(TEXT("%s: %s"), *Line.Speaker.ToString(), *CurrentLine.Text.ToString()));
			State = EDnaRunnerState::ShowingLine;
			OnLine.Broadcast(this, CurrentLine);
			return;
		}
		case EDnaOpCode::Choice:
		{
			const FDnaChoiceGroupData& Group = Node->ChoiceGroups[Ins.Index];
			CurrentChoices.Reset();
			CurrentOptionIndices.Reset();
			for (int32 OptionIndex = 0; OptionIndex < Group.Options.Num(); ++OptionIndex)
			{
				const FDnaChoiceData& Option = Group.Options[OptionIndex];
				if (Option.bOnce && Ledger->WasChosenOnce(Option.Id))
				{
					continue;
				}
				if (!Condition(Option.Condition))
				{
					continue;
				}
				FDnaChoiceView& View = CurrentChoices.AddDefaulted_GetRef();
				View.Index = CurrentChoices.Num() - 1;
				View.Id = Option.Id;
				View.Label = FormatText(Option.Label, Option.LabelArgs);
				CurrentOptionIndices.Add(OptionIndex);
			}
			if (CurrentChoices.Num() == 0)
			{
				// Every option was hidden: fall through to the gather. DNS5008 makes this rare.
				PC = Group.Gather;
				continue;
			}
			CurrentGroup = Ins.Index;
			for (const FDnaChoiceView& View : CurrentChoices)
			{
				Transcript.Add(FString::Printf(TEXT("  - %s"), *View.Label.ToString()));
			}
			State = EDnaRunnerState::AwaitingChoice;
			OnChoices.Broadcast(this, CurrentChoices);
			return;
		}
		case EDnaOpCode::Jump:
			PC = Ins.Index;
			continue;
		case EDnaOpCode::JumpIfFalse:
			PC = Condition(Ins.Expr) ? PC + 1 : Ins.Index;
			continue;
		case EDnaOpCode::Set:
		{
			++PC;
			const FDnaCompiledExpr* Compiled = Expr(Ins.Expr);
			if (!Compiled) { continue; }
			FDnaValue Value = Ledger->Evaluate(*Compiled);
			if (Ins.Assign != EDnaAssignOp::Set)
			{
				const FDnaValue Current = Ledger->GetFact(Ins.Name);
				const double Delta = Ins.Assign == EDnaAssignOp::Add ? Value.AsNumber() : -Value.AsNumber();
				Value = Current.Type == EDnaFactType::Float
					? FDnaValue::FromFloat(static_cast<float>(Current.AsNumber() + Delta))
					: FDnaValue::FromInt(static_cast<int32>(Current.AsNumber() + Delta));
			}
			Ledger->SetFact(Ins.Name, Value, EDnaWriteSource::Narrative);
			Transcript.Add(FString::Printf(TEXT("[set %s = %s]"), *Ins.Name.ToString(), *Ledger->GetFact(Ins.Name).ToString()));
			continue;
		}
		case EDnaOpCode::Accept:
		case EDnaOpCode::Complete:
		case EDnaOpCode::Fail:
			// Quests are not implemented yet; the command is recorded so a transcript shows it happened.
			++PC;
			Transcript.Add(FString::Printf(TEXT("[%s %s]"),
				Ins.Op == EDnaOpCode::Accept ? TEXT("accept") : Ins.Op == EDnaOpCode::Complete ? TEXT("complete") : TEXT("fail"), *Ins.Name.ToString()));
			continue;
		case EDnaOpCode::Advance:
			++PC;
			Ledger->SetChapter(Ins.Name);
			Transcript.Add(FString::Printf(TEXT("[advance %s]"), *Ins.Name.ToString()));
			continue;
		case EDnaOpCode::Play:
			++PC;
			Transcript.Add(FString::Printf(TEXT("[play %s%s]"), *Ins.Name.ToString(), Ins.bAsync ? TEXT(" async") : TEXT("")));
			if (OnPlayStage.IsBound() && !Ins.bAsync)
			{
				State = EDnaRunnerState::PlayingStage;
				OnPlayStage.Broadcast(this, Ins.Name, true);
				return;
			}
			OnPlayStage.Broadcast(this, Ins.Name, false);
			continue;
		case EDnaOpCode::Emit:
			++PC;
			Transcript.Add(FString::Printf(TEXT("[emit %s]"), *Ins.Name.ToString()));
			Ledger->EmitEvent(Ins.Name);
			continue;
		case EDnaOpCode::Wait:
		{
			++PC;
			Transcript.Add(FString::Printf(TEXT("[wait %gs]"), Ins.Seconds));
			State = EDnaRunnerState::Waiting;
			// Scheduled before the broadcast: a listener that ends the wait on the spot cancels it.
			if (!bManualWait)
			{
				WaitHandle = Ledger->GetClock().Schedule(Ins.Seconds, FSimpleDelegate::CreateUObject(this, &UDreamNarrativeDialogueRunner::NotifyWaitFinished));
			}
			OnWait.Broadcast(this, Ins.Seconds);
			return;
		}
		case EDnaOpCode::GoTo:
			// A visit counts once the node is left, so `visited(x)` inside x still sees the earlier visits only.
			Ledger->MarkVisited(NodeName);
			if (!EnterNode(Ins.Name))
			{
				Finish();
				return;
			}
			continue;
		case EDnaOpCode::End:
			Finish();
			return;
		}
	}
	Finish();
}

void UDreamNarrativeDialogueRunner::Advance()
{
	if (State != EDnaRunnerState::ShowingLine)
	{
		return;
	}
	State = EDnaRunnerState::Idle;
	Run();
}

bool UDreamNarrativeDialogueRunner::Choose(int32 Index)
{
	if (State != EDnaRunnerState::AwaitingChoice || !CurrentOptionIndices.IsValidIndex(Index) || !Node)
	{
		return false;
	}
	const FDnaChoiceGroupData& Group = Node->ChoiceGroups[CurrentGroup];
	const FDnaChoiceData& Option = Group.Options[CurrentOptionIndices[Index]];
	Transcript.Add(FString::Printf(TEXT("[choose %s]"), *CurrentChoices[Index].Label.ToString()));
	if (Option.bOnce)
	{
		Ledger->MarkChosenOnce(Option.Id);
	}
	CurrentChoices.Reset();
	CurrentOptionIndices.Reset();
	CurrentGroup = INDEX_NONE;
	PC = Option.Target;
	State = EDnaRunnerState::Idle;
	Run();
	return true;
}

void UDreamNarrativeDialogueRunner::NotifyStageFinished()
{
	if (State != EDnaRunnerState::PlayingStage)
	{
		return;
	}
	State = EDnaRunnerState::Idle;
	Run();
}

void UDreamNarrativeDialogueRunner::NotifyWaitFinished()
{
	if (State != EDnaRunnerState::Waiting)
	{
		return;
	}
	// Whoever ends the wait, nothing may be left on the clock to end the next one early.
	if (WaitHandle.IsValid() && Ledger)
	{
		Ledger->GetClock().Cancel(WaitHandle);
	}
	State = EDnaRunnerState::Idle;
	Run();
}

void UDreamNarrativeDialogueRunner::Abort()
{
	if (State != EDnaRunnerState::Ended)
	{
		Transcript.Add(TEXT("[abort]"));
		Finish();
	}
}

void UDreamNarrativeDialogueRunner::Finish()
{
	if (State == EDnaRunnerState::Ended)
	{
		return;
	}
	if (WaitHandle.IsValid() && Ledger)
	{
		Ledger->GetClock().Cancel(WaitHandle);
	}
	if (Ledger && !NodeName.IsNone())
	{
		Ledger->MarkVisited(NodeName);
	}
	State = EDnaRunnerState::Ended;
	Transcript.Add(TEXT("[end]"));
	OnEnded.Broadcast(this);
}
