// Copyright DreamMoon. All Rights Reserved.

#include "DreamNarrativeRecorder.h"
#include "DreamNarrativeSettings.h"
#include "DreamNarrativeStory.h"
#include "Misc/Paths.h"

namespace
{
	/** Text that reads back as itself in a test block: no interpolation braces or comment, nothing a line could start as instead. */
	bool IsPlain(const FString& Text)
	{
		if (Text.IsEmpty() || Text != Text.TrimStartAndEnd() || Text.Contains(TEXT("{")) || Text.Contains(TEXT("}")) || Text.Contains(TEXT("//")))
		{
			return false;
		}
		for (const TCHAR* Start : { TEXT("-"), TEXT(">"), TEXT("#"), TEXT("＃"), TEXT("absent") })
		{
			if (Text.StartsWith(Start))
			{
				return false;
			}
		}
		return true;
	}

	/** A value as the source writes it; floats with every digit it takes to read back as the same float. */
	FString ValueText(const FDnaValue& Value)
	{
		return Value.Type == EDnaFactType::Float ? FString::Printf(TEXT("%.9g"), Value.Float) : Value.ToString();
	}

	FDnaValue DefaultOf(const FDnaFactDecl& Decl)
	{
		FDnaValue Default = Decl.Default;
		Default.Type = Decl.Type;
		return Default;
	}

	/** A quest's hidden facts (`@名`, `@名.objectives`) are written as quest states, never as facts. */
	bool IsQuestFact(FName Fact)
	{
		return Fact.ToString().StartsWith(TEXT("@"));
	}

	/** The quest whose state fact this is (`@猎狼` -> 猎狼); None for the objectives fact and every other fact. */
	FName QuestOfStateFact(FName Fact)
	{
		const FString Text = Fact.ToString();
		return Text.StartsWith(TEXT("@")) && !Text.EndsWith(TEXT(".objectives")) ? FName(*Text.Mid(1)) : NAME_None;
	}

	FString QuestExpectText(FName Quest, EDnaQuestState State)
	{
		switch (State)
		{
		case EDnaQuestState::Accepted:
		case EDnaQuestState::Going: return FString::Printf(TEXT("expect %s.active"), *Quest.ToString());
		case EDnaQuestState::Completed: return FString::Printf(TEXT("expect %s.completed"), *Quest.ToString());
		case EDnaQuestState::Failed:
		case EDnaQuestState::TimedOut: return FString::Printf(TEXT("expect %s.failed"), *Quest.ToString());
		default: return FString::Printf(TEXT("expect not %s.active and not %s.completed and not %s.failed"), *Quest.ToString(), *Quest.ToString(), *Quest.ToString());
		}
	}

	/** `emit 名(参数 = 值, ...)` as a step writes it. */
	FString EmitText(const FDnaEventPayload& Event)
	{
		TArray<FString> Args;
		for (const FDnaEventArg& Arg : Event.Args)
		{
			Args.Add(FString::Printf(TEXT("%s = %s"), *Arg.Name.ToString(), *ValueText(Arg.Value)));
		}
		return TEXT("emit ") + Event.Event.ToString() + (Args.Num() > 0 ? TEXT("(") + FString::Join(Args, TEXT(", ")) + TEXT(")") : FString());
	}

	FString ExpectText(const FString& Fact, const FDnaValue& Value)
	{
		if (Value.Type == EDnaFactType::Bool)
		{
			return (Value.bBool ? TEXT("expect ") : TEXT("expect not ")) + Fact;
		}
		return FString::Printf(TEXT("expect %s == %s"), *Fact, *ValueText(Value));
	}
}

// ---------------------------------------------------------------- starting and stopping

void UDreamNarrativeRecorder::Begin(UDreamNarrativeStory* InStory, const FString& InName)
{
	Detach();
	Name = InName;
	Steps.Reset();
	EventSteps.Reset();
	LastDrawStep = INDEX_NONE;
	UDreamNarrativeLedger* InLedger = InStory ? InStory->GetLedger() : nullptr;
	if (!InLedger)
	{
		return;
	}
	Story = InStory;
	Ledger = InLedger;

	// Session facts are left out of a snapshot; a recording starts from them all the same.
	StartSnapshot = Ledger->MakeSnapshot();
	StartSession.Reset();
	TArray<FName> Facts;
	Ledger->GetFactNames(Facts);
	for (const FName& Fact : Facts)
	{
		const FDnaFactDecl* Decl = Ledger->FindFactDecl(Fact);
		if (Decl && Decl->bSession)
		{
			StartSession.Add(Fact, Ledger->GetFact(Fact));
		}
	}

	TalkHandle = Story->OnTalkRequested.AddUObject(this, &UDreamNarrativeRecorder::HandleTalk);
	StartHandle = Story->OnStartRequested.AddUObject(this, &UDreamNarrativeRecorder::HandleStart);
	Story->OnDialogueStarted.AddDynamic(this, &UDreamNarrativeRecorder::HandleDialogueStarted);
	WriteHandle = Ledger->OnFactWritten.AddUObject(this, &UDreamNarrativeRecorder::HandleFactWritten);
	JumpHandle = Ledger->OnJumpRequested.AddUObject(this, &UDreamNarrativeRecorder::HandleJump);
	QuestHandle = Story->OnQuestRequested.AddUObject(this, &UDreamNarrativeRecorder::HandleQuestRequested);
	EventHandle = Story->OnEventHeard.AddUObject(this, &UDreamNarrativeRecorder::HandleEvent);
	PassOn = Ledger->GetRandomSource();
	Ledger->SetRandomSource(this);
}

FString UDreamNarrativeRecorder::End()
{
	if (!IsRecording())
	{
		return FString();
	}
	// Stopped in the middle of a dialogue: what it showed so far is kept.
	TArray<TObjectPtr<UDreamNarrativeDialogueRunner>> Running;
	Watched.GetKeys(Running);
	for (UDreamNarrativeDialogueRunner* Each : Running)
	{
		Unwatch(Each);
	}

	TArray<FString> Out;
	Out.Add(TEXT("// dn.record 录制;回放就是下面这个 test 块。剧情改了回放变红时,旁边会写出 .actual(现在看到的样子),"));
	Out.Add(TEXT("// 确认是预期的改动就用它替换本文件。"));
	Out.Add(FString());
	Out.Add(TEXT("test ") + Name);

	// -- where it started: everything that was not a default, the chapter last
	TArray<TPair<FString, FString>> Given;
	auto AddGiven = [this, &Given](FName Fact, const FDnaValue& Value)
	{
		const FDnaFactDecl* Decl = Ledger->FindFactDecl(Fact);
		if (Decl && !IsQuestFact(Fact) && !(Value.Type == Decl->Type && Value.Equals(DefaultOf(*Decl))))
		{
			Given.Emplace(Fact.ToString(), ValueText(Value));
		}
	};
	for (const FDnaFactSnapshotEntry& Entry : StartSnapshot.Facts) { AddGiven(Entry.Fact, Entry.Value); }
	for (const TPair<FName, FDnaValue>& Session : StartSession) { AddGiven(Session.Key, Session.Value); }
	Given.Sort([](const TPair<FString, FString>& A, const TPair<FString, FString>& B) { return A.Key < B.Key; });
	TArray<FString> GivenLines;
	for (const TPair<FString, FString>& Entry : Given) { GivenLines.Add(FString::Printf(TEXT("    %s = %s"), *Entry.Key, *Entry.Value)); }
	TArray<FName> Visited;
	StartSnapshot.Visited.GetKeys(Visited);
	Visited.Sort(FNameLexicalLess());
	for (const FName& Node : Visited) { GivenLines.Add(FString::Printf(TEXT("    visited(%s) = %d"), *Node.ToString(), StartSnapshot.Visited[Node])); }
	TArray<FName> Once = StartSnapshot.ChosenOnce;
	Once.Sort(FNameLexicalLess());
	for (const FName& Id : Once) { GivenLines.Add(TEXT("    once #") + Id.ToString()); }
	TArray<FString> Quests;
	for (const FDnaFactSnapshotEntry& Entry : StartSnapshot.Facts)
	{
		const FName Quest = QuestOfStateFact(Entry.Fact);
		EDnaQuestState State = EDnaQuestState::Inactive;
		if (Quest.IsNone() || !::DreamNarrative::TryParseQuestState(Entry.Value.Tag, State) || State == EDnaQuestState::Inactive)
		{
			continue;
		}
		FString Line = FString::Printf(TEXT("    quest %s = %s"), *Quest.ToString(), *::DreamNarrative::QuestStateMember(State).ToString().ToLower());
		if (const float* Remaining = StartSnapshot.Timers.Find(Entry.Fact))
		{
			Line += FString::Printf(TEXT(" %.9gs"), *Remaining);
		}
		Quests.Add(Line);
	}
	Quests.Sort();
	GivenLines.Append(Quests);
	if (!StartSnapshot.Chapter.IsNone()) { GivenLines.Add(TEXT("    chapter = ") + StartSnapshot.Chapter.ToString()); }
	if (GivenLines.Num() > 0)
	{
		Out.Add(TEXT("  given"));
		Out.Append(GivenLines);
	}

	for (const FStep& Step : Steps)
	{
		if (Step.Draws.Num() > 0)
		{
			TArray<FString> Units;
			for (const float Unit : Step.Draws) { Units.Add(FString::Printf(TEXT("%.9g"), Unit)); }
			Out.Add(TEXT("  rolls ") + FString::Join(Units, TEXT(", ")));
		}
		Out.Add(TEXT("  ") + Step.Header);
		for (const TPair<int32, FString>& Child : Step.Children)
		{
			Out.Add(FString::ChrN(2 + Child.Key * 2, TEXT(' ')) + Child.Value);
		}
		for (const TPair<FName, TArray<TPair<int32, FString>>>& Block : Step.Channels)
		{
			Out.Add(TEXT("    channel ") + Block.Key.ToString());
			for (const TPair<int32, FString>& Child : Block.Value)
			{
				Out.Add(FString::ChrN(2 + Child.Key * 2, TEXT(' ')) + Child.Value);
			}
		}
	}

	// -- where it ended: every fact that changed, and the chapter
	TMap<FName, FDnaValue> Before;
	for (const FDnaFactSnapshotEntry& Entry : StartSnapshot.Facts) { Before.Add(Entry.Fact, Entry.Value); }
	Before.Append(StartSession);
	TArray<FName> Facts;
	Ledger->GetFactNames(Facts);
	Facts.Sort(FNameLexicalLess());
	int32 Expects = 0;
	for (const FName& Fact : Facts)
	{
		const FDnaFactDecl* Decl = Ledger->FindFactDecl(Fact);
		const FDnaValue Now = Ledger->GetFact(Fact);
		const FDnaValue* Then = Before.Find(Fact);
		const FDnaValue Old = Then ? *Then : (Decl ? DefaultOf(*Decl) : FDnaValue());
		if (Now.Type == Old.Type && Now.Equals(Old))
		{
			continue;
		}
		const FName Quest = QuestOfStateFact(Fact);
		EDnaQuestState State = EDnaQuestState::Inactive;
		if (!Quest.IsNone() && ::DreamNarrative::TryParseQuestState(Now.Tag, State))
		{
			Out.Add(TEXT("  ") + QuestExpectText(Quest, State));
			++Expects;
		}
		else if (!IsQuestFact(Fact))
		{
			Out.Add(TEXT("  ") + ExpectText(Fact.ToString(), Now));
			++Expects;
		}
	}
	if (!Ledger->GetChapter().IsNone())
	{
		Out.Add(TEXT("  expect chapter == ") + Ledger->GetChapter().ToString());
		++Expects;
	}
	if (Steps.Num() == 0 && Expects == 0)
	{
		// Nothing happened; a block needs a body, and a save round trip is a harmless one.
		Out.Add(TEXT("  save"));
	}

	Detach();
	return FString::Join(Out, TEXT("\n")) + TEXT("\n");
}

FString UDreamNarrativeRecorder::GetDefaultPath(const FString& InName)
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / UDreamNarrativeSettings::Get()->SourceDirectory / TEXT("Tests/Recorded") / (InName + TEXT(".dna")));
}

void UDreamNarrativeRecorder::BeginDestroy()
{
	// The ledger keeps a plain pointer to its draw source; it must not outlive this.
	if (Ledger && Ledger->GetRandomSource() == this)
	{
		Ledger->SetRandomSource(PassOn);
	}
	Super::BeginDestroy();
}

void UDreamNarrativeRecorder::Detach()
{
	TArray<TObjectPtr<UDreamNarrativeDialogueRunner>> Running;
	Watched.GetKeys(Running);
	for (UDreamNarrativeDialogueRunner* Each : Running)
	{
		Unwatch(Each);
	}
	if (Story)
	{
		Story->OnTalkRequested.Remove(TalkHandle);
		Story->OnStartRequested.Remove(StartHandle);
		Story->OnQuestRequested.Remove(QuestHandle);
		Story->OnEventHeard.Remove(EventHandle);
		Story->OnDialogueStarted.RemoveDynamic(this, &UDreamNarrativeRecorder::HandleDialogueStarted);
	}
	if (Ledger)
	{
		Ledger->OnFactWritten.Remove(WriteHandle);
		Ledger->OnJumpRequested.Remove(JumpHandle);
		if (Ledger->GetRandomSource() == this)
		{
			Ledger->SetRandomSource(PassOn);
		}
	}
	Story = nullptr;
	Ledger = nullptr;
	PassOn = nullptr;
}

void UDreamNarrativeRecorder::Unwatch(UDreamNarrativeDialogueRunner* InRunner)
{
	if (!InRunner)
	{
		return;
	}
	InRunner->OnLine.RemoveDynamic(this, &UDreamNarrativeRecorder::HandleLine);
	InRunner->OnChoiceMade.RemoveDynamic(this, &UDreamNarrativeRecorder::HandleChoice);
	InRunner->OnEnded.RemoveDynamic(this, &UDreamNarrativeRecorder::HandleEnded);
	Watched.Remove(InRunner);
}

// ---------------------------------------------------------------- steps

void UDreamNarrativeRecorder::AddStep(FStep&& Step)
{
	Steps.Add(MoveTemp(Step));
}

void UDreamNarrativeRecorder::AddStep(const FString& Header)
{
	FStep Step;
	Step.Header = Header;
	AddStep(MoveTemp(Step));
}

int32 UDreamNarrativeRecorder::DrawStep()
{
	int32 Step = Steps.Num() - 1;
	for (const TPair<TObjectPtr<UDreamNarrativeDialogueRunner>, int32>& Each : Watched)
	{
		Step = FMath::Min(Step, Each.Value);
	}
	LastDrawStep = FMath::Max(LastDrawStep, Step);
	return LastDrawStep;
}

TArray<TPair<int32, FString>>* UDreamNarrativeRecorder::LinesOf(const UDreamNarrativeDialogueRunner* InRunner)
{
	const int32* Index = Watched.Find(InRunner);
	if (!Index || !Steps.IsValidIndex(*Index))
	{
		return nullptr;
	}
	FStep& Step = Steps[*Index];
	const FName Channel = InRunner->GetChannel();
	if (Channel == ::DreamNarrative::MainChannelName())
	{
		return &Step.Children;
	}
	TPair<FName, TArray<TPair<int32, FString>>>* Block = Step.Channels.FindByPredicate([Channel](const TPair<FName, TArray<TPair<int32, FString>>>& Each) { return Each.Key == Channel; });
	return &(Block ? *Block : Step.Channels.Emplace_GetRef(Channel, TArray<TPair<int32, FString>>())).Value;
}

void UDreamNarrativeRecorder::HandleTalk(FName Character)
{
	AddStep(TEXT("talk ") + Character.ToString());
}

void UDreamNarrativeRecorder::HandleStart(FName Node)
{
	AddStep(TEXT("start ") + Node.ToString());
}

void UDreamNarrativeRecorder::HandleJump(FName Chapter)
{
	AddStep(TEXT("jump ") + Chapter.ToString());
}

void UDreamNarrativeRecorder::HandleEvent(const FDnaEventPayload& Event, EDnaWriteSource Source)
{
	// The story's own `emit` follows from the steps already written down; what it opens belongs where it was raised.
	if (Source != EDnaWriteSource::Game)
	{
		EventSteps.Add(Event.Event, DrawStep());
		return;
	}
	AddStep(EmitText(Event));
	EventSteps.Add(Event.Event, Steps.Num() - 1);
}

void UDreamNarrativeRecorder::HandleQuestRequested(EDnaQuestRequest What, FName Quest, EDnaWriteSource Who)
{
	// What the story's own commands do to quests follows from the steps already written down.
	if (Who != EDnaWriteSource::Game)
	{
		return;
	}
	AddStep(FString::Printf(TEXT("%s %s"), What == EDnaQuestRequest::Accept ? TEXT("accept") : What == EDnaQuestRequest::Complete ? TEXT("complete") : TEXT("fail"), *Quest.ToString()));
}

void UDreamNarrativeRecorder::HandleFactWritten(FName Fact, const FDnaValue& Value, EDnaWriteSource Source)
{
	if (Source != EDnaWriteSource::Game)
	{
		return;
	}
	// The game writes extern facts; a narrative one gets through only with the ownership rule off,
	// and a test can only give that one, with the story's authority.
	const FDnaFactDecl* Decl = Ledger ? Ledger->FindFactDecl(Fact) : nullptr;
	FStep Step;
	if (Decl && Decl->Owner == EDnaFactOwner::Narrative)
	{
		Step.Header = TEXT("given");
		Step.Children.Emplace(1, FString::Printf(TEXT("%s = %s"), *Fact.ToString(), *ValueText(Value)));
	}
	else
	{
		Step.Header = FString::Printf(TEXT("set %s = %s"), *Fact.ToString(), *ValueText(Value));
	}
	AddStep(MoveTemp(Step));
}

void UDreamNarrativeRecorder::HandleDialogueStarted(UDreamNarrativeDialogueRunner* InRunner)
{
	if (!InRunner)
	{
		return;
	}
	// One an event opened belongs to the step that raised it, even when it waited its turn.
	int32 Step = Steps.Num() - 1;
	if (const FDnaEventPayload* Event = InRunner->GetEventScope())
	{
		if (const int32* Raised = EventSteps.Find(Event->Event))
		{
			Step = *Raised;
		}
	}
	if (!Steps.IsValidIndex(Step))
	{
		return;
	}
	Watched.Add(InRunner, Step);
	InRunner->OnLine.AddDynamic(this, &UDreamNarrativeRecorder::HandleLine);
	InRunner->OnChoiceMade.AddDynamic(this, &UDreamNarrativeRecorder::HandleChoice);
	InRunner->OnEnded.AddDynamic(this, &UDreamNarrativeRecorder::HandleEnded);
}

void UDreamNarrativeRecorder::HandleLine(UDreamNarrativeDialogueRunner* InRunner, const FDnaLineView& Line)
{
	// Shown again after a pause: the replay, where nothing talks over it, shows it once.
	TArray<TPair<int32, FString>>* Children = Line.bRepeat ? nullptr : LinesOf(InRunner);
	if (!Children)
	{
		return;
	}
	const int32 Depth = InRunner->GetChannel() == ::DreamNarrative::MainChannelName() ? 1 : 2;
	TArray<FString> Parts;
	Line.Text.ToString().ParseIntoArray(Parts, TEXT("\n"), /*bCullEmpty=*/false);
	bool bPlain = Parts.Num() > 0;
	for (const FString& Part : Parts) { bPlain &= IsPlain(Part); }
	if (!bPlain && !Line.Id.IsNone())
	{
		Children->Emplace(Depth, TEXT("#") + Line.Id.ToString());
		return;
	}
	FString Head;
	if (Line.bNarration)
	{
		Head = TEXT("> ");
	}
	else
	{
		Head = Line.Speaker.ToString();
		if (Line.Tags.Num() > 0)
		{
			TArray<FString> Tags;
			for (const FName& Tag : Line.Tags) { Tags.Add(Tag.ToString()); }
			Head += TEXT("(") + FString::Join(Tags, TEXT(", ")) + TEXT(")");
		}
		Head += TEXT(": ");
	}
	Children->Emplace(Depth, Head + (Parts.Num() > 0 ? Parts[0] : FString()));
	for (int32 Index = 1; Index < Parts.Num(); ++Index)
	{
		Children->Emplace(Depth + 1, Parts[Index]);
	}
}

void UDreamNarrativeRecorder::HandleChoice(UDreamNarrativeDialogueRunner* InRunner, const FDnaChoiceView& Choice)
{
	TArray<TPair<int32, FString>>* Children = LinesOf(InRunner);
	if (!Children)
	{
		return;
	}
	const FString Label = Choice.Label.ToString();
	int32 Same = 0;
	for (const FDnaChoiceView& Offered : InRunner->GetChoices())
	{
		Same += Offered.Label.ToString() == Label ? 1 : 0;
	}
	const bool bByLabel = Same <= 1 && IsPlain(Label) && !Label.Contains(TEXT("\n"));
	const int32 Depth = InRunner->GetChannel() == ::DreamNarrative::MainChannelName() ? 1 : 2;
	Children->Emplace(Depth, TEXT("- ") + (bByLabel ? Label : TEXT("#") + Choice.Id.ToString()));
}

void UDreamNarrativeRecorder::HandleEnded(UDreamNarrativeDialogueRunner* InRunner)
{
	Unwatch(InRunner);
}

float UDreamNarrativeRecorder::NextUnit(FRandomStream& Stream)
{
	const float Unit = PassOn ? PassOn->NextUnit(Stream) : Stream.GetFraction();
	const int32 Step = DrawStep();
	if (Steps.IsValidIndex(Step))
	{
		Steps[Step].Draws.Add(Unit);
	}
	return Unit;
}
