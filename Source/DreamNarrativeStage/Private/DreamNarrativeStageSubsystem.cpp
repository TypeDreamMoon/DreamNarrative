// Copyright DreamMoon. All Rights Reserved.

#include "DreamNarrativeStageSubsystem.h"
#include "DreamNarrativeLedger.h"
#include "DreamNarrativeScript.h"
#include "DreamNarrativeStageDirector.h"
#include "DreamNarrativeStageModule.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Subsystems/SubsystemCollection.h"

void UDreamNarrativeStageSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	InitializeStandalone(Collection.InitializeDependency<UDreamNarrativeLedger>());
}

void UDreamNarrativeStageSubsystem::InitializeStandalone(UDreamNarrativeLedger* InLedger)
{
	Ledger = InLedger;
	if (!Ledger)
	{
		return;
	}
	Ledger->SetStageHost(this);
	for (UDreamNarrativeScript* Script : Ledger->GetScripts())
	{
		Index(Script);
	}
	ScriptHandle = Ledger->OnScriptRegistered.AddUObject(this, &UDreamNarrativeStageSubsystem::Index);
	// A save going in ends what plays: the world it was playing in is being replaced.
	ApplyingHandle = Ledger->OnSnapshotApplying.AddWeakLambda(this, [this]() { StopAllStages(EDnaStageEnd::Stopped); });
}

void UDreamNarrativeStageSubsystem::Deinitialize()
{
	if (Ledger)
	{
		for (TPair<uint64, FPlay>& Play : Plays)
		{
			Ledger->GetClock().Cancel(Play.Value.Timer);
		}
		if (Ledger->GetStageHost() == this)
		{
			Ledger->SetStageHost(nullptr);
		}
		Ledger->OnScriptRegistered.Remove(ScriptHandle);
		Ledger->OnSnapshotApplying.Remove(ApplyingHandle);
	}
	// The story goes with the game instance; nothing is told the stages are over.
	Plays.Reset();
	for (const TPair<TWeakObjectPtr<UDreamNarrativeStageDirector>, FDelegateHandle>& Handle : DirectorHandles)
	{
		if (UDreamNarrativeStageDirector* Director = Handle.Key.Get())
		{
			Director->OnPlayEnded.Remove(Handle.Value);
		}
	}
	DirectorHandles.Reset();
	Super::Deinitialize();
}

void UDreamNarrativeStageSubsystem::Index(UDreamNarrativeScript* Script)
{
	const UDnaStageSection* Section = Script ? Script->FindSection<UDnaStageSection>() : nullptr;
	if (!Section)
	{
		return;
	}
	for (int32 Index = 0; Index < Section->Stages.Num(); ++Index)
	{
		const FDnaStageData& Stage = Section->Stages[Index];
		StageIndex.Add(Stage.Name, { Section, Index });
		if (!Stage.WasName.IsNone())
		{
			Redirects.Add(Stage.WasName, Stage.Name);
		}
	}
}

const FDnaStageData* UDreamNarrativeStageSubsystem::FindStage(FName Stage) const
{
	const FStageRef* Ref = StageIndex.Find(Stage);
	if (!Ref)
	{
		const FName* Renamed = Redirects.Find(Stage);
		Ref = Renamed ? StageIndex.Find(*Renamed) : nullptr;
	}
	const UDnaStageSection* Section = Ref ? Ref->Section.Get() : nullptr;
	return Section && Section->Stages.IsValidIndex(Ref->StageIndex) ? &Section->Stages[Ref->StageIndex] : nullptr;
}

// ---------------------------------------------------------------- directors

void UDreamNarrativeStageSubsystem::RegisterDirector(UDreamNarrativeStageDirector* Director)
{
	Directors.RemoveAll([Director](const TWeakObjectPtr<UDreamNarrativeStageDirector>& Each) { return !Each.IsValid() || Each.Get() == Director; });
	Directors.Add(Director);
}

void UDreamNarrativeStageSubsystem::UnregisterDirector(UDreamNarrativeStageDirector* Director)
{
	Directors.RemoveAll([Director](const TWeakObjectPtr<UDreamNarrativeStageDirector>& Each) { return !Each.IsValid() || Each.Get() == Director; });
}

UDreamNarrativeStageDirector* UDreamNarrativeStageSubsystem::GetStoryDirector() const
{
	const UGameInstance* Instance = GetGameInstance();
	UWorld* World = Instance ? Instance->GetWorld() : nullptr;
	if (!World || World->bIsTearingDown)
	{
		return nullptr;
	}
	for (int32 Index = Directors.Num() - 1; Index >= 0; --Index)
	{
		UDreamNarrativeStageDirector* Director = Directors[Index].Get();
		if (Director && Director->GetWorld() == World && Director->bPlaysStoryStages)
		{
			return Director;
		}
	}
	APlayerController* Controller = Instance->GetFirstLocalPlayerController(World);
	if (!Controller)
	{
		Controller = World->GetFirstPlayerController();
	}
	return Controller ? UDreamNarrativeStageDirector::FindOrAddDirector(Controller) : nullptr;
}

// ---------------------------------------------------------------- playing

uint64 UDreamNarrativeStageSubsystem::PlayStage(const FDnaStageRequest& Request, FDnaStageFinishedDelegate OnFinished)
{
	const FDnaStageData* Data = FindStage(Request.Stage);
	if (!Data || !Ledger)
	{
		return 0;
	}
	if (Data->bLineRelative && Request.LineSeconds <= 0.0)
	{
		UE_LOG(LogDreamNarrativeStage, Warning, TEXT("Stage %s follows a line; it plays only with the line it is attached to"), *Data->Name.ToString());
		return 0;
	}
	const uint64 Ticket = NextTicket++;
	{
		FPlay& Play = Plays.Add(Ticket);
		Play.Stage = Data->Name;
		Play.bOnLine = Data->bLineRelative;
		Play.OnFinished = MoveTemp(OnFinished);
	}
	const FName StageName = Data->Name;
	const bool bOnLine = Data->bLineRelative;

	if (UDreamNarrativeStageDirector* Director = GetStoryDirector())
	{
		if (!DirectorHandles.Contains(Director))
		{
			DirectorHandles.Add(Director, Director->OnPlayEnded.AddUObject(this, &UDreamNarrativeStageSubsystem::HandleDirectorEnded, TWeakObjectPtr<UDreamNarrativeStageDirector>(Director)));
		}
		Plays[Ticket].Director = Director;
		// Playing may end what played there before (Replaced): the map can change under this call.
		const uint32 Number = Director->Play(*Data, static_cast<float>(Request.LineSeconds));
		FPlay* Play = Plays.Find(Ticket);
		if (Number == 0)
		{
			Plays.Remove(Ticket);
			return 0;
		}
		if (Play)
		{
			Play->PlayNumber = Number;
			if (Director->GetPlayNumber() != Number)
			{
				// Something stopped it while it started; tell the story once the clock moves.
				Play->Timer = Ledger->GetClock().Schedule(0.0, FSimpleDelegate::CreateWeakLambda(this, [this, Ticket]() { Finish(Ticket, EDnaStageEnd::Stopped); }));
			}
		}
	}
	else
	{
		// Nobody to show it to: it takes its time all the same.
		const double Seconds = (Data->Duration + Data->GetTail()) * (bOnLine ? Request.LineSeconds : 1.0);
		Plays[Ticket].Timer = Ledger->GetClock().Schedule(Seconds, FSimpleDelegate::CreateWeakLambda(this, [this, Ticket]()
		{
			if (FPlay* Play = Plays.Find(Ticket))
			{
				Play->Timer.Invalidate();
			}
			Finish(Ticket, EDnaStageEnd::Finished);
		}));
	}
	UE_LOG(LogDreamNarrativeStage, Verbose, TEXT("Stage %s plays for %s"), *StageName.ToString(), Request.Channel.IsNone() ? TEXT("the game") : *Request.Channel.ToString());
	Ledger->OnStageActivity.Broadcast(StageName, true, bOnLine);
	return Ticket;
}

void UDreamNarrativeStageSubsystem::StopStage(uint64 Ticket)
{
	const FPlay* Play = Plays.Find(Ticket);
	if (!Play)
	{
		return;
	}
	UDreamNarrativeStageDirector* Director = Play->Director.Get();
	if (Director && Play->PlayNumber != 0 && Director->GetPlayNumber() == Play->PlayNumber)
	{
		// The director's end comes back through HandleDirectorEnded.
		Director->Stop(EDnaStageEnd::Stopped);
	}
	Finish(Ticket, EDnaStageEnd::Stopped);
}

void UDreamNarrativeStageSubsystem::StopAllStages(EDnaStageEnd Reason)
{
	TArray<uint64> Tickets;
	Plays.GetKeys(Tickets);
	for (const uint64 Ticket : Tickets)
	{
		const FPlay* Play = Plays.Find(Ticket);
		if (!Play)
		{
			continue;
		}
		UDreamNarrativeStageDirector* Director = Play->Director.Get();
		if (Director && Play->PlayNumber != 0 && Director->GetPlayNumber() == Play->PlayNumber)
		{
			Director->Stop(Reason);
		}
		Finish(Ticket, Reason);
	}
}

bool UDreamNarrativeStageSubsystem::PlayStageByName(FName Stage)
{
	FDnaStageRequest Request;
	Request.Stage = Stage;
	return PlayStage(Request, FDnaStageFinishedDelegate()) != 0;
}

void UDreamNarrativeStageSubsystem::HandleDirectorEnded(uint32 PlayNumber, EDnaStageEnd Reason, TWeakObjectPtr<UDreamNarrativeStageDirector> Director)
{
	for (const TPair<uint64, FPlay>& Play : Plays)
	{
		if (Play.Value.PlayNumber == PlayNumber && Play.Value.Director == Director)
		{
			Finish(Play.Key, Reason);
			return;
		}
	}
}

void UDreamNarrativeStageSubsystem::Finish(uint64 Ticket, EDnaStageEnd Reason)
{
	FPlay Play;
	if (!Plays.RemoveAndCopyValue(Ticket, Play))
	{
		return;
	}
	if (Ledger)
	{
		Ledger->GetClock().Cancel(Play.Timer);
		Ledger->OnStageActivity.Broadcast(Play.Stage, false, Play.bOnLine);
	}
	Play.OnFinished.ExecuteIfBound(Reason);
}
