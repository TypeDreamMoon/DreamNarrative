// Copyright DreamMoon. All Rights Reserved.

#include "DreamNarrativeLedger.h"
#include "DreamNarrativeCoreModule.h"
#include "DreamNarrativeSettings.h"

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
	if (bCommitting || DirtyFacts.Num() == 0)
	{
		return;
	}
	// Observers may write facts of their own; those land in a fresh batch after this one.
	bCommitting = true;
	TArray<FName> Changed = MoveTemp(DirtyFacts);
	DirtyFacts.Reset();
	if (Trace)
	{
		Trace->OnEvent(Changed.Num() > 1 ? TEXT("ledger.batch.several") : TEXT("ledger.batch.one"));
	}
	OnBatchCommitted.Broadcast(Changed);
	CheckAdvanceRules();
	bCommitting = false;
	if (DirtyFacts.Num() > 0 && BatchDepth == 0)
	{
		Commit();
	}
}

// ---------------------------------------------------------------- chapter

bool UDreamNarrativeLedger::SetChapter(FName Chapter)
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
	OnChapterChanged.Broadcast(Old, Name);
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
	return SetChapter(Target);
}

void UDreamNarrativeLedger::CheckAdvanceRules()
{
	if (CurrentChapter.IsNone())
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
	const int32* Count = VisitCounts.Find(Dialogue);
	return Count ? *Count : 0;
}

void UDreamNarrativeLedger::MarkVisited(FName Dialogue)
{
	++VisitCounts.FindOrAdd(Dialogue);
}

void UDreamNarrativeLedger::SetVisited(FName Dialogue, int32 Count)
{
	if (Count > 0)
	{
		VisitCounts.Add(Dialogue, Count);
	}
	else
	{
		VisitCounts.Remove(Dialogue);
	}
}

void UDreamNarrativeLedger::EmitEvent(FName Event)
{
	OnEvent.Broadcast(Event);
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
		FDnaFactSnapshotEntry& Entry = Snapshot.Facts.AddDefaulted_GetRef();
		Entry.Fact = Pair.Key;
		Entry.Value = Pair.Value;
	}
	Snapshot.Chapter = CurrentChapter;
	Snapshot.Visited = VisitCounts;
	Snapshot.ChosenOnce = ChosenOnce.Array();
	return Snapshot;
}

void UDreamNarrativeLedger::ApplySnapshot(const FDnaLedgerSnapshot& Snapshot)
{
	if (Trace)
	{
		Trace->OnEvent(TEXT("ledger.snapshot.apply"));
	}
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
			SetFact(Name, Entry.Value, EDnaWriteSource::Narrative);
		}
		VisitCounts = Snapshot.Visited;
		ChosenOnce = TSet<FName>(Snapshot.ChosenOnce);
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
		SetChapter(Snapshot.Chapter);
	}
}

void UDreamNarrativeLedger::ResetToDefaults()
{
	FBatchScope Batch(*this);
	for (const TPair<FName, FDnaFactDecl>& Pair : FactDecls)
	{
		FDnaValue Default = Pair.Value.Default;
		Default.Type = Pair.Value.Type;
		SetFact(Pair.Key, Default, EDnaWriteSource::Narrative);
	}
	VisitCounts.Reset();
	ChosenOnce.Reset();
	if (!CurrentChapter.IsNone())
	{
		const FName Old = CurrentChapter;
		CurrentChapter = NAME_None;
		OnChapterChanged.Broadcast(Old, NAME_None);
	}
}
