// Copyright DreamMoon. All Rights Reserved.

#include "DreamNarrativeLedger.h"
#include "DreamNarrativeCoreModule.h"
#include "DreamNarrativeSettings.h"
#include "Subsystems/SubsystemCollection.h"

void UDreamNarrativeLedger::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	const int32 Seed = UDreamNarrativeSettings::Get()->RandomSeed;
	if (Seed != 0)
	{
		Random.Initialize(Seed);
	}
	else
	{
		Random.GenerateNewSeed();
	}
}

void UDreamNarrativeLedger::Deinitialize()
{
	// The game's clock keeps its timers on the game instance; drop them while it is still there.
	Timers.Reset();
	Clock.Reset();
	Super::Deinitialize();
}

// ---------------------------------------------------------------- declarations

void UDreamNarrativeLedger::RegisterScript(UDreamNarrativeScript* Script)
{
	if (!Script)
	{
		return;
	}
	Scripts.AddUnique(Script);

	for (const FDnaFactDecl& Decl : Script->Facts)
	{
		const bool bNew = !FactDecls.Contains(Decl.Name);
		FactDecls.Add(Decl.Name, Decl);
		if (bNew || !Values.Contains(Decl.Name))
		{
			FDnaValue Default = Decl.Default;
			Default.Type = Decl.Type;
			Values.Add(Decl.Name, Default);
		}
		if (!Decl.WasName.IsNone())
		{
			Redirects.Add(Decl.WasName, Decl.Name);
		}
	}
	for (const FDnaCharacterDecl& Decl : Script->Characters)
	{
		CharacterDecls.Add(Decl.Name, Decl);
	}
	for (const FDnaEventDecl& Decl : Script->Events)
	{
		EventDecls.Add(Decl.Name, Decl);
	}
	for (const FDnaChapterDecl& Decl : Script->Chapters)
	{
		if (!ChapterDecls.Contains(Decl.Name))
		{
			ChapterOrder.Add(Decl.Name);
		}
		ChapterDecls.Add(Decl.Name, Decl);
		ChapterScripts.Add(Decl.Name, Script);
		if (!Decl.WasName.IsNone())
		{
			Redirects.Add(Decl.WasName, Decl.Name);
		}
	}
	OnScriptRegistered.Broadcast(Script);
}

const FDnaFactDecl* UDreamNarrativeLedger::FindFactDecl(FName Fact) const
{
	return FactDecls.Find(Resolve(Fact));
}

const FDnaChapterDecl* UDreamNarrativeLedger::FindChapterDecl(FName Chapter) const
{
	return ChapterDecls.Find(Resolve(Chapter));
}

const FDnaCharacterDecl* UDreamNarrativeLedger::FindCharacterDecl(FName Character) const
{
	return CharacterDecls.Find(Character);
}

FName UDreamNarrativeLedger::Resolve(FName Name) const
{
	// A rename chain is followed to its end; a cycle would be a compiler bug, so it is bounded.
	for (int32 Hops = 0; Hops < 16; ++Hops)
	{
		const FName* Next = Redirects.Find(Name);
		if (!Next || FactDecls.Contains(Name) || ChapterDecls.Contains(Name))
		{
			break;
		}
		Name = *Next;
	}
	return Name;
}

// ---------------------------------------------------------------- facts

FDnaValue UDreamNarrativeLedger::GetFact(FName Fact) const
{
	if (const FDnaValue* Value = Values.Find(Resolve(Fact)))
	{
		return *Value;
	}
	return FDnaValue();
}

bool UDreamNarrativeLedger::SetFact(FName Fact, const FDnaValue& InValue, EDnaWriteSource Source)
{
	const FName Name = Resolve(Fact);
	const FDnaFactDecl* Decl = FactDecls.Find(Name);
	auto Report = [this, Name, Decl, Source](EDnaFactWriteResult Result)
	{
		if (Trace)
		{
			Trace->OnFactWrite(Name, Decl ? Decl->Type : EDnaFactType::Bool, Decl ? Decl->Owner : EDnaFactOwner::Narrative, Source, Result);
		}
	};
	if (!Decl)
	{
		UE_LOG(LogDreamNarrative, Error, TEXT("SetFact: no fact called '%s' is declared"), *Fact.ToString());
		Report(EDnaFactWriteResult::RefusedUndeclared);
		return false;
	}

	if (Source == EDnaWriteSource::Game && Decl->Owner == EDnaFactOwner::Narrative)
	{
		if (UDreamNarrativeSettings::Get()->bEnforceFactOwnership)
		{
			UE_LOG(LogDreamNarrative, Error, TEXT("SetFact: '%s' is owned by the narrative; game code may not write it (declare it `extern` if the game feeds it)"), *Name.ToString());
			Report(EDnaFactWriteResult::RefusedOwnership);
			return false;
		}
		UE_LOG(LogDreamNarrative, Warning, TEXT("SetFact: game code is writing narrative-owned fact '%s' (ownership enforcement is off)"), *Name.ToString());
	}

	// Coerce to the declared type; refuse what cannot be coerced.
	FDnaValue Value = InValue;
	switch (Decl->Type)
	{
	case EDnaFactType::Bool:
		Value = FDnaValue::FromBool(InValue.AsBool());
		break;
	case EDnaFactType::Int:
		if (InValue.Type == EDnaFactType::Tag)
		{
			UE_LOG(LogDreamNarrative, Error, TEXT("SetFact: '%s' is an int fact; a tag value cannot be written to it"), *Name.ToString());
			Report(EDnaFactWriteResult::RefusedType);
			return false;
		}
		Value = FDnaValue::FromInt(static_cast<int32>(InValue.AsNumber()));
		break;
	case EDnaFactType::Float:
		if (InValue.Type == EDnaFactType::Tag)
		{
			UE_LOG(LogDreamNarrative, Error, TEXT("SetFact: '%s' is a float fact; a tag value cannot be written to it"), *Name.ToString());
			Report(EDnaFactWriteResult::RefusedType);
			return false;
		}
		Value = FDnaValue::FromFloat(static_cast<float>(InValue.AsNumber()));
		break;
	case EDnaFactType::Tag:
		if (InValue.Type != EDnaFactType::Tag || !Decl->Domain.Contains(InValue.Tag))
		{
			UE_LOG(LogDreamNarrative, Error, TEXT("SetFact: '%s' is not one of the values of '%s'"), *InValue.ToString(), *Name.ToString());
			Report(EDnaFactWriteResult::RefusedDomain);
			return false;
		}
		break;
	}
	bool bClamped = false;
	if (Decl->bHasRange && Value.IsNumeric())
	{
		const double Clamped = FMath::Clamp(Value.AsNumber(), Decl->RangeMin, Decl->RangeMax);
		bClamped = Clamped != Value.AsNumber();
		Value = Decl->Type == EDnaFactType::Int ? FDnaValue::FromInt(static_cast<int32>(Clamped)) : FDnaValue::FromFloat(static_cast<float>(Clamped));
	}

	FDnaValue& Slot = Values.FindOrAdd(Name);
	if (Slot.Equals(Value) && Slot.Type == Value.Type)
	{
		Report(bClamped ? EDnaFactWriteResult::Clamped : EDnaFactWriteResult::Unchanged);
		return true;
	}
	Report(bClamped ? EDnaFactWriteResult::Clamped : EDnaFactWriteResult::Written);
	const FDnaValue Old = Slot;
	Slot = Value;

	OnFactChanged.Broadcast(Name, Old, Value);
	OnFactWritten.Broadcast(Name, Value, Source);
	DirtyFacts.AddUnique(Name);
	if (BatchDepth == 0)
	{
		Commit();
	}
	return true;
}

bool UDreamNarrativeLedger::AddToFact(FName Fact, float Delta)
{
	const FDnaFactDecl* Decl = FindFactDecl(Fact);
	if (!Decl || !(Decl->Type == EDnaFactType::Int || Decl->Type == EDnaFactType::Float))
	{
		UE_LOG(LogDreamNarrative, Error, TEXT("AddToFact: '%s' is not a numeric fact"), *Fact.ToString());
		return false;
	}
	const FDnaValue Current = GetFact(Fact);
	return SetFact(Fact, Decl->Type == EDnaFactType::Int
		? FDnaValue::FromInt(Current.Int + FMath::RoundToInt(Delta))
		: FDnaValue::FromFloat(Current.Float + Delta), EDnaWriteSource::Game);
}

// ---------------------------------------------------------------- batches

void UDreamNarrativeLedger::BeginBatch()
{
	++BatchDepth;
}

void UDreamNarrativeLedger::EndBatch()
{
	if (BatchDepth <= 0)
	{
		UE_LOG(LogDreamNarrative, Warning, TEXT("EndBatch without a matching BeginBatch"));
		BatchDepth = 0;
		return;
	}
	if (--BatchDepth == 0)
	{
		Commit();
	}
}

void UDreamNarrativeLedger::Commit()
{
	if (bCommitting || (DirtyFacts.Num() == 0 && !bWorldStateDirty))
	{
		return;
	}
	// Observers may write facts of their own; those land in a fresh batch after this one.
	bCommitting = true;
	bWorldStateDirty = false;
	TArray<FName> Changed = MoveTemp(DirtyFacts);
	DirtyFacts.Reset();
	if (Changed.Num() > 0)
	{
		if (Trace) { Trace->OnEvent(Changed.Num() > 1 ? TEXT("ledger.batch.several") : TEXT("ledger.batch.one")); }
		OnBatchCommitted.Broadcast(Changed);
		CheckAdvanceRules();
	}
	else
	{
		OnWorldStateChanged.Broadcast();
		// A rule can read `visited(...)` with no fact behind it.
		CheckAdvanceRules();
	}
	bCommitting = false;
	if ((DirtyFacts.Num() > 0 || bWorldStateDirty) && BatchDepth == 0)
	{
		Commit();
	}
}

// ---------------------------------------------------------------- chapter

bool UDreamNarrativeLedger::SetChapter(FName Chapter, bool bLeaveCheckpoint)
{
	const FName Name = Resolve(Chapter);
	if (!ChapterDecls.Contains(Name))
	{
		UE_LOG(LogDreamNarrative, Error, TEXT("SetChapter: no chapter called '%s'"), *Chapter.ToString());
		return false;
	}
	if (CurrentChapter == Name)
	{
		return true;
	}
	const FName Old = CurrentChapter;
	CurrentChapter = Name;
	if (bLeaveCheckpoint)
	{
		// The first time only: entering a chapter again leaves the checkpoint it has.
		if (Checkpoints.ContainsByPredicate([Name](const FDnaChapterCheckpoint& Point) { return Point.Chapter == Name; }))
		{
			if (Trace) { Trace->OnEvent(TEXT("checkpoint.kept")); }
		}
		else
		{
			LeaveCheckpoint(Name);
		}
	}
	OnChapterChanged.Broadcast(Old, Name);
	bWorldStateDirty = true;
	if (BatchDepth == 0) { Commit(); }
	return true;
}

bool UDreamNarrativeLedger::IsProgress(FName Fact) const
{
	const FDnaFactDecl* Decl = FactDecls.Find(Resolve(Fact));
	return Decl && Decl->bProgress && !Decl->bSession && !Decl->bProfile;
}

void UDreamNarrativeLedger::LeaveCheckpoint(FName Chapter)
{
	if (Trace) { Trace->OnEvent(TEXT("checkpoint.leave")); }
	FDnaChapterCheckpoint& Point = Checkpoints.AddDefaulted_GetRef();
	Point.Chapter = Chapter;
	for (const TPair<FName, FDnaValue>& Pair : Values)
	{
		const FDnaFactDecl* Decl = FactDecls.Find(Pair.Key);
		if (Decl && !Decl->bSession && !Decl->bProfile && !Decl->bProgress)
		{
			FDnaFactSnapshotEntry& Entry = Point.Facts.AddDefaulted_GetRef();
			Entry.Fact = Pair.Key;
			Entry.Value = Pair.Value;
		}
	}
	Point.Visited = VisitCounts;
	Point.ChosenOnce = ChosenOnce.Array();
	for (const TPair<FName, FDnaClockHandle>& Pair : Timers)
	{
		if (!IsProgress(Pair.Key))
		{
			Point.Timers.Add(Pair.Key, static_cast<float>(FMath::Max(0.0, GetTimerRemaining(Pair.Key))));
		}
	}
}

TArray<FName> UDreamNarrativeLedger::GetChapterCheckpoints() const
{
	TArray<FName> Out;
	for (const FDnaChapterCheckpoint& Point : Checkpoints)
	{
		Out.Add(Point.Chapter);
	}
	return Out;
}

void UDreamNarrativeLedger::StartNewPlaythrough()
{
	if (Trace) { Trace->OnEvent(TEXT("playthrough.new")); }
	// An empty save: everything of the slot back to its defaults, and whatever ran on the old story ends.
	ApplySnapshot(FDnaLedgerSnapshot());
	OnNewPlaythrough.Broadcast();
	if (ChapterOrder.Num() > 0)
	{
		SetChapter(ChapterOrder[0]);
	}
}

bool UDreamNarrativeLedger::SelectChapter(FName Chapter)
{
	const FName Name = Resolve(Chapter);
	const FDnaChapterCheckpoint* Point = Checkpoints.FindByPredicate([Name](const FDnaChapterCheckpoint& Each) { return Each.Chapter == Name; });
	if (!Point)
	{
		UE_LOG(LogDreamNarrative, Warning, TEXT("SelectChapter: the story never entered '%s', so it has no checkpoint"), *Chapter.ToString());
		return false;
	}
	OnChapterSelected.Broadcast(Name);
	if (Trace) { Trace->OnEvent(TEXT("checkpoint.select")); }

	FDnaLedgerSnapshot Snapshot;
	Snapshot.Chapter = Name;
	Snapshot.Facts = Point->Facts;
	Snapshot.Visited = Point->Visited;
	Snapshot.ChosenOnce = Point->ChosenOnce;
	Snapshot.Timers = Point->Timers;
	Snapshot.Checkpoints = Checkpoints;
	// Progress goes on from where it is now, with its timers (a side quest's timeout, say).
	bool bKept = false;
	for (const TPair<FName, FDnaValue>& Pair : Values)
	{
		if (!IsProgress(Pair.Key))
		{
			continue;
		}
		Snapshot.Facts.RemoveAll([this, &Pair](const FDnaFactSnapshotEntry& Entry) { return Resolve(Entry.Fact) == Pair.Key; });
		FDnaFactSnapshotEntry& Entry = Snapshot.Facts.AddDefaulted_GetRef();
		Entry.Fact = Pair.Key;
		Entry.Value = Pair.Value;
		bKept = true;
	}
	for (auto It = Snapshot.Timers.CreateIterator(); It; ++It)
	{
		if (IsProgress(It.Key()))
		{
			It.RemoveCurrent();
		}
	}
	for (const TPair<FName, FDnaClockHandle>& Pair : Timers)
	{
		if (IsProgress(Pair.Key))
		{
			Snapshot.Timers.Add(Pair.Key, static_cast<float>(FMath::Max(0.0, GetTimerRemaining(Pair.Key))));
		}
	}
	if (bKept && Trace) { Trace->OnEvent(TEXT("checkpoint.progress-kept")); }
	ApplySnapshot(Snapshot);
	return true;
}

void UDreamNarrativeLedger::ApplyImplied(const FDnaChapterDecl& Chapter)
{
	for (const FDnaImpliedFact& Implied : Chapter.Implied)
	{
		SetFact(Implied.Fact, Implied.Value, EDnaWriteSource::Narrative);
	}
}

bool UDreamNarrativeLedger::JumpToChapter(FName Chapter)
{
	const FName Target = Resolve(Chapter);
	if (!ChapterDecls.Contains(Target))
	{
		UE_LOG(LogDreamNarrative, Error, TEXT("JumpToChapter: no chapter called '%s'"), *Chapter.ToString());
		return false;
	}

	TGuardValue<bool> SuppressAdvance(bSuppressAdvance, true);
	OnJumpRequested.Broadcast(Target);

	// Walk the `after` chain back to the first chapter, then apply forwards.
	TArray<FName> Chain;
	FName Cursor = Target;
	while (!Cursor.IsNone() && !Chain.Contains(Cursor))
	{
		Chain.Add(Cursor);
		const FDnaChapterDecl* Decl = ChapterDecls.Find(Cursor);
		Cursor = Decl ? Resolve(Decl->After) : NAME_None;
	}

	if (Trace)
	{
		Trace->OnEvent(TEXT("chapter.jump"));
		if (Chain.Num() > 1)
		{
			Trace->OnEvent(TEXT("chapter.after-chain"));
		}
	}
	{
		FBatchScope Batch(*this);
		for (int32 Index = Chain.Num() - 1; Index >= 0; --Index)
		{
			const FDnaChapterDecl& Decl = ChapterDecls[Chain[Index]];
			if (Trace && Decl.Implied.Num() > 0)
			{
				Trace->OnEvent(TEXT("chapter.implied"));
			}
			ApplyImplied(Decl);
		}
	}
	// A teleport is not the story moving on: it leaves no checkpoint.
	return SetChapter(Target, /*bLeaveCheckpoint=*/false);
}

void UDreamNarrativeLedger::CheckAdvanceRules()
{
	if (CurrentChapter.IsNone() || bApplyingSnapshot || bSuppressAdvance)
	{
		return;
	}
	const FDnaChapterDecl* Decl = ChapterDecls.Find(CurrentChapter);
	const TWeakObjectPtr<UDreamNarrativeScript>* ScriptPtr = ChapterScripts.Find(CurrentChapter);
	if (!Decl || !ScriptPtr || !ScriptPtr->IsValid())
	{
		return;
	}
	for (const FDnaAdvanceRule& Rule : Decl->Advance)
	{
		const FDnaCompiledExpr* Condition = ScriptPtr->Get()->GetExpression(Rule.ConditionExpr);
		if (Rule.ConditionExpr == INDEX_NONE || (Condition && EvaluateCondition(*Condition)))
		{
			if (Trace)
			{
				Trace->OnEvent(TEXT("chapter.advance-rule"));
			}
			SetChapter(Rule.Target);
			return;
		}
	}
}

// ---------------------------------------------------------------- bookkeeping

int32 UDreamNarrativeLedger::GetVisited(FName Dialogue) const
{
	const int32* Count = VisitCounts.Find(Resolve(Dialogue));
	return Count ? *Count : 0;
}

void UDreamNarrativeLedger::MarkVisited(FName Dialogue)
{
	SetVisited(Dialogue, GetVisited(Dialogue) + 1);
}

void UDreamNarrativeLedger::SetVisited(FName Dialogue, int32 Count)
{
	Dialogue = Resolve(Dialogue);
	if (GetVisited(Dialogue) == FMath::Max(0, Count)) { return; }
	if (Count > 0)
	{
		VisitCounts.Add(Dialogue, Count);
	}
	else
	{
		VisitCounts.Remove(Dialogue);
	}
	bWorldStateDirty = true;
	if (BatchDepth == 0) { Commit(); }
}

void UDreamNarrativeLedger::EmitEvent(FName Event)
{
	FDnaEventPayload Payload;
	Payload.Event = Event;
	RaiseEventAs(Payload, EDnaWriteSource::Game);
}

namespace
{
	/** Value as Param holds it, the way a fact write converts it; false when it does not fit at all. */
	bool FitEventArg(const FDnaFactDecl& Param, const FDnaValue& Value, FDnaValue& Out)
	{
		switch (Param.Type)
		{
		case EDnaFactType::Bool:
			if (Value.Type == EDnaFactType::Tag) { return false; }
			Out = FDnaValue::FromBool(Value.AsBool());
			return true;
		case EDnaFactType::Int:
		case EDnaFactType::Float:
		{
			if (Value.Type == EDnaFactType::Tag) { return false; }
			double Number = Value.AsNumber();
			if (Param.bHasRange)
			{
				Number = FMath::Clamp(Number, Param.RangeMin, Param.RangeMax);
			}
			Out = Param.Type == EDnaFactType::Int ? FDnaValue::FromInt(static_cast<int32>(Number)) : FDnaValue::FromFloat(static_cast<float>(Number));
			return true;
		}
		case EDnaFactType::Tag:
			if (Value.Type != EDnaFactType::Tag || !Param.Domain.Contains(Value.Tag)) { return false; }
			Out = Value;
			return true;
		}
		return false;
	}
}

void UDreamNarrativeLedger::RaiseEventAs(const FDnaEventPayload& InEvent, EDnaWriteSource Source)
{
	FDnaEventPayload Event;
	Event.Event = InEvent.Event;
	if (const FDnaEventDecl* Decl = EventDecls.Find(InEvent.Event))
	{
		// Every parameter is there, in declaration order: the value given when it fits, the default otherwise.
		for (const FDnaFactDecl& Param : Decl->Params)
		{
			FDnaValue Value = Param.Default;
			Value.Type = Param.Type;
			if (const FDnaValue* Given = InEvent.Find(Param.Name))
			{
				if (!FitEventArg(Param, *Given, Value))
				{
					UE_LOG(LogDreamNarrative, Warning, TEXT("RaiseEvent: '%s' does not fit parameter '%s' of event '%s'; its default stands in"),
						*Given->ToString(), *Param.Name.ToString(), *InEvent.Event.ToString());
					if (Trace) { Trace->OnEvent(TEXT("event.arg-refused")); }
				}
			}
			else if (Trace)
			{
				Trace->OnEvent(TEXT("event.arg-default"));
			}
			Event.Args.Add({ Param.Name, Value });
		}
		for (const FDnaEventArg& Arg : InEvent.Args)
		{
			if (!Decl->FindParam(Arg.Name))
			{
				UE_LOG(LogDreamNarrative, Warning, TEXT("RaiseEvent: event '%s' has no parameter '%s'; the value was dropped"),
					*InEvent.Event.ToString(), *Arg.Name.ToString());
				if (Trace) { Trace->OnEvent(TEXT("event.arg-refused")); }
			}
		}
	}
	else
	{
		Event.Args = InEvent.Args;
	}
	if (Trace)
	{
		Trace->OnEvent(Source == EDnaWriteSource::Game ? TEXT("event.raise.game") : TEXT("event.raise.narrative"));
	}
	// The game hears it first, then the story answers it.
	OnEvent.Broadcast(Event.Event);
	OnEventPayload.Broadcast(Event);
	OnEventRaised.Broadcast(Event, Source);
}

bool UDreamNarrativeLedger::GetQuestState(FName Quest, EDnaQuestStateQuery Query) const
{
	return QuestStateProvider ? QuestStateProvider->GetQuestState(Quest, Query) : false;
}

IDnaClock& UDreamNarrativeLedger::GetClock()
{
	if (!Clock.IsValid())
	{
		Clock = MakeShared<FDnaWorldClock>(GetGameInstance());
	}
	return *Clock;
}

// ---------------------------------------------------------------- timers

void UDreamNarrativeLedger::StartTimer(FName Timer, double Seconds)
{
	Timer = Resolve(Timer);
	CancelTimer(Timer);
	if (Trace)
	{
		Trace->OnEvent(TEXT("ledger.timer.start"));
	}
	Timers.Add(Timer, GetClock().Schedule(Seconds, FSimpleDelegate::CreateWeakLambda(this, [this, Timer]()
	{
		Timers.Remove(Timer);
		if (Trace)
		{
			Trace->OnEvent(TEXT("ledger.timer.fired"));
		}
		OnTimerFired.Broadcast(Timer);
	})));
}

void UDreamNarrativeLedger::CancelTimer(FName Timer)
{
	Timer = Resolve(Timer);
	if (FDnaClockHandle* Handle = Timers.Find(Timer))
	{
		if (Clock.IsValid())
		{
			Clock->Cancel(*Handle);
		}
		Timers.Remove(Timer);
	}
}

double UDreamNarrativeLedger::GetTimerRemaining(FName Timer) const
{
	const FDnaClockHandle* Handle = Timers.Find(Resolve(Timer));
	return Handle && Clock.IsValid() ? Clock->GetRemaining(*Handle) : -1.0;
}

// ---------------------------------------------------------------- snapshot

FDnaLedgerSnapshot UDreamNarrativeLedger::MakeSnapshot() const
{
	FDnaLedgerSnapshot Snapshot;
	for (const TPair<FName, FDnaValue>& Pair : Values)
	{
		const FDnaFactDecl* Decl = FactDecls.Find(Pair.Key);
		if (Decl && Decl->bSession)
		{
			if (Trace)
			{
				Trace->OnEvent(TEXT("ledger.snapshot.session-left-out"));
			}
			continue;
		}
		if (Decl && Decl->bProfile)
		{
			if (Trace)
			{
				Trace->OnEvent(TEXT("ledger.snapshot.profile-left-out"));
			}
			continue;
		}
		FDnaFactSnapshotEntry& Entry = Snapshot.Facts.AddDefaulted_GetRef();
		Entry.Fact = Pair.Key;
		Entry.Value = Pair.Value;
	}
	Snapshot.Chapter = CurrentChapter;
	Snapshot.Visited = VisitCounts;
	Snapshot.ChosenOnce = ChosenOnce.Array();
	for (const TPair<FName, FDnaClockHandle>& Pair : Timers)
	{
		Snapshot.Timers.Add(Pair.Key, static_cast<float>(FMath::Max(0.0, GetTimerRemaining(Pair.Key))));
	}
	Snapshot.Checkpoints = Checkpoints;
	return Snapshot;
}

FDnaProfileSnapshot UDreamNarrativeLedger::MakeProfileSnapshot() const
{
	FDnaProfileSnapshot Snapshot;
	for (const TPair<FName, FDnaValue>& Pair : Values)
	{
		const FDnaFactDecl* Decl = FactDecls.Find(Pair.Key);
		if (Decl && Decl->bProfile)
		{
			FDnaFactSnapshotEntry& Entry = Snapshot.Facts.AddDefaulted_GetRef();
			Entry.Fact = Pair.Key;
			Entry.Value = Pair.Value;
		}
	}
	return Snapshot;
}

bool UDreamNarrativeLedger::ApplyProfileSnapshot(const FDnaProfileSnapshot& Snapshot)
{
	// Written by a newer build: what it holds may not read the way this one thinks. Refused whole
	// rather than taken in part.
	const int32 Readable = FDnaProfileSnapshot().SchemaVersion;
	if (Snapshot.SchemaVersion > Readable)
	{
		UE_LOG(LogDreamNarrative, Error, TEXT("ApplyProfileSnapshot: the profile is schema %d, newer than this build reads (%d); nothing was applied"), Snapshot.SchemaVersion, Readable);
		return false;
	}
	if (Trace)
	{
		Trace->OnEvent(TEXT("profile.apply"));
	}
	// Loading a profile is no progress either: nothing it sets starts or finishes anything.
	TGuardValue<bool> Applying(bApplyingSnapshot, true);
	{
		FBatchScope Batch(*this);
		for (const TPair<FName, FDnaFactDecl>& Pair : FactDecls)
		{
			if (Pair.Value.bProfile)
			{
				FDnaValue Default = Pair.Value.Default;
				Default.Type = Pair.Value.Type;
				SetFact(Pair.Key, Default, EDnaWriteSource::Narrative);
			}
		}
		for (const FDnaFactSnapshotEntry& Entry : Snapshot.Facts)
		{
			const FName Name = Resolve(Entry.Fact);
			const FDnaFactDecl* Decl = FactDecls.Find(Name);
			if (!Decl || !Decl->bProfile)
			{
				UE_LOG(LogDreamNarrative, Warning, TEXT("ApplyProfileSnapshot: '%s' is no longer a profile fact; its value was dropped"), *Entry.Fact.ToString());
				continue;
			}
			SetFact(Name, Entry.Value, EDnaWriteSource::Narrative);
		}
	}
	OnSnapshotRestored.Broadcast();
	OnSnapshotApplied.Broadcast();
	return true;
}

bool UDreamNarrativeLedger::ApplySnapshot(const FDnaLedgerSnapshot& InSnapshot)
{
	const int32 Readable = FDnaLedgerSnapshot().SchemaVersion;
	if (InSnapshot.SchemaVersion > Readable)
	{
		UE_LOG(LogDreamNarrative, Error, TEXT("ApplySnapshot: the snapshot is schema %d, newer than this build reads (%d); nothing was applied"), InSnapshot.SchemaVersion, Readable);
		return false;
	}
	FDnaLedgerSnapshot Snapshot = InSnapshot;
	// Older layouts come up one version at a time; a step goes here when a layout changes what it means.
	// 1 -> 2 only added checkpoints, and a save from before has none.
	Snapshot.SchemaVersion = Readable;
	// Canonical entries win if an old save contains both the old and current spelling.
	auto RemapKeys = [this](auto& Map)
	{
		auto Before = MoveTemp(Map);
		Map.Reset();
		for (const auto& Pair : Before)
		{
			if (Resolve(Pair.Key) == Pair.Key) { Map.Add(Pair.Key, Pair.Value); }
		}
		for (const auto& Pair : Before)
		{
			const FName Key = Resolve(Pair.Key);
			if (!Map.Contains(Key)) { Map.Add(Key, Pair.Value); }
		}
	};
	auto RemapChoices = [this](TArray<FName>& Choices)
	{
		for (FName& Choice : Choices) { Choice = Resolve(Choice); }
	};
	RemapKeys(Snapshot.Visited);
	RemapKeys(Snapshot.Timers);
	RemapChoices(Snapshot.ChosenOnce);
	for (FDnaChapterCheckpoint& Point : Snapshot.Checkpoints)
	{
		Point.Chapter = Resolve(Point.Chapter);
		RemapKeys(Point.Visited);
		RemapKeys(Point.Timers);
		RemapChoices(Point.ChosenOnce);
	}
	// Old names now one chapter keep one checkpoint: the first, left when it was first entered. A chapter
	// no script declares any more cannot be chosen, so its checkpoint goes.
	TSet<FName> Seen;
	Snapshot.Checkpoints.RemoveAll([this, &Seen](const FDnaChapterCheckpoint& Point)
	{
		bool bAlready = false;
		Seen.Add(Point.Chapter, &bAlready);
		if (!bAlready && ChapterDecls.Num() > 0 && !ChapterDecls.Contains(Point.Chapter))
		{
			UE_LOG(LogDreamNarrative, Warning, TEXT("ApplySnapshot: chapter '%s' is no longer declared; its checkpoint was dropped"), *Point.Chapter.ToString());
			return true;
		}
		return bAlready;
	});
	if (Trace)
	{
		Trace->OnEvent(TEXT("ledger.snapshot.apply"));
	}
	TGuardValue<bool> Applying(bApplyingSnapshot, true);
	// What runs on the story being replaced ends first; whatever it writes on the way out is overwritten below.
	OnSnapshotApplying.Broadcast();
	{
		FBatchScope Batch(*this);
		ResetToDefaults();
		for (const FDnaFactSnapshotEntry& Entry : Snapshot.Facts)
		{
			const FName Name = Resolve(Entry.Fact);
			if (!FactDecls.Contains(Name))
			{
				UE_LOG(LogDreamNarrative, Warning, TEXT("ApplySnapshot: fact '%s' is no longer declared; its value was dropped"), *Entry.Fact.ToString());
				if (Trace)
				{
					Trace->OnEvent(TEXT("ledger.snapshot.dropped"));
				}
				continue;
			}
			if (Trace && Name != Entry.Fact)
			{
				Trace->OnEvent(TEXT("ledger.snapshot.renamed"));
			}
			// Written before the fact moved to the profile: the profile has its own value now.
			if (FactDecls[Name].bProfile)
			{
				continue;
			}
			SetFact(Name, Entry.Value, EDnaWriteSource::Narrative);
		}
		VisitCounts = Snapshot.Visited;
		ChosenOnce = TSet<FName>(Snapshot.ChosenOnce);
		Checkpoints = Snapshot.Checkpoints;
	}
	// Set after the batch has committed, while no chapter is current, so no advance rule runs: loading
	// a save restores the chapter it was saved in. A rule it satisfies runs at the next write, as it
	// would have before the save.
	if (!Snapshot.Chapter.IsNone())
	{
		if (Trace)
		{
			Trace->OnEvent(TEXT("chapter.restored"));
		}
		SetChapter(Snapshot.Chapter, /*bLeaveCheckpoint=*/false);
	}
	// Timers go on from what they had left.
	for (const TPair<FName, float>& Pair : Snapshot.Timers)
	{
		StartTimer(Pair.Key, Pair.Value);
	}
	OnSnapshotRestored.Broadcast();
	OnSnapshotApplied.Broadcast();
	return true;
}

void UDreamNarrativeLedger::ResetToDefaults()
{
	FBatchScope Batch(*this);
	for (const TPair<FName, FDnaFactDecl>& Pair : FactDecls)
	{
		if (Pair.Value.bProfile)
		{
			continue;
		}
		FDnaValue Default = Pair.Value.Default;
		Default.Type = Pair.Value.Type;
		SetFact(Pair.Key, Default, EDnaWriteSource::Narrative);
	}
	VisitCounts.Reset();
	ChosenOnce.Reset();
	Checkpoints.Reset();
	TArray<FName> Running;
	Timers.GetKeys(Running);
	for (const FName& Timer : Running)
	{
		CancelTimer(Timer);
	}
	if (!CurrentChapter.IsNone())
	{
		const FName Old = CurrentChapter;
		CurrentChapter = NAME_None;
		OnChapterChanged.Broadcast(Old, NAME_None);
	}
}
