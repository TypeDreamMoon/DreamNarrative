// Copyright DreamMoon. All Rights Reserved.

#include "DreamNarrativeDialogueRunner.h"
#include "DreamNarrativeLedger.h"
#include "DreamNarrativeSettings.h"
#include "DreamNarrativeStory.h"
#include "DreamNarrativeStoryModule.h"
#include "DreamNarrativeTrace.h"

namespace
{
	const TCHAR* OpEvent(EDnaOpCode Op)
	{
		switch (Op)
		{
		case EDnaOpCode::Line: return TEXT("dialogue.op.Line");
		case EDnaOpCode::Choice: return TEXT("dialogue.op.Choice");
		case EDnaOpCode::Jump: return TEXT("dialogue.op.Jump");
		case EDnaOpCode::JumpIfFalse: return TEXT("dialogue.op.JumpIfFalse");
		case EDnaOpCode::Set: return TEXT("dialogue.op.Set");
		case EDnaOpCode::Accept: return TEXT("dialogue.op.Accept");
		case EDnaOpCode::Complete: return TEXT("dialogue.op.Complete");
		case EDnaOpCode::Fail: return TEXT("dialogue.op.Fail");
		case EDnaOpCode::Advance: return TEXT("dialogue.op.Advance");
		case EDnaOpCode::Play: return TEXT("dialogue.op.Play");
		case EDnaOpCode::Emit: return TEXT("dialogue.op.Emit");
		case EDnaOpCode::Wait: return TEXT("dialogue.op.Wait");
		case EDnaOpCode::GoTo: return TEXT("dialogue.op.GoTo");
		case EDnaOpCode::End: return TEXT("dialogue.op.End");
		}
		return TEXT("dialogue.op.?");
	}

	/** `combo.<What>.<a>+<b>` for every pair of the modifiers present, in the order given. */
	void TraceCombos(IDnaTrace& Tracer, const TCHAR* What, const TArray<const TCHAR*>& Present)
	{
		for (int32 First = 0; First < Present.Num(); ++First)
		{
			for (int32 Second = First + 1; Second < Present.Num(); ++Second)
			{
				Tracer.OnEvent(*FString::Printf(TEXT("combo.%s.%s+%s"), What, Present[First], Present[Second]));
			}
		}
	}

	void TraceShownLine(IDnaTrace& Tracer, const FDnaLineData& Line)
	{
		Tracer.OnEvent(Line.Speaker.IsNone() ? TEXT("dialogue.line.narration") : TEXT("dialogue.line.speech"));
		TArray<const TCHAR*> Modifiers;
		if (Line.Condition != INDEX_NONE) { Tracer.OnEvent(TEXT("dialogue.line.if")); Modifiers.Add(TEXT("if")); }
		if (Line.Duration > 0.f) { Tracer.OnEvent(TEXT("dialogue.line.for")); Modifiers.Add(TEXT("for")); }
		if (!Line.Stage.IsNone()) { Tracer.OnEvent(TEXT("dialogue.line.stage")); Modifiers.Add(TEXT("stage")); }
		if (Line.bExplicitId) { Tracer.OnEvent(TEXT("dialogue.line.id")); Modifiers.Add(TEXT("id")); }
		if (Line.TextArgs.Num() > 0) { Tracer.OnEvent(TEXT("dialogue.line.interpolated")); }
		if (Line.Tags.Num() > 0) { Tracer.OnEvent(TEXT("dialogue.line.tags")); }
		if (Line.Text.ToString().Contains(TEXT("\n"))) { Tracer.OnEvent(TEXT("dialogue.line.continued")); }
		TraceCombos(Tracer, TEXT("line"), Modifiers);
	}

	void TraceShownOption(IDnaTrace& Tracer, const FDnaChoiceData& Option)
	{
		Tracer.OnEvent(TEXT("dialogue.choice.shown"));
		TArray<const TCHAR*> Modifiers;
		if (Option.Condition != INDEX_NONE) { Tracer.OnEvent(TEXT("dialogue.choice.if")); Modifiers.Add(TEXT("if")); }
		if (Option.bOnce) { Tracer.OnEvent(TEXT("dialogue.choice.once")); Modifiers.Add(TEXT("once")); }
		if (Option.bExplicitId) { Tracer.OnEvent(TEXT("dialogue.choice.id")); Modifiers.Add(TEXT("id")); }
		if (Option.LabelArgs.Num() > 0) { Tracer.OnEvent(TEXT("dialogue.choice.interpolated")); }
		TraceCombos(Tracer, TEXT("choice"), Modifiers);
	}
}

bool UDreamNarrativeDialogueRunner::Start(UDreamNarrativeStory* InStory, UDreamNarrativeLedger* InLedger, const UDnaDialogueSection* InSection, FName InNode)
{
	// A runner can be started again. A dialogue still running on it ends first, as an abort would end it.
	if (State != EDnaRunnerState::Idle && State != EDnaRunnerState::Ended)
	{
		Abort();
	}
	Story = InStory;
	Ledger = InLedger;
	Section = const_cast<UDnaDialogueSection*>(InSection);
	Node = nullptr;
	NodeName = NAME_None;
	PC = 0;
	State = EDnaRunnerState::Idle;
	CurrentLine = FDnaLineView();
	CurrentChoices.Reset();
	CurrentGroup = INDEX_NONE;
	CurrentOptionIndices.Reset();
	WaitHandle = FDnaClockHandle();
	AdvanceHandle = FDnaClockHandle();
	bPaused = false;
	bShown = false;
	LinesShown = 0;
	PausedWaitRemaining = -1.0;
	if (Channel.IsNone())
	{
		Channel = ::DreamNarrative::MainChannelName();
	}
	Transcript.Reset();
	if (!Ledger || !Section || !EnterNode(InNode))
	{
		State = EDnaRunnerState::Ended;
		return false;
	}
	Run();
	return true;
}

void UDreamNarrativeDialogueRunner::SetChannel(FName InChannel, bool bInAutoAdvance)
{
	Channel = InChannel.IsNone() ? ::DreamNarrative::MainChannelName() : InChannel;
	bAutoAdvance = bInAutoAdvance;
}

void UDreamNarrativeDialogueRunner::SetEventScope(const FDnaEventPayload& InEvent)
{
	Event = InEvent;
	bHasEvent = true;
}

void UDreamNarrativeDialogueRunner::ClearEventScope()
{
	Event = FDnaEventPayload();
	bHasEvent = false;
}

float UDreamNarrativeDialogueRunner::LineSeconds(const FDnaLineView& Line)
{
	if (Line.Duration > 0.f)
	{
		return Line.Duration;
	}
	const UDreamNarrativeSettings* Settings = UDreamNarrativeSettings::Get();
	return FMath::Max(Settings->AutoAdvanceMinimumSeconds, Settings->AutoAdvanceSecondsPerCharacter * Line.Text.ToString().Len());
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

FDnaValue UDreamNarrativeDialogueRunner::Evaluate(const FDnaCompiledExpr& Compiled) const
{
	if (!Ledger)
	{
		return FDnaValue();
	}
	FDnaEventScopeContext Context(*Ledger, bHasEvent ? &Event : nullptr);
	return ::DreamNarrative::EvaluateExpr(Compiled, Context);
}

bool UDreamNarrativeDialogueRunner::Condition(int32 Index) const
{
	if (Index == INDEX_NONE)
	{
		return true;
	}
	const FDnaCompiledExpr* Compiled = Expr(Index);
	return Compiled && Compiled->IsValid() && Evaluate(*Compiled).AsBool();
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
		const FDnaValue Value = Compiled ? Evaluate(*Compiled) : FDnaValue();
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
		IDnaTrace* Tracer = Ledger->GetTrace();
		if (--StepBudget <= 0)
		{
			UE_LOG(LogDreamNarrativeStory, Error, TEXT("Dialogue '%s' ran %d steps without yielding; aborting"), *NodeName.ToString(), 10000);
			if (Tracer) { Tracer->OnEvent(TEXT("dialogue.runaway")); }
			Finish();
			return;
		}

		const FDnaInstruction& Ins = Node->Code[PC];
		if (Ins.Op != EDnaOpCode::Set)
		{
			CloseBatch();
			// Committing tells observers; one of them may have ended this dialogue.
			if (State == EDnaRunnerState::Ended)
			{
				return;
			}
		}
		if (Tracer)
		{
			Tracer->OnEvent(OpEvent(Ins.Op));
		}
		switch (Ins.Op)
		{
		case EDnaOpCode::Line:
		{
			const FDnaLineData& Line = Node->Lines[Ins.Index];
			++PC;
			if (!Condition(Line.Condition))
			{
				if (Tracer) { Tracer->OnEvent(TEXT("dialogue.line.hidden")); }
				continue;
			}
			if (Tracer)
			{
				TraceShownLine(*Tracer, Line);
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
			CurrentLine.Channel = Channel;
			Transcript.Add(Line.Speaker.IsNone()
				? FString::Printf(TEXT("> %s"), *CurrentLine.Text.ToString())
				: FString::Printf(TEXT("%s: %s"), *Line.Speaker.ToString(), *CurrentLine.Text.ToString()));
			State = EDnaRunnerState::ShowingLine;
			bShown = false;
			ShowLine();
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
					if (Tracer) { Tracer->OnEvent(TEXT("dialogue.choice.once-hidden")); }
					continue;
				}
				if (!Condition(Option.Condition))
				{
					if (Tracer) { Tracer->OnEvent(TEXT("dialogue.choice.if-hidden")); }
					continue;
				}
				if (Tracer)
				{
					TraceShownOption(*Tracer, Option);
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
				if (Tracer) { Tracer->OnEvent(TEXT("dialogue.choice.all-hidden")); }
				PC = Group.Gather;
				continue;
			}
			CurrentGroup = Ins.Index;
			for (const FDnaChoiceView& View : CurrentChoices)
			{
				Transcript.Add(FString::Printf(TEXT("  - %s"), *View.Label.ToString()));
			}
			State = EDnaRunnerState::AwaitingChoice;
			bShown = !bPaused;
			if (bShown)
			{
				OnChoices.Broadcast(this, CurrentChoices);
			}
			return;
		}
		case EDnaOpCode::Jump:
			PC = Ins.Index;
			continue;
		case EDnaOpCode::JumpIfFalse:
		{
			const bool bEnter = Condition(Ins.Expr);
			if (Tracer) { Tracer->OnEvent(bEnter ? TEXT("dialogue.branch.enter") : TEXT("dialogue.branch.skip")); }
			PC = bEnter ? PC + 1 : Ins.Index;
			continue;
		}
		case EDnaOpCode::Set:
		{
			++PC;
			const FDnaCompiledExpr* Compiled = Expr(Ins.Expr);
			if (!Compiled) { continue; }
			if (!bBatchOpen)
			{
				Ledger->BeginBatch();
				bBatchOpen = true;
			}
			if (Tracer)
			{
				Tracer->OnEvent(Ins.Assign == EDnaAssignOp::Set ? TEXT("dialogue.set.assign") : Ins.Assign == EDnaAssignOp::Add ? TEXT("dialogue.set.add") : TEXT("dialogue.set.sub"));
			}
			FDnaValue Value = Evaluate(*Compiled);
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
			++PC;
			Transcript.Add(FString::Printf(TEXT("[%s %s]"),
				Ins.Op == EDnaOpCode::Accept ? TEXT("accept") : Ins.Op == EDnaOpCode::Complete ? TEXT("complete") : TEXT("fail"), *Ins.Name.ToString()));
			if (Story)
			{
				Story->RequestQuest(Ins.Op == EDnaOpCode::Accept ? EDnaQuestRequest::Accept : Ins.Op == EDnaOpCode::Complete ? EDnaQuestRequest::Complete : EDnaQuestRequest::Fail,
					Ins.Name, EDnaWriteSource::Narrative);
				// A quest's own blocks may have ended this dialogue.
				if (State == EDnaRunnerState::Ended)
				{
					return;
				}
			}
			continue;
		case EDnaOpCode::Advance:
			++PC;
			if (Tracer) { Tracer->OnEvent(TEXT("chapter.advance-command")); }
			Ledger->SetChapter(Ins.Name);
			Transcript.Add(FString::Printf(TEXT("[advance %s]"), *Ins.Name.ToString()));
			continue;
		case EDnaOpCode::Play:
			++PC;
			Transcript.Add(FString::Printf(TEXT("[play %s%s]"), *Ins.Name.ToString(), Ins.bAsync ? TEXT(" async") : TEXT("")));
			if (OnPlayStage.IsBound() && !Ins.bAsync)
			{
				if (Tracer) { Tracer->OnEvent(TEXT("dialogue.play.blocking")); }
				State = EDnaRunnerState::PlayingStage;
				OnPlayStage.Broadcast(this, Ins.Name, true);
				return;
			}
			if (Tracer) { Tracer->OnEvent(Ins.bAsync ? TEXT("dialogue.play.async") : TEXT("dialogue.play.unheard")); }
			OnPlayStage.Broadcast(this, Ins.Name, false);
			continue;
		case EDnaOpCode::Emit:
		{
			++PC;
			FDnaEventPayload Raised;
			Raised.Event = Ins.Name;
			for (int32 Arg = 0; Arg < Ins.ArgNames.Num() && Arg < Ins.ArgExprs.Num(); ++Arg)
			{
				const FDnaCompiledExpr* Compiled = Expr(Ins.ArgExprs[Arg]);
				Raised.Args.Add({ Ins.ArgNames[Arg], Compiled ? Evaluate(*Compiled) : FDnaValue() });
			}
			if (Tracer && Raised.Args.Num() > 0) { Tracer->OnEvent(TEXT("dialogue.emit.args")); }
			Transcript.Add(FString::Printf(TEXT("[emit %s]"), *Raised.ToString()));
			Ledger->RaiseEventAs(Raised, EDnaWriteSource::Narrative);
			// What the event opened may have ended this dialogue.
			if (State == EDnaRunnerState::Ended)
			{
				return;
			}
			continue;
		}
		case EDnaOpCode::Wait:
		{
			++PC;
			Transcript.Add(FString::Printf(TEXT("[wait %gs]"), Ins.Seconds));
			State = EDnaRunnerState::Waiting;
			// Scheduled before the broadcast: a listener that ends the wait on the spot cancels it.
			if (Tracer) { Tracer->OnEvent(bManualWait ? TEXT("dialogue.wait.manual") : TEXT("dialogue.wait.clock")); }
			if (!bManualWait)
			{
				if (bPaused)
				{
					PausedWaitRemaining = Ins.Seconds;
				}
				else
				{
					WaitHandle = Ledger->GetClock().Schedule(Ins.Seconds, FSimpleDelegate::CreateUObject(this, &UDreamNarrativeDialogueRunner::NotifyWaitFinished));
				}
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
	if (State != EDnaRunnerState::ShowingLine || bPaused)
	{
		return;
	}
	CancelAutoAdvance();
	State = EDnaRunnerState::Idle;
	Run();
}

void UDreamNarrativeDialogueRunner::ShowLine()
{
	if (bPaused)
	{
		return;
	}
	CurrentLine.bRepeat = bShown;
	bShown = true;
	++LinesShown;
	// Scheduled before the broadcast, as a wait is: a listener that moves on at once cancels it.
	ScheduleAutoAdvance();
	OnLine.Broadcast(this, CurrentLine);
}

void UDreamNarrativeDialogueRunner::ScheduleAutoAdvance()
{
	CancelAutoAdvance();
	if (bAutoAdvance && !bPaused && Ledger && State == EDnaRunnerState::ShowingLine)
	{
		AdvanceHandle = Ledger->GetClock().Schedule(LineSeconds(CurrentLine), FSimpleDelegate::CreateUObject(this, &UDreamNarrativeDialogueRunner::HandleAutoAdvance));
	}
}

void UDreamNarrativeDialogueRunner::CancelAutoAdvance()
{
	if (AdvanceHandle.IsValid() && Ledger)
	{
		Ledger->GetClock().Cancel(AdvanceHandle);
	}
	AdvanceHandle.Invalidate();
}

void UDreamNarrativeDialogueRunner::HandleAutoAdvance()
{
	AdvanceHandle.Invalidate();
	if (IDnaTrace* Tracer = Ledger ? Ledger->GetTrace() : nullptr)
	{
		Tracer->OnEvent(TEXT("channel.auto-advance"));
	}
	Advance();
}

void UDreamNarrativeDialogueRunner::Pause()
{
	if (bPaused || State == EDnaRunnerState::Ended)
	{
		return;
	}
	bPaused = true;
	CancelAutoAdvance();
	if (State == EDnaRunnerState::Waiting && WaitHandle.IsValid() && Ledger)
	{
		PausedWaitRemaining = FMath::Max(0.0, Ledger->GetClock().GetRemaining(WaitHandle));
		Ledger->GetClock().Cancel(WaitHandle);
	}
	Transcript.Add(TEXT("[pause]"));
	OnPaused.Broadcast(this);
}

void UDreamNarrativeDialogueRunner::Resume()
{
	if (!bPaused || State == EDnaRunnerState::Ended)
	{
		return;
	}
	bPaused = false;
	Transcript.Add(TEXT("[resume]"));
	OnResumed.Broadcast(this);
	switch (State)
	{
	case EDnaRunnerState::ShowingLine:
		ShowLine();
		break;
	case EDnaRunnerState::AwaitingChoice:
		bShown = true;
		OnChoices.Broadcast(this, CurrentChoices);
		break;
	case EDnaRunnerState::Waiting:
		if (!bManualWait && PausedWaitRemaining >= 0.0 && Ledger)
		{
			WaitHandle = Ledger->GetClock().Schedule(PausedWaitRemaining, FSimpleDelegate::CreateUObject(this, &UDreamNarrativeDialogueRunner::NotifyWaitFinished));
		}
		PausedWaitRemaining = -1.0;
		break;
	default:
		break;
	}
}

bool UDreamNarrativeDialogueRunner::Choose(int32 Index)
{
	if (State != EDnaRunnerState::AwaitingChoice || bPaused || !CurrentOptionIndices.IsValidIndex(Index) || !Node)
	{
		return false;
	}
	const FDnaChoiceGroupData& Group = Node->ChoiceGroups[CurrentGroup];
	const FDnaChoiceData& Option = Group.Options[CurrentOptionIndices[Index]];
	Transcript.Add(FString::Printf(TEXT("[choose %s]"), *CurrentChoices[Index].Label.ToString()));
	OnChoiceMade.Broadcast(this, CurrentChoices[Index]);
	if (IDnaTrace* Tracer = Ledger ? Ledger->GetTrace() : nullptr)
	{
		Tracer->OnEvent(Option.bOnce ? TEXT("dialogue.choice.once-taken") : TEXT("dialogue.choice.taken"));
	}
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
	if (State != EDnaRunnerState::Waiting || bPaused)
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
		if (IDnaTrace* Tracer = Ledger ? Ledger->GetTrace() : nullptr)
		{
			Tracer->OnEvent(TEXT("dialogue.abort"));
		}
		Finish();
	}
}

void UDreamNarrativeDialogueRunner::Finish()
{
	if (State == EDnaRunnerState::Ended)
	{
		return;
	}
	CloseBatch();
	if (WaitHandle.IsValid() && Ledger)
	{
		Ledger->GetClock().Cancel(WaitHandle);
	}
	CancelAutoAdvance();
	bPaused = false;
	if (Ledger && !NodeName.IsNone())
	{
		Ledger->MarkVisited(NodeName);
	}
	State = EDnaRunnerState::Ended;
	Transcript.Add(TEXT("[end]"));
	OnEnded.Broadcast(this);
}

void UDreamNarrativeDialogueRunner::CloseBatch()
{
	if (bBatchOpen)
	{
		bBatchOpen = false;
		if (Ledger)
		{
			Ledger->EndBatch();
		}
	}
}
