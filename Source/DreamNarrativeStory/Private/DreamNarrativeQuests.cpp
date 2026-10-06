// Copyright DreamMoon. All Rights Reserved.
//
// The quest half of the Story subsystem. A quest's state and done objectives are ledger facts; every
// change goes through the ledger, so it commits in a batch, observers hear of it, and a save has it.

#include "DreamNarrativeQuest.h"
#include "DreamNarrativeStory.h"
#include "DreamNarrativeStoryModule.h"
#include "DreamNarrativeTrace.h"

namespace
{
	bool IsActive(EDnaQuestState State)
	{
		return State == EDnaQuestState::Accepted || State == EDnaQuestState::Going;
	}

	bool IsTakeable(EDnaQuestState State)
	{
		return State == EDnaQuestState::Inactive || State == EDnaQuestState::Failed || State == EDnaQuestState::TimedOut;
	}

	const TCHAR* StateName(EDnaQuestState State)
	{
		switch (State)
		{
		case EDnaQuestState::Inactive: return TEXT("inactive");
		case EDnaQuestState::Accepted: return TEXT("accepted");
		case EDnaQuestState::Going: return TEXT("going");
		case EDnaQuestState::Completed: return TEXT("completed");
		case EDnaQuestState::Failed: return TEXT("failed");
		case EDnaQuestState::TimedOut: return TEXT("timed out");
		}
		return TEXT("?");
	}

	bool SameValues(const TArray<FDnaValue>& A, const TArray<FDnaValue>& B)
	{
		if (A.Num() != B.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < A.Num(); ++Index)
		{
			if (!(A[Index].Type == B[Index].Type && A[Index].Equals(B[Index])))
			{
				return false;
			}
		}
		return true;
	}

	/** Every fact an expression reads. */
	void CollectFacts(const FDnaCompiledExpr* Expr, TArray<FName>& Out)
	{
		if (!Expr)
		{
			return;
		}
		for (const FDnaExprNode& Node : Expr->Nodes)
		{
			if (Node.Kind == EDnaExprNodeKind::Fact)
			{
				Out.AddUnique(Node.Name);
			}
		}
	}

	/** Every fact a quest's objectives read, in their conditions and in their `show:` texts. */
	void CollectObjectiveFacts(const FDnaQuestData& Quest, const UDreamNarrativeScript& Script, TArray<FName>& Out)
	{
		for (const FDnaObjectiveData& Objective : Quest.Objectives)
		{
			CollectFacts(Script.GetExpression(Objective.Condition), Out);
			for (const int32 Arg : Objective.Show.Args) { CollectFacts(Script.GetExpression(Arg), Out); }
		}
	}

	/** Whether a batch that changed these facts moves what the player sees of a quest: its objectives or its journal. */
	bool Touches(const FDnaQuestData& Quest, const UDreamNarrativeScript& Script, TConstArrayView<FName> Changed)
	{
		if (Changed.Num() == 0)
		{
			return false;
		}
		TArray<FName> Facts;
		CollectObjectiveFacts(Quest, Script, Facts);
		for (const FDnaQuestText* Text : { &Quest.JournalAccepted, &Quest.JournalGoing, &Quest.JournalCompleted, &Quest.JournalFailed })
		{
			for (const int32 Arg : Text->Args) { CollectFacts(Script.GetExpression(Arg), Facts); }
		}
		for (const FName& Fact : Facts)
		{
			if (Changed.Contains(Fact))
			{
				return true;
			}
		}
		return false;
	}
}

// ---------------------------------------------------------------- the section

const FDnaQuestData* UDnaQuestSection::FindQuest(FName Name) const
{
	return Quests.FindByPredicate([Name](const FDnaQuestData& Quest) { return Quest.Name == Name; });
}

const FDnaQuestData* UDreamNarrativeStory::FindQuest(FName Quest, const UDnaQuestSection** OutSection) const
{
	const FQuestRef* Ref = QuestIndex.Find(Quest);
	if (!Ref || !Ref->Section.IsValid() || !Ref->Section->Quests.IsValidIndex(Ref->QuestIndex))
	{
		return nullptr;
	}
	if (OutSection)
	{
		*OutSection = Ref->Section.Get();
	}
	return &Ref->Section->Quests[Ref->QuestIndex];
}

// ---------------------------------------------------------------- watching the ledger

void UDreamNarrativeStory::WatchLedger()
{
	UnwatchLedger();
	CommitHandle = Ledger->OnBatchCommitted.AddUObject(this, &UDreamNarrativeStory::HandleBatchCommitted);
	TimerHandle = Ledger->OnTimerFired.AddUObject(this, &UDreamNarrativeStory::HandleTimerFired);
	SnapshotHandle = Ledger->OnSnapshotApplied.AddUObject(this, &UDreamNarrativeStory::HandleSnapshotApplied);
	ApplyingHandle = Ledger->OnSnapshotApplying.AddUObject(this, &UDreamNarrativeStory::HandleSnapshotApplying);
	EventHandle = Ledger->OnEventRaised.AddUObject(this, &UDreamNarrativeStory::HandleEventRaised);
	PlaythroughHandle = Ledger->OnNewPlaythrough.AddUObject(this, &UDreamNarrativeStory::HandleNewPlaythrough);
}

void UDreamNarrativeStory::UnwatchLedger()
{
	if (Ledger)
	{
		Ledger->OnBatchCommitted.Remove(CommitHandle);
		Ledger->OnTimerFired.Remove(TimerHandle);
		Ledger->OnSnapshotApplied.Remove(SnapshotHandle);
		Ledger->OnSnapshotApplying.Remove(ApplyingHandle);
		Ledger->OnEventRaised.Remove(EventHandle);
		Ledger->OnNewPlaythrough.Remove(PlaythroughHandle);
	}
	CommitHandle.Reset();
	TimerHandle.Reset();
	SnapshotHandle.Reset();
	ApplyingHandle.Reset();
	EventHandle.Reset();
	PlaythroughHandle.Reset();
}

void UDreamNarrativeStory::HandleSnapshotApplying()
{
	// Dialogue positions are not saved: a load ends every dialogue, and cooldowns start over.
	StopAllDialogue();
	LastStarted.Reset();
}

void UDreamNarrativeStory::HandleBatchCommitted(const TArray<FName>& Changed)
{
	// A load puts a story back; nothing in it progresses on the way.
	if (Ledger->IsApplyingSnapshot())
	{
		return;
	}
	CheckQuests(Changed);
	CheckArchives();
}

void UDreamNarrativeStory::HandleNewPlaythrough()
{
	CheckQuests();
	CheckArchives();
}

void UDreamNarrativeStory::CheckQuests(TConstArrayView<FName> Changed)
{
	// What a quest writes while it is checked lands in a batch of its own, which the ledger commits
	// right after this one: the quests are checked again then.
	for (const FName& Name : QuestOrder)
	{
		const UDnaQuestSection* Section = nullptr;
		const FDnaQuestData* Quest = FindQuest(Name, &Section);
		if (Quest && Section && Section->GetScript())
		{
			EvaluateQuest(*Quest, *Section->GetScript(), Changed);
		}
	}
}

void UDreamNarrativeStory::HandleSnapshotApplied()
{
	// What a quest read before the load says nothing about the loaded story: a quest found Accepted
	// counts its progress from what it reads now.
	Baselines.Reset();
	for (const FName& Name : QuestOrder)
	{
		const UDnaQuestSection* Section = nullptr;
		const FDnaQuestData* Quest = FindQuest(Name, &Section);
		if (Quest && Section && Section->GetScript() && GetQuestStateOf(Name) == EDnaQuestState::Accepted)
		{
			Baselines.Add(Name, ReadValues(*Quest, *Section->GetScript()));
		}
	}
}

void UDreamNarrativeStory::HandleTimerFired(FName Timer)
{
	for (const FName& Name : QuestOrder)
	{
		if (::DreamNarrative::QuestStateFact(Name) == Timer && IsActive(GetQuestStateOf(Name)))
		{
			if (IDnaTrace* Tracer = Ledger->GetTrace()) { Tracer->OnEvent(TEXT("quest.timeout")); }
			Finish(Name, EDnaQuestState::TimedOut);
			return;
		}
	}
}

void UDreamNarrativeStory::EvaluateQuest(const FDnaQuestData& Quest, const UDreamNarrativeScript& Script, TConstArrayView<FName> Changed)
{
	const EDnaQuestState State = GetQuestStateOf(Quest.Name);
	IDnaTrace* Tracer = Ledger->GetTrace();

	// Nobody hands out a hidden quest (a memory chain, say): it starts itself the first time it can be
	// taken on. Only the first time -- one that failed stays failed until something takes it on again.
	if (State == EDnaQuestState::Inactive && Quest.Priority == EDnaQuestPriority::Hidden && IsQuestAvailable(Quest.Name))
	{
		if (Tracer) { Tracer->OnEvent(TEXT("quest.start-hidden")); }
		AcceptNow(Quest.Name);
		return;
	}
	if (!IsActive(State))
	{
		Baselines.Remove(Quest.Name);
		return;
	}
	bool bUpdated = false;
	auto Update = [this, &Quest, &bUpdated, Tracer]()
	{
		if (Tracer) { Tracer->OnEvent(TEXT("quest.update")); }
		OnQuestUpdated.Broadcast(Quest.Name);
		bUpdated = true;
	};

	// Accepted until something it watches moves; a quest found Accepted after a load starts from what it sees.
	if (State == EDnaQuestState::Accepted)
	{
		const TArray<FDnaValue> Now = ReadValues(Quest, Script);
		const TArray<FDnaValue>* Then = Baselines.Find(Quest.Name);
		if (!Then)
		{
			Baselines.Add(Quest.Name, Now);
		}
		else if (!SameValues(*Then, Now))
		{
			Baselines.Remove(Quest.Name);
			if (Tracer) { Tracer->OnEvent(TEXT("quest.going")); }
			SetQuestState(Quest.Name, EDnaQuestState::Going);
			Update();
		}
	}

	// A failure condition wins over anything completed in the same batch.
	if (Quest.FailIf != INDEX_NONE && Holds(Quest.FailIf, Script))
	{
		if (Tracer) { Tracer->OnEvent(TEXT("quest.fail-if")); }
		Finish(Quest.Name, EDnaQuestState::Failed);
		return;
	}

	// Objectives latch: done once, done for good.
	const FName DoneFact = ::DreamNarrative::QuestObjectivesFact(Quest.Name);
	const int32 Before = Ledger->GetFactAsInt(DoneFact);
	int32 Done = Before;
	for (int32 Index = 0; Index < Quest.Objectives.Num() && Index < 31; ++Index)
	{
		const int32 Bit = 1 << Index;
		if (!(Done & Bit) && Holds(Quest.Objectives[Index].Condition, Script))
		{
			Done |= Bit;
			if (Tracer) { Tracer->OnEvent(Quest.Objectives[Index].bOptional ? TEXT("quest.objective.optional") : TEXT("quest.objective")); }
		}
	}
	if (Done != Before)
	{
		Ledger->SetFact(DoneFact, FDnaValue::FromInt(Done), EDnaWriteSource::Narrative);
		Update();
	}
	// Completes on its objectives only when it has required ones; one without is completed by a command.
	int32 Required = 0;
	int32 RequiredDone = 0;
	for (int32 Index = 0; Index < Quest.Objectives.Num() && Index < 31; ++Index)
	{
		if (!Quest.Objectives[Index].bOptional)
		{
			++Required;
			RequiredDone += (Done & (1 << Index)) != 0 ? 1 : 0;
		}
	}
	if (Required > 0 && RequiredDone == Required)
	{
		Finish(Quest.Name, EDnaQuestState::Completed);
		return;
	}
	// Nothing got done, but what the player reads moved: the count in `猎狼 {狼杀了}/5`, say.
	if (!bUpdated && Touches(Quest, Script, Changed))
	{
		Update();
	}
}

TArray<FDnaValue> UDreamNarrativeStory::ReadValues(const FDnaQuestData& Quest, const UDreamNarrativeScript& Script) const
{
	TArray<FName> Facts;
	CollectObjectiveFacts(Quest, Script, Facts);
	TArray<FDnaValue> Values;
	for (const FName& Fact : Facts)
	{
		Values.Add(Ledger->GetFact(Fact));
	}
	return Values;
}

// ---------------------------------------------------------------- requests

bool UDreamNarrativeStory::RequestQuest(EDnaQuestRequest What, FName Quest, EDnaWriteSource Who)
{
	const FDnaQuestData* Data = FindQuest(Quest);
	if (!Data || !Ledger)
	{
		UE_LOG(LogDreamNarrativeStory, Error, TEXT("There is no quest called '%s'"), *Quest.ToString());
		return false;
	}
	OnQuestRequested.Broadcast(What, Quest, Who);
	IDnaTrace* Tracer = Ledger->GetTrace();
	const EDnaQuestState State = GetQuestStateOf(Quest);
	if (What == EDnaQuestRequest::Accept)
	{
		if (!IsQuestAvailable(Quest))
		{
			if (Tracer) { Tracer->OnEvent(TEXT("quest.accept-refused")); }
			UE_LOG(LogDreamNarrativeStory, Warning, TEXT("Quest '%s' cannot be accepted now: it is %s%s"), *Quest.ToString(), StateName(State),
				IsTakeable(State) ? TEXT(" and its available-if does not hold") : TEXT(""));
			return false;
		}
		return AcceptNow(Quest);
	}
	if (!IsActive(State))
	{
		if (Tracer) { Tracer->OnEvent(TEXT("quest.finish-refused")); }
		UE_LOG(LogDreamNarrativeStory, Warning, TEXT("Quest '%s' cannot be %s: it is %s, not active"), *Quest.ToString(),
			What == EDnaQuestRequest::Complete ? TEXT("completed") : TEXT("failed"), StateName(State));
		return false;
	}
	if (Tracer) { Tracer->OnEvent(What == EDnaQuestRequest::Complete ? TEXT("quest.complete-command") : TEXT("quest.fail-command")); }
	Finish(Quest, What == EDnaQuestRequest::Complete ? EDnaQuestState::Completed : EDnaQuestState::Failed);
	return true;
}

bool UDreamNarrativeStory::AcceptNow(FName Quest)
{
	const UDnaQuestSection* Section = nullptr;
	const FDnaQuestData* Data = FindQuest(Quest, &Section);
	const UDreamNarrativeScript* Script = Section ? Section->GetScript() : nullptr;
	if (!Data || !Script)
	{
		return false;
	}
	if (IDnaTrace* Tracer = Ledger->GetTrace())
	{
		Tracer->OnEvent(GetQuestStateOf(Quest) == EDnaQuestState::Inactive ? TEXT("quest.accept") : TEXT("quest.accept-again"));
	}
	{
		// One batch: observers see the quest taken on with whatever its `on accept` wrote, all at once.
		UDreamNarrativeLedger::FBatchScope Batch(*Ledger);
		Baselines.Remove(Quest);
		Ledger->SetFact(::DreamNarrative::QuestObjectivesFact(Quest), FDnaValue::FromInt(0), EDnaWriteSource::Narrative);
		SetQuestState(Quest, EDnaQuestState::Accepted);
		RunHandler(Quest, Data->OnAccept, *Script);
		if (Data->Timeout > 0.f)
		{
			Ledger->StartTimer(::DreamNarrative::QuestStateFact(Quest), Data->Timeout);
		}
	}
	return true;
}

void UDreamNarrativeStory::Finish(FName Quest, EDnaQuestState State)
{
	const UDnaQuestSection* Section = nullptr;
	const FDnaQuestData* Data = FindQuest(Quest, &Section);
	const UDreamNarrativeScript* Script = Section ? Section->GetScript() : nullptr;
	if (!Data || !Script)
	{
		return;
	}
	IDnaTrace* Tracer = Ledger->GetTrace();
	{
		UDreamNarrativeLedger::FBatchScope Batch(*Ledger);
		Baselines.Remove(Quest);
		Ledger->CancelTimer(::DreamNarrative::QuestStateFact(Quest));
		SetQuestState(Quest, State);
		switch (State)
		{
		case EDnaQuestState::Completed:
			if (Tracer) { Tracer->OnEvent(TEXT("quest.complete")); }
			RunHandler(Quest, Data->OnComplete, *Script);
			break;
		case EDnaQuestState::TimedOut:
			RunHandler(Quest, Data->OnTimeout, *Script);
			break;
		default:
			if (Tracer) { Tracer->OnEvent(TEXT("quest.fail")); }
			RunHandler(Quest, Data->OnFail, *Script);
			break;
		}
	}
	if (State == EDnaQuestState::Completed)
	{
		for (const FDnaSubquestData& Subquest : Data->Subquests)
		{
			if (Subquest.bAuto)
			{
				if (Tracer) { Tracer->OnEvent(TEXT("quest.subquest-auto")); }
				RequestQuest(EDnaQuestRequest::Accept, Subquest.Quest, EDnaWriteSource::Narrative);
			}
		}
	}
}

void UDreamNarrativeStory::SetQuestState(FName Quest, EDnaQuestState State)
{
	const EDnaQuestState Old = GetQuestStateOf(Quest);
	Ledger->SetFact(::DreamNarrative::QuestStateFact(Quest), FDnaValue::FromTag(::DreamNarrative::QuestStateMember(State)), EDnaWriteSource::Narrative);
	if (Old != State)
	{
		OnQuestStateChanged.Broadcast(Quest, Old, State);
	}
}

void UDreamNarrativeStory::RunHandler(FName Quest, const FDnaQuestHandlerData& Handler, const UDreamNarrativeScript& Script)
{
	IDnaTrace* Tracer = Ledger->GetTrace();
	if (Tracer && !Handler.IsEmpty()) { Tracer->OnEvent(TEXT("quest.handler")); }
	for (const FDnaInstruction& Ins : Handler.Code)
	{
		switch (Ins.Op)
		{
		case EDnaOpCode::Set:
		{
			const FDnaCompiledExpr* Expr = Script.GetExpression(Ins.Expr);
			if (!Expr) { break; }
			FDnaValue Value = Ledger->Evaluate(*Expr);
			if (Ins.Assign != EDnaAssignOp::Set)
			{
				const FDnaValue Current = Ledger->GetFact(Ins.Name);
				const double Delta = Ins.Assign == EDnaAssignOp::Add ? Value.AsNumber() : -Value.AsNumber();
				Value = Current.Type == EDnaFactType::Float
					? FDnaValue::FromFloat(static_cast<float>(Current.AsNumber() + Delta))
					: FDnaValue::FromInt(static_cast<int32>(Current.AsNumber() + Delta));
			}
			Ledger->SetFact(Ins.Name, Value, EDnaWriteSource::Narrative);
			break;
		}
		case EDnaOpCode::Accept:
			RequestQuest(EDnaQuestRequest::Accept, Ins.Name, EDnaWriteSource::Narrative);
			break;
		case EDnaOpCode::Complete:
			RequestQuest(EDnaQuestRequest::Complete, Ins.Name, EDnaWriteSource::Narrative);
			break;
		case EDnaOpCode::Fail:
			RequestQuest(EDnaQuestRequest::Fail, Ins.Name, EDnaWriteSource::Narrative);
			break;
		case EDnaOpCode::Advance:
			Ledger->SetChapter(Ins.Name);
			break;
		case EDnaOpCode::Play:
			OnQuestStage.Broadcast(Quest, Ins.Name);
			break;
		case EDnaOpCode::Emit:
		{
			FDnaEventPayload Raised;
			Raised.Event = Ins.Name;
			for (int32 Arg = 0; Arg < Ins.ArgNames.Num() && Arg < Ins.ArgExprs.Num(); ++Arg)
			{
				const FDnaCompiledExpr* Value = Script.GetExpression(Ins.ArgExprs[Arg]);
				Raised.Args.Add({ Ins.ArgNames[Arg], Value ? Ledger->Evaluate(*Value) : FDnaValue() });
			}
			Ledger->RaiseEventAs(Raised, EDnaWriteSource::Narrative);
			break;
		}
		default:
			break;
		}
	}
	if (Handler.Rewards.Num() > 0)
	{
		if (Tracer) { Tracer->OnEvent(TEXT("quest.reward")); }
		OnQuestRewards.Broadcast(Quest, Handler.Rewards);
	}
}

// ---------------------------------------------------------------- looking

EDnaQuestState UDreamNarrativeStory::GetQuestStateOf(FName Quest) const
{
	EDnaQuestState State = EDnaQuestState::Inactive;
	if (Ledger)
	{
		::DreamNarrative::TryParseQuestState(Ledger->GetFact(::DreamNarrative::QuestStateFact(Quest)).Tag, State);
	}
	return State;
}

bool UDreamNarrativeStory::IsQuestAvailable(FName Quest) const
{
	const UDnaQuestSection* Section = nullptr;
	const FDnaQuestData* Data = FindQuest(Quest, &Section);
	if (!Data || !Section || !Section->GetScript() || !IsTakeable(GetQuestStateOf(Quest)))
	{
		return false;
	}
	return Data->Available == INDEX_NONE || Holds(Data->Available, *Section->GetScript());
}

bool UDreamNarrativeStory::GetQuestState(FName Quest, EDnaQuestStateQuery Query) const
{
	const EDnaQuestState State = GetQuestStateOf(Quest);
	switch (Query)
	{
	case EDnaQuestStateQuery::Available: return IsQuestAvailable(Quest);
	case EDnaQuestStateQuery::Active: return IsActive(State);
	case EDnaQuestStateQuery::Completed: return State == EDnaQuestState::Completed;
	case EDnaQuestStateQuery::Failed: return State == EDnaQuestState::Failed || State == EDnaQuestState::TimedOut;
	}
	return false;
}

bool UDreamNarrativeStory::Holds(int32 Expr, const UDreamNarrativeScript& Script) const
{
	const FDnaCompiledExpr* Compiled = Script.GetExpression(Expr);
	return Compiled && Ledger && Ledger->EvaluateCondition(*Compiled);
}

FText UDreamNarrativeStory::Format(const FDnaQuestText& Text, const UDreamNarrativeScript& Script) const
{
	if (Text.Args.Num() == 0 || !Ledger)
	{
		return Text.Text;
	}
	FFormatOrderedArguments Ordered;
	for (const int32 Index : Text.Args)
	{
		const FDnaCompiledExpr* Compiled = Script.GetExpression(Index);
		const FDnaValue Value = Compiled ? Ledger->Evaluate(*Compiled) : FDnaValue();
		switch (Value.Type)
		{
		case EDnaFactType::Int: Ordered.Add(FText::AsNumber(Value.Int)); break;
		case EDnaFactType::Float: Ordered.Add(FText::AsNumber(Value.Float)); break;
		default: Ordered.Add(FText::FromString(Value.ToString())); break;
		}
	}
	return FText::Format(Text.Text, Ordered);
}

FText UDreamNarrativeStory::GetQuestJournal(FName Quest) const
{
	const UDnaQuestSection* Section = nullptr;
	const FDnaQuestData* Data = FindQuest(Quest, &Section);
	if (!Data || !Section || !Section->GetScript())
	{
		return FText();
	}
	switch (GetQuestStateOf(Quest))
	{
	case EDnaQuestState::Accepted: return Format(Data->JournalAccepted, *Section->GetScript());
	case EDnaQuestState::Going: return Format(Data->JournalGoing.IsEmpty() ? Data->JournalAccepted : Data->JournalGoing, *Section->GetScript());
	case EDnaQuestState::Completed: return Format(Data->JournalCompleted, *Section->GetScript());
	case EDnaQuestState::Failed:
	case EDnaQuestState::TimedOut: return Format(Data->JournalFailed, *Section->GetScript());
	default: return FText();
	}
}

TArray<FDnaObjectiveView> UDreamNarrativeStory::GetQuestObjectives(FName Quest) const
{
	TArray<FDnaObjectiveView> Views;
	const UDnaQuestSection* Section = nullptr;
	const FDnaQuestData* Data = FindQuest(Quest, &Section);
	if (!Data || !Section || !Section->GetScript() || !Ledger)
	{
		return Views;
	}
	const int32 Done = Ledger->GetFactAsInt(::DreamNarrative::QuestObjectivesFact(Quest));
	for (int32 Index = 0; Index < Data->Objectives.Num(); ++Index)
	{
		FDnaObjectiveView& View = Views.AddDefaulted_GetRef();
		View.Text = Format(Data->Objectives[Index].Show, *Section->GetScript());
		View.bDone = Index < 31 && (Done & (1 << Index)) != 0;
		View.bOptional = Data->Objectives[Index].bOptional;
	}
	return Views;
}
