// Copyright DreamMoon. All Rights Reserved.
//
// The channel half of the Story subsystem: which dialogue runs where, what waits its turn, which
// channel holds which still. Each channel runs one dialogue at a time; channels run side by side.
//
// A channel is talking from the moment a dialogue starts on it until it has nothing left to say
// (its dialogue ended and nothing waits its turn). While it talks, the channels it `pauses` hold
// still; when it starts talking, the channels it `stops` are cut off.

#include "DreamNarrativeStory.h"
#include "DreamNarrativeStoryModule.h"
#include "DreamNarrativeTrace.h"

namespace
{
	bool IsRunning(const UDreamNarrativeDialogueRunner* Runner)
	{
		return Runner && Runner->GetState() != EDnaRunnerState::Ended;
	}

	void Note(const UDreamNarrativeLedger* Ledger, const TCHAR* Event)
	{
		if (IDnaTrace* Tracer = Ledger ? Ledger->GetTrace() : nullptr)
		{
			Tracer->OnEvent(Event);
		}
	}
}

// ---------------------------------------------------------------- looking

void UDreamNarrativeStory::EnsureMainChannel()
{
	const FName Main = ::DreamNarrative::MainChannelName();
	if (ChannelDecls.Contains(Main))
	{
		return;
	}
	FDnaChannelData& Data = ChannelDecls.Add(Main);
	Data.Name = Main;
	Data.bLockInput = true;
	Data.bAutoAdvance = false;
	Data.Busy = EDnaChannelBusy::Interrupt;
	ChannelOrder.Insert(Main, 0);
	Channels.FindOrAdd(Main);
}

const FDnaChannelData* UDreamNarrativeStory::FindChannel(FName Channel) const
{
	return ChannelDecls.Find(Channel.IsNone() ? ::DreamNarrative::MainChannelName() : Channel);
}

UDreamNarrativeDialogueRunner* UDreamNarrativeStory::GetRunner(FName Channel) const
{
	const FDnaChannelState* State = Channels.Find(Channel.IsNone() ? ::DreamNarrative::MainChannelName() : Channel);
	return State && IsRunning(State->Runner) ? State->Runner.Get() : nullptr;
}

int32 UDreamNarrativeStory::GetQueueLength(FName Channel) const
{
	const FDnaChannelState* State = Channels.Find(Channel.IsNone() ? ::DreamNarrative::MainChannelName() : Channel);
	return State ? State->Queue.Num() : 0;
}

bool UDreamNarrativeStory::IsInputLocked() const
{
	for (const TPair<FName, FDnaChannelState>& Pair : Channels)
	{
		const FDnaChannelData* Data = ChannelDecls.Find(Pair.Key);
		if (Data && Data->bLockInput && IsRunning(Pair.Value.Runner) && !Pair.Value.Runner->IsPaused())
		{
			return true;
		}
	}
	return false;
}

FName UDreamNarrativeStory::ChannelOf(const FDnaDialogueNodeData& Data) const
{
	if (Data.Channel.IsNone())
	{
		return ::DreamNarrative::MainChannelName();
	}
	if (!ChannelDecls.Contains(Data.Channel))
	{
		UE_LOG(LogDreamNarrativeStory, Warning, TEXT("Dialogue '%s' runs on channel '%s', which no registered script declares; it runs on main"),
			*Data.Name.ToString(), *Data.Channel.ToString());
		return ::DreamNarrative::MainChannelName();
	}
	return Data.Channel;
}

FDnaChannelState& UDreamNarrativeStory::StateOf(FName Channel)
{
	return Channels.FindOrAdd(Channel);
}

bool UDreamNarrativeStory::IsRunningOrQueued(FName Node) const
{
	for (const TPair<FName, FDnaChannelState>& Pair : Channels)
	{
		const FDnaChannelState& State = Pair.Value;
		if (IsRunning(State.Runner) && State.RunnerNode == Node)
		{
			return true;
		}
		auto Holds = [Node](const FDnaChannelQueueItem& Item) { return Item.Node == Node; };
		if (State.Queue.ContainsByPredicate(Holds) || State.Parked.ContainsByPredicate(Holds))
		{
			return true;
		}
	}
	return false;
}

bool UDreamNarrativeStory::IsEntrySpent(const FDnaDialogueNodeData& Data, bool bForEvent) const
{
	if (!Ledger)
	{
		return false;
	}
	// An event that comes again while its dialogue still runs, or waits, does not start it a second time.
	if (bForEvent && IsRunningOrQueued(Data.Name))
	{
		Note(Ledger, TEXT("dialogue.entry.running"));
		return true;
	}
	if (Data.bOnce && (Ledger->GetVisited(Data.Name) > 0 || IsRunningOrQueued(Data.Name)))
	{
		Note(Ledger, TEXT("dialogue.entry.once-spent"));
		return true;
	}
	if (Data.Cooldown > 0.f)
	{
		const double* Last = LastStarted.Find(Data.Name);
		const double Now = Ledger->GetClock().GetSeconds();
		// The game's clock starts again with each world; an earlier start from before that has long run out.
		if (Last && Now >= *Last && Now - *Last < Data.Cooldown)
		{
			Note(Ledger, TEXT("dialogue.entry.cooldown"));
			return true;
		}
	}
	return false;
}

// ---------------------------------------------------------------- starting

UDreamNarrativeDialogueRunner* UDreamNarrativeStory::StartOnChannel(FName Node, const FDnaEventPayload* Event, bool bRequested)
{
	const UDnaDialogueSection* Section = nullptr;
	const FDnaDialogueNodeData* Data = FindNode(Node, &Section);
	if (!Data || !Section)
	{
		UE_LOG(LogDreamNarrativeStory, Error, TEXT("StartDialogue: no dialogue called '%s'"), *Node.ToString());
		return nullptr;
	}
	const FName Channel = ChannelOf(*Data);
	const FDnaChannelData& Decl = ChannelDecls.FindChecked(Channel);
	const bool bBusy = IsRunning(StateOf(Channel).Runner);

	if (bRequested)
	{
		// The game asked for this one: whatever ran on the channel ends.
		if (bBusy)
		{
			CutOff(Channel, /*bHoldToResume=*/false, NAME_None);
		}
	}
	else if (bBusy || StateOf(Channel).PauseCount > 0)
	{
		if (Decl.Busy == EDnaChannelBusy::Drop)
		{
			Note(Ledger, TEXT("channel.drop"));
			return nullptr;
		}
		const FDnaChannelState& State = StateOf(Channel);
		const bool bCutsIn = Decl.Busy == EDnaChannelBusy::Interrupt && bBusy && State.PauseCount == 0 && Data->Priority >= State.RunnerPriority;
		if (!bCutsIn)
		{
			Note(Ledger, TEXT("channel.queue"));
			FDnaChannelQueueItem Item;
			Item.Node = Node;
			Item.Priority = Data->Priority;
			if (Event)
			{
				Item.Event = *Event;
				Item.bHasEvent = true;
			}
			Enqueue(Channel, MoveTemp(Item));
			return nullptr;
		}
		Note(Ledger, TEXT("channel.interrupt"));
		CutOff(Channel, State.bRunnerResumes, NAME_None);
	}

	UDreamNarrativeDialogueRunner* Started = StartNow(Channel, Node, *Data, *Section, Event);
	// Asked for on a channel another one holds still: it starts, and holds still with it.
	const FDnaChannelState& After = StateOf(Channel);
	if (After.PauseCount > 0 && After.Runner == Started && IsRunning(Started))
	{
		Started->Pause();
	}
	return Started;
}

UDreamNarrativeDialogueRunner* UDreamNarrativeStory::StartNow(FName Channel, FName Node, const FDnaDialogueNodeData& Data, const UDnaDialogueSection& Section, const FDnaEventPayload* Event)
{
	const FDnaChannelData& Decl = ChannelDecls.FindChecked(Channel);
	UDreamNarrativeDialogueRunner* Runner = NewObject<UDreamNarrativeDialogueRunner>(this);
	Runner->SetChannel(Channel, Decl.bAutoAdvance);
	if (Event)
	{
		Runner->SetEventScope(*Event);
	}
	Runner->OnEnded.AddDynamic(this, &UDreamNarrativeStory::HandleRunnerEnded);

	FDnaChannelState& State = StateOf(Channel);
	State.Runner = Runner;
	State.RunnerNode = Node;
	State.RunnerPriority = Data.Priority;
	State.bRunnerResumes = Data.bResume;
	if (Ledger)
	{
		LastStarted.Add(Node, Ledger->GetClock().GetSeconds());
	}
	Note(Ledger, TEXT("channel.start"));
	if (!State.bSuppressing)
	{
		BecameBusy(Channel);
	}
	OnDialogueStarted.Broadcast(Runner);
	Runner->Start(this, Ledger, &Section, Node);
	return Runner;
}

void UDreamNarrativeStory::Enqueue(FName Channel, FDnaChannelQueueItem&& Item)
{
	// Behind what was cut off here and goes on first, behind everything of its priority or higher.
	TArray<FDnaChannelQueueItem>& Queue = StateOf(Channel).Queue;
	int32 At = 0;
	while (At < Queue.Num() && (Queue[At].Suspended || Queue[At].Priority >= Item.Priority))
	{
		++At;
	}
	Queue.Insert(MoveTemp(Item), At);
}

void UDreamNarrativeStory::CutOff(FName Channel, bool bHoldToResume, FName ParkFor)
{
	FDnaChannelState& State = StateOf(Channel);
	UDreamNarrativeDialogueRunner* Runner = State.Runner;
	FDnaChannelQueueItem Held;
	Held.Node = State.RunnerNode;
	Held.Priority = State.RunnerPriority;
	Held.Suspended = Runner;
	Held.WaitsFor = ParkFor;
	State.Runner = nullptr;
	State.RunnerNode = NAME_None;
	if (!IsRunning(Runner))
	{
		return;
	}
	if (!bHoldToResume)
	{
		// No channel holds it any more, so its end does not move the channel on.
		Runner->Abort();
		return;
	}
	Runner->Pause();
	if (ParkFor.IsNone())
	{
		Note(Ledger, TEXT("channel.hold"));
		StateOf(Channel).Queue.Insert(MoveTemp(Held), 0);
	}
	else
	{
		Note(Ledger, TEXT("channel.park"));
		StateOf(Channel).Parked.Add(MoveTemp(Held));
	}
}

void UDreamNarrativeStory::ContinueChannel(FName Channel)
{
	if (IsRunning(StateOf(Channel).Runner))
	{
		return;
	}
	StateOf(Channel).Runner = nullptr;
	while (StateOf(Channel).PauseCount == 0 && StateOf(Channel).Queue.Num() > 0)
	{
		FDnaChannelQueueItem Item = MoveTemp(StateOf(Channel).Queue[0]);
		StateOf(Channel).Queue.RemoveAt(0);
		if (Item.Suspended)
		{
			if (!IsRunning(Item.Suspended))
			{
				continue;
			}
			FDnaChannelState& State = StateOf(Channel);
			State.Runner = Item.Suspended;
			State.RunnerNode = Item.Node;
			State.RunnerPriority = Item.Priority;
			State.bRunnerResumes = true;
			if (!State.bSuppressing)
			{
				BecameBusy(Channel);
			}
			Note(Ledger, TEXT("channel.resume"));
			Item.Suspended->Resume();
			return;
		}
		const UDnaDialogueSection* Section = nullptr;
		const FDnaDialogueNodeData* Data = FindNode(Item.Node, &Section);
		if (!Data || !Section)
		{
			continue;
		}
		StartNow(Channel, Item.Node, *Data, *Section, Item.bHasEvent ? &Item.Event : nullptr);
		return;
	}
	BecameQuiet(Channel);
}

void UDreamNarrativeStory::HandleRunnerEnded(UDreamNarrativeDialogueRunner* Runner)
{
	FName Ended;
	for (const TPair<FName, FDnaChannelState>& Pair : Channels)
	{
		if (Pair.Value.Runner == Runner)
		{
			Ended = Pair.Key;
			break;
		}
	}
	// A dialogue cut off or stopped with everything else is no channel's any more.
	if (Ended.IsNone())
	{
		return;
	}
	StateOf(Ended).Runner = nullptr;
	StateOf(Ended).RunnerNode = NAME_None;
	ContinueChannel(Ended);
}

// ---------------------------------------------------------------- holding each other still

void UDreamNarrativeStory::BecameBusy(FName Channel)
{
	if (StateOf(Channel).bSuppressing)
	{
		return;
	}
	StateOf(Channel).bSuppressing = true;
	const FDnaChannelData* Decl = ChannelDecls.Find(Channel);
	if (!Decl)
	{
		return;
	}
	const TArray<FName> Pauses = Decl->Pauses;
	const TArray<FName> Stops = Decl->Stops;
	for (const FName& Other : Pauses)
	{
		if (Other != Channel && Channels.Contains(Other))
		{
			PauseChannel(Other);
		}
	}
	for (const FName& Other : Stops)
	{
		if (Other != Channel && Channels.Contains(Other))
		{
			StopChannel(Other, Channel);
		}
	}
}

void UDreamNarrativeStory::BecameQuiet(FName Channel)
{
	if (!StateOf(Channel).bSuppressing)
	{
		return;
	}
	StateOf(Channel).bSuppressing = false;
	if (const FDnaChannelData* Decl = ChannelDecls.Find(Channel))
	{
		const TArray<FName> Pauses = Decl->Pauses;
		for (const FName& Other : Pauses)
		{
			if (Other != Channel && Channels.Contains(Other))
			{
				UnpauseChannel(Other);
			}
		}
	}
	// What this channel stopped and waits for it goes on now, first in line on its own channel.
	TArray<FName> Released;
	for (TPair<FName, FDnaChannelState>& Pair : Channels)
	{
		TArray<FDnaChannelQueueItem>& Parked = Pair.Value.Parked;
		for (int32 Index = Parked.Num() - 1; Index >= 0; --Index)
		{
			if (Parked[Index].WaitsFor != Channel)
			{
				continue;
			}
			FDnaChannelQueueItem Item = MoveTemp(Parked[Index]);
			Parked.RemoveAt(Index);
			Item.WaitsFor = NAME_None;
			Pair.Value.Queue.Insert(MoveTemp(Item), 0);
			Released.AddUnique(Pair.Key);
		}
	}
	for (const FName& Other : Released)
	{
		ContinueChannel(Other);
	}
}

void UDreamNarrativeStory::HandleStageActivity(FName Stage, bool bStarted, bool bOnLine)
{
	if (bOnLine)
	{
		return;
	}
	if (bStarted ? ++StagesPlaying != 1 : (StagesPlaying == 0 || --StagesPlaying != 0))
	{
		return;
	}
	for (const FName& Channel : ChannelOrder)
	{
		const FDnaChannelData* Decl = ChannelDecls.Find(Channel);
		if (Decl && Decl->bPauseDuringStage)
		{
			Note(Ledger, bStarted ? TEXT("channel.stage-pause") : TEXT("channel.stage-resume"));
			if (bStarted) { PauseChannel(Channel); }
			else { UnpauseChannel(Channel); }
		}
	}
}

void UDreamNarrativeStory::PauseChannel(FName Channel)
{
	FDnaChannelState& State = StateOf(Channel);
	if (++State.PauseCount == 1 && IsRunning(State.Runner))
	{
		Note(Ledger, TEXT("channel.pause"));
		State.Runner->Pause();
	}
}

void UDreamNarrativeStory::UnpauseChannel(FName Channel)
{
	FDnaChannelState& State = StateOf(Channel);
	if (State.PauseCount == 0 || --State.PauseCount > 0)
	{
		return;
	}
	if (IsRunning(State.Runner))
	{
		Note(Ledger, TEXT("channel.resume"));
		State.Runner->Resume();
		return;
	}
	ContinueChannel(Channel);
}

void UDreamNarrativeStory::StopChannel(FName Channel, FName By)
{
	if (IsRunning(StateOf(Channel).Runner))
	{
		Note(Ledger, TEXT("channel.stop"));
		CutOff(Channel, StateOf(Channel).bRunnerResumes, By);
	}
	// What waited its turn there is dropped with it.
	TArray<FDnaChannelQueueItem> Dropped = MoveTemp(StateOf(Channel).Queue);
	StateOf(Channel).Queue.Reset();
	for (FDnaChannelQueueItem& Item : Dropped)
	{
		if (IsRunning(Item.Suspended))
		{
			Item.Suspended->Abort();
		}
	}
	ContinueChannel(Channel);
}

void UDreamNarrativeStory::StopAllDialogue()
{
	TArray<UDreamNarrativeDialogueRunner*> Ending;
	for (TPair<FName, FDnaChannelState>& Pair : Channels)
	{
		FDnaChannelState& State = Pair.Value;
		Ending.Add(State.Runner);
		for (const FDnaChannelQueueItem& Item : State.Queue) { Ending.Add(Item.Suspended); }
		for (const FDnaChannelQueueItem& Item : State.Parked) { Ending.Add(Item.Suspended); }
		State = FDnaChannelState();
	}
	// No channel holds them now, so ending them starts nothing.
	for (UDreamNarrativeDialogueRunner* Runner : Ending)
	{
		if (IsRunning(Runner))
		{
			Runner->Abort();
		}
	}
}

// ---------------------------------------------------------------- events

void UDreamNarrativeStory::HandleEventRaised(const FDnaEventPayload& Event, EDnaWriteSource Source)
{
	OnEventHeard.Broadcast(Event, Source);
	// Every channel picks against the story as the event found it; then the picks start, in channel order.
	TArray<FName> Picks;
	for (const FName& Channel : ChannelOrder)
	{
		const FName Node = SelectEventEntry(Event.Event, Channel, Event);
		if (!Node.IsNone())
		{
			Picks.Add(Node);
		}
	}
	if (Picks.Num() == 0)
	{
		Note(Ledger, TEXT("event.unheard"));
		return;
	}
	for (const FName& Node : Picks)
	{
		Note(Ledger, TEXT("event.trigger"));
		StartOnChannel(Node, &Event, /*bRequested=*/false);
	}
}

FName UDreamNarrativeStory::SelectEventEntry(FName Event, FName Channel, const FDnaEventPayload& Payload) const
{
	const TArray<FName>* Entries = EntriesByEvent.Find(Event);
	if (!Entries || !Ledger)
	{
		return NAME_None;
	}
	FName Best;
	int32 BestPriority = INT_MIN;
	for (const FName& Name : *Entries)
	{
		const UDnaDialogueSection* Section = nullptr;
		const FDnaDialogueNodeData* Data = FindNode(Name, &Section);
		if (!Data || !Section || ChannelOf(*Data) != Channel || IsEntrySpent(*Data, /*bForEvent=*/true))
		{
			continue;
		}
		if (Data->Condition != INDEX_NONE)
		{
			const FDnaCompiledExpr* Condition = Section->GetScript() ? Section->GetScript()->GetExpression(Data->Condition) : nullptr;
			FDnaEventScopeContext Context(*Ledger, &Payload);
			if (!Condition || !::DreamNarrative::EvaluateCondition(*Condition, Context))
			{
				continue;
			}
		}
		if (Data->Priority > BestPriority)
		{
			Best = Name;
			BestPriority = Data->Priority;
		}
	}
	return Best;
}
